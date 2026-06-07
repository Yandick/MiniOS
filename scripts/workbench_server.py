#!/usr/bin/env python3
"""Serve MiniOS Workbench and bridge browser commands to the real MiniOS shell."""

from __future__ import annotations

import argparse
import json
import mimetypes
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEMO_DIR = PROJECT_ROOT / "demo"
BINARY = PROJECT_ROOT / "build" / "os_project"
MAX_COMMAND_CHARS = 8191


class MiniOSSession:
    FD_ALIAS_TOKENS = {"$fd", "$lastfd", "${fd}", "${lastfd}", "{fd}", "{lastfd}", "@fd", "@lastfd"}

    def __init__(self, root: Path) -> None:
        self.root = root
        self.proc: subprocess.Popen[str] | None = None
        self.reader: threading.Thread | None = None
        self.output: queue.Queue[str] = queue.Queue()
        self.lock = threading.RLock()
        self.started_at = ""
        self.last_fd: int | None = None
        self.open_fds: dict[int, dict[str, Any]] = {}
        self.start()

    def start(self) -> None:
        with self.lock:
            self._stop_locked()
            self._clear_fd_state_locked()
            self._ensure_binary()
            command = [str(BINARY), "tour", "--interactive"]
            if shutil.which("stdbuf") is not None:
                command = ["stdbuf", "-o0", "-e0", *command]
            self.started_at = datetime.now().astimezone().isoformat(timespec="seconds")
            self.proc = subprocess.Popen(
                command,
                cwd=self.root,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
            self.reader = threading.Thread(target=self._read_output, args=(self.proc,), daemon=True)
            self.reader.start()

    def restart(self) -> str:
        self.start()
        return self.wait_for_output(0.45)

    def stop(self) -> None:
        with self.lock:
            self._stop_locked()

    def send_command(self, command: str, wait_seconds: float = 0.22) -> tuple[str, str]:
        command = command.replace("\r", "").strip("\n")
        if len(command) > MAX_COMMAND_CHARS:
            raise ValueError(f"command is too long; max {MAX_COMMAND_CHARS} characters")
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                self.start()
            assert self.proc is not None
            assert self.proc.stdin is not None
            resolved_command = self._resolve_fd_aliases_locked(command)
            self.proc.stdin.write(resolved_command + "\n")
            self.proc.stdin.flush()
        output = self.wait_for_output(wait_seconds)
        with self.lock:
            self._capture_fd_state_locked(resolved_command, output)
        return output, resolved_command

    def drain_output(self) -> str:
        parts: list[str] = []
        while True:
            try:
                parts.append(self.output.get_nowait())
            except queue.Empty:
                break
        return "".join(parts)

    def wait_for_output(self, seconds: float) -> str:
        deadline = time.monotonic() + seconds
        parts: list[str] = []
        while time.monotonic() < deadline:
            try:
                parts.append(self.output.get(timeout=0.03))
            except queue.Empty:
                continue
        parts.append(self.drain_output())
        return "".join(parts)

    def state(self) -> dict[str, Any]:
        proc = self.proc
        return {
            "running": proc is not None and proc.poll() is None,
            "pid": proc.pid if proc is not None else None,
            "started_at": self.started_at,
            "cwd": str(self.root),
            "binary": str(BINARY),
            "fd_state": self._fd_state_locked(),
            "output": self.drain_output(),
        }

    def _ensure_binary(self) -> None:
        if BINARY.exists():
            return
        make_tool = self._make_tool()
        if make_tool is None:
            raise RuntimeError("MiniOS binary is missing and no make tool was found")
        result = subprocess.run(
            [make_tool, BINARY.relative_to(PROJECT_ROOT).as_posix()],
            cwd=self.root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if result.returncode != 0:
            raise RuntimeError(f"failed to build {BINARY}:\n{result.stdout}")

    @staticmethod
    def _make_tool() -> str | None:
        return shutil.which("make") or shutil.which("gmake")

    def _read_output(self, proc: subprocess.Popen[str]) -> None:
        assert proc.stdout is not None
        while True:
            chunk = proc.stdout.read(1)
            if chunk == "":
                break
            self.output.put(chunk)
        code = proc.wait()
        self.output.put(f"\n[workbench] MiniOS process exited with code {code}\n")

    def _stop_locked(self) -> None:
        if self.proc is None:
            return
        proc = self.proc
        self.proc = None
        if proc.poll() is None:
            try:
                proc.terminate()
                proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=1.0)
            except OSError:
                pass

    def _clear_fd_state_locked(self) -> None:
        self.last_fd = None
        self.open_fds = {}

    def _fd_state_locked(self) -> dict[str, Any]:
        return {
            "last_fd": self.last_fd,
            "open_fds": [
                {"fd": fd, **info}
                for fd, info in sorted(self.open_fds.items(), key=lambda item: item[0])
            ],
        }

    def _resolve_fd_aliases_locked(self, command: str) -> str:
        parts = command.split(maxsplit=2)
        if len(parts) < 2:
            return command
        name = parts[0].lower()
        if name not in {"readfd", "writefd", "seekfd", "close"}:
            return command
        if parts[1].lower() not in self.FD_ALIAS_TOKENS:
            return command
        if self.last_fd is None:
            raise ValueError("no captured fd; run open <path> [r|w|a|rw] first")
        parts[1] = str(self.last_fd)
        return " ".join(parts)

    def _capture_fd_state_locked(self, command: str, output: str) -> None:
        parts = command.split(maxsplit=2)
        if not parts:
            return
        name = parts[0].lower()
        if name == "open":
            self._capture_open_output_locked(output)
            return
        if name in {"fd", "fds", "lsof", "status"}:
            self._capture_fd_table_locked(output)
            return
        if name == "loadfs" and "loaded image from" in output:
            self._clear_fd_state_locked()
            return
        if name == "close":
            self._capture_close_locked(parts, output)
            return
        if name in {"readfd", "writefd", "seekfd"}:
            fd = self._command_fd(parts)
            if fd is not None and "[syscall]" in output and "usage:" not in output:
                self.last_fd = fd

    def _capture_open_output_locked(self, output: str) -> None:
        match = re.search(r"\[syscall\]\s+open\s+->\s+fd=(\d+)\s+path=(\S+)\s+mode=(\S+)", output)
        if match is None:
            return
        fd = int(match.group(1))
        self.last_fd = fd
        self.open_fds[fd] = {
            "path": match.group(2),
            "mode": match.group(3),
            "offset": None,
            "state": "open",
        }

    def _capture_fd_table_locked(self, output: str) -> None:
        if "Owner" not in output or "FD" not in output:
            return
        open_fds: dict[int, dict[str, Any]] = {}
        for line in output.splitlines():
            match = re.match(r"\s*(\d+)\s+(\d+)\s+(\S+)\s+(\d+)\s+(\S+)\s+(.+?)\s*$", line)
            if match is None:
                continue
            owner = int(match.group(1))
            if owner != 0:
                continue
            fd = int(match.group(2))
            open_fds[fd] = {
                "path": match.group(6),
                "mode": match.group(3),
                "offset": int(match.group(4)),
                "state": match.group(5),
            }
        self.open_fds = open_fds
        if self.last_fd not in self.open_fds:
            self.last_fd = max(open_fds) if open_fds else None

    def _capture_close_locked(self, parts: list[str], output: str) -> None:
        if "[syscall] close -> fd closed" not in output:
            return
        fd = self._command_fd(parts)
        if fd is None:
            return
        self.open_fds.pop(fd, None)
        if self.last_fd == fd:
            self.last_fd = max(self.open_fds) if self.open_fds else None

    @staticmethod
    def _command_fd(parts: list[str]) -> int | None:
        if len(parts) < 2:
            return None
        try:
            return int(parts[1])
        except ValueError:
            return None


class WorkbenchHandler(SimpleHTTPRequestHandler):
    session: MiniOSSession

    def translate_path(self, path: str) -> str:
        parsed = urlparse(path)
        raw_path = unquote(parsed.path)
        if raw_path in ("/", "/home", "/workbench", "/demo"):
            route_map = {
                "/": "/index.html",
                "/home": "/index.html",
                "/workbench": "/workbench.html",
                "/demo": "/minios_story.html",
            }
            raw_path = route_map[raw_path]
        target = (DEMO_DIR / raw_path.lstrip("/")).resolve()
        if not str(target).startswith(str(DEMO_DIR.resolve())):
            return str(DEMO_DIR / "index.html")
        return str(target)

    def do_GET(self) -> None:
        if self.path.startswith("/api/state"):
            self._send_json(self.session.state())
            return
        super().do_GET()

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/api/command":
                payload = self._read_json()
                command = str(payload.get("command", ""))
                wait_seconds = self._command_wait_seconds(command, payload)
                output, resolved_command = self.session.send_command(command, wait_seconds)
                state = self.session.state()
                output += state.pop("output", "")
                self._send_json({"ok": True, **state, "output": output, "resolved_command": resolved_command})
                return
            if parsed.path == "/api/restart":
                output = self.session.restart()
                state = self.session.state()
                output += state.pop("output", "")
                self._send_json({"ok": True, **state, "output": output})
                return
        except Exception as exc:  # noqa: BLE001 - API should return errors as JSON.
            self._send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)
            return
        self.send_error(HTTPStatus.NOT_FOUND, "API route not found")

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def guess_type(self, path: str) -> str:
        if path.endswith(".js"):
            return "application/javascript; charset=utf-8"
        if path.endswith(".css"):
            return "text/css; charset=utf-8"
        return mimetypes.guess_type(path)[0] or "application/octet-stream"

    def log_message(self, format: str, *args: Any) -> None:
        sys.stderr.write("[workbench] " + format % args + "\n")

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length > 8192:
            raise ValueError("request body is too large")
        raw = self.rfile.read(length).decode("utf-8")
        return json.loads(raw or "{}")

    def _send_json(self, payload: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _command_wait_seconds(self, command: str, payload: dict[str, Any]) -> float:
        requested = payload.get("wait_seconds")
        if isinstance(requested, (int, float)):
            return max(0.1, min(float(requested), 4.0))
        name = command.strip().split(maxsplit=1)[0] if command.strip() else ""
        if name in {"bench", "tracebench", "autotune", "benchcsv", "perfreport", "sync", "realtime"}:
            return 1.1
        if name in {"status", "ps", "top", "sched", "page", "dmesg"}:
            return 0.55
        return 0.36 if bool(payload.get("silent", False)) else 0.28

def make_server(host: str, port: int, session: MiniOSSession) -> ThreadingHTTPServer:
    for candidate in range(port, 8100):
        try:
            WorkbenchHandler.session = session
            server = ThreadingHTTPServer((host, candidate), WorkbenchHandler)
            server.daemon_threads = True
            return server
        except OSError:
            continue
    raise OSError(f"no free port from {port} to 8099")


def normalize_start_path(path: str) -> str:
    clean = path.strip() or "/"
    return clean if clean.startswith("/") else f"/{clean}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve MiniOS Workbench.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8001)
    parser.add_argument("--start-path", default="/workbench.html")
    args = parser.parse_args()

    session = MiniOSSession(PROJECT_ROOT)
    server = make_server(args.host, args.port, session)
    host, port = server.server_address
    start_path = normalize_start_path(args.start_path)

    def shutdown(_signum: int, _frame: Any) -> None:
        session.stop()
        server.server_close()
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    print(f"MiniOS URL: http://{host}:{port}{start_path}", flush=True)
    print("Press Ctrl+C to stop the server.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        session.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
