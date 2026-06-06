#!/usr/bin/env python3
"""Build browser-readable MiniOS demo assets from verification logs."""

from __future__ import annotations

import json
import re
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "build" / "evidence"
ASSETS = ROOT / "demo" / "assets"


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def first_match(pattern: str, text: str, default: str = "-") -> str:
    match = re.search(pattern, text, re.MULTILINE)
    return match.group(1).strip() if match else default


def count(pattern: str, text: str) -> int:
    return len(re.findall(pattern, text, re.MULTILINE))


def recent_tick_lines(text: str, limit: int = 12) -> list[str]:
    lines = [line for line in text.splitlines() if line.startswith("[tick ")]
    return lines[-limit:]


def compact_lines(lines: list[str], limit: int = 14) -> list[str]:
    non_empty = [line.rstrip() for line in lines if line.strip()]
    important = [
        line
        for line in non_empty
        if re.search(
            r"create_process|Kernel tick|PID\s+Name|MEM_WAIT|scheduler=|policy=|sched |irq wake|mm wake|vm "
            r"|fault|hit|TinyFS|fs |fd=|EDF|RMS|producer|readers|dining|tracebench|推荐|Total=|External|bytes=|EOF",
            line,
            re.IGNORECASE,
        )
    ]
    selected = important or non_empty
    if len(selected) > limit:
        selected = selected[: limit - 4] + ["..."] + selected[-3:]
    return [line[:150] for line in selected]


def collect_phases(text: str) -> list[str]:
    phases: list[str] = []
    for line in text.splitlines():
        match = re.search(r"\[tour\]\s+(Phase\s+\d+(?:\.\d+)?/\d+\s+.+)", line)
        if match:
            phases.append(match.group(1))
    return phases


def parse_blocks(text: str) -> list[dict[str, object]]:
    prompt = re.compile(r"^minios:(?:(.*):)?(\d+)\$\s*(.*)$")
    blocks: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    for line in text.splitlines():
        match = prompt.match(line)
        if match:
            if current is not None:
                blocks.append(current)
            current = {"tick": int(match.group(2)), "command": match.group(3), "lines": []}
            continue
        if current is not None:
            current["lines"].append(line)
    if current is not None:
        blocks.append(current)
    return blocks


def parse_phase_title(text: str, fallback_index: int) -> tuple[str, str, str]:
    match = re.search(r"Phase\s+(\d+(?:\.\d+)?)/(\d+)\s+([^:]+):\s*(.+)", text)
    if not match:
        return f"Phase {fallback_index + 1}", text, text
    title = match.group(3).strip()
    subtitle = match.group(4).strip()
    return title, subtitle, f"Phase {match.group(1)}/{match.group(2)} {title}: {subtitle}"


def summarize_command(commands: list[str]) -> str:
    if not commands:
        return "(no command)"
    if len(commands) <= 4:
        return "; ".join(commands)
    return f"{commands[0]}; {commands[1]}; ...; {commands[-1]} ({len(commands)} commands)"


def parse_proc_patch(lines: list[str]) -> dict[str, list[object]]:
    states = {"READY", "RUNNING", "BLOCKED", "MEM_WAIT", "TERMINATED"}
    patch: dict[str, list[object]] = {}
    for line in lines:
        parts = line.split()
        if len(parts) < 14 or not parts[0].isdigit() or parts[-1] not in states:
            continue
        try:
            patch[parts[0]] = [parts[1], int(parts[3]), int(parts[8]), parts[-1]]
        except ValueError:
            continue
    return patch


def max_tick(lines: list[str], default_tick: int) -> int:
    ticks = [int(value) for value in re.findall(r"\[tick\s+(\d+)\]", "\n".join(lines))]
    return max(ticks + [default_tick])


def infer_scheduler(commands: list[str], lines: list[str], current: str) -> str:
    text = "\n".join(commands + lines)
    for pattern in [
        r"scheduler=([A-Z]+)",
        r"\[scheduler\]\s+policy=([A-Z]+)",
        r"sched set policy=([A-Z]+)",
        r"policy=([A-Z]+)",
    ]:
        matches = re.findall(pattern, text)
        if matches:
            return matches[-1].upper()
    for command in commands:
        if command.startswith("sched "):
            mode = command.split()[1].lower() if len(command.split()) > 1 else ""
            if mode in {"rr", "mlfq", "fcfs", "sjf", "priority"}:
                return mode.upper()
    return current


def infer_running(lines: list[str], current: str) -> str:
    text = "\n".join(lines)
    if "cpu idle" in text:
        return "idle"
    matches = re.findall(r"sched dispatch pid=\d+ name=([A-Za-z0-9_-]+)", text)
    return matches[-1] if matches else current


def infer_memory(lines: list[str], current: int) -> int:
    text = "\n".join(lines)
    matches = re.findall(r"Total=128,\s+Used=(\d+)", text)
    return int(matches[-1]) if matches else current


def infer_vm(lines: list[str], current: str) -> str:
    text = "\n".join(lines)
    match = None
    for pattern in [
        r"Kernel VM state: policy=(\w+) frames=(\d+) faults=(\d+) hits=(\d+) fault_rate=([0-9.]+%)",
        r"\[vm\]\s+online policy=(\w+) frames=(\d+) faults=(\d+) hits=(\d+) fault_rate=([0-9.]+%)",
    ]:
        found = re.findall(pattern, text)
        if found:
            match = found[-1]
    if match is None:
        return current
    policy, frames, faults, hits, rate = match
    return f"VM: {policy.upper()} frames={frames} faults={faults} hits={hits} rate={rate}"


def infer_fd(lines: list[str], current: str) -> str:
    text = "\n".join(lines)
    if "Kernel open file table" in text and ("FD    Mode" in text or "Owner" in text):
        return "Open FD table: printed in real status snapshot"
    open_matches = re.findall(r"fs open fd=(\d+) path=([^ ]+) mode=([^ ]+) offset=(\d+)", text)
    close_matches = re.findall(r"fs close (?:owner=\d+ )?fd=(\d+)", text)
    if open_matches:
        fd, path, mode, offset = open_matches[-1]
        if close_matches and close_matches[-1] == fd:
            return "Open FD table: empty after close"
        return f"Open FD table\nFD {fd} mode={mode} offset={offset}\n{path}"
    return current


def build_story_asset(full_demo: str) -> dict[str, object]:
    blocks = parse_blocks(full_demo)
    groups: list[dict[str, object]] = []
    current: dict[str, object] | None = None

    for block in blocks:
        command = str(block["command"])
        output_lines = list(block["lines"])
        if command.startswith("note Phase"):
            phase_line = next((line.replace("[tour] ", "", 1) for line in output_lines if line.startswith("[tour] ")), command[5:])
            title, subtitle, full_title = parse_phase_title(phase_line, len(groups))
            current = {"phase": len(groups), "title": title, "subtitle": subtitle, "full_title": full_title, "blocks": []}
            groups.append(current)
            continue
        if current is not None:
            current["blocks"].append(block)

    phases: list[list[str]] = []
    steps: list[dict[str, object]] = []
    evidence: list[str] = []
    modules: list[list[object]] = []
    sched = "RR"
    running = "idle"
    mem = 0
    vm = "VM: LRU frames=3 faults=0 hits=0"
    fd = "Open FD table: empty"

    for group in groups:
        phase_index = int(group["phase"])
        title = str(group["title"])
        subtitle = str(group["subtitle"])
        phase_blocks = list(group["blocks"])
        commands = [str(block["command"]) for block in phase_blocks]
        lines: list[str] = []
        prompt_tick = 0
        for block in phase_blocks:
            prompt_tick = int(block["tick"])
            lines.extend(str(line) for line in block["lines"])

        tick = max_tick(lines, prompt_tick)
        sched = infer_scheduler(commands, lines, sched)
        running = infer_running(lines, running)
        mem = infer_memory(lines, mem)
        vm = infer_vm(lines, vm)
        fd = infer_fd(lines, fd)
        proc_patch = parse_proc_patch(lines)
        out_lines = compact_lines(lines)

        step: dict[str, object] = {
            "phase": phase_index,
            "cmd": summarize_command(commands),
            "out": "\n".join(out_lines) if out_lines else "(本阶段无额外输出)",
            "note": f"阶段目标: {subtitle}",
            "tick": tick,
            "sched": sched,
            "running": running,
            "mem": mem,
            "vm": vm,
            "fd": fd,
        }
        if proc_patch:
            step["procPatch"] = proc_patch
        phases.append([title, subtitle])
        steps.append(step)
        evidence.append(f"阶段记录: {group['full_title']}\n命令数: {len(commands)}; 当前 tick={tick}; 模块状态已更新")
        modules.append([title[:10], phase_index])

    return {
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "label": "Full Demo",
        "subtitle": "完整模式展示端到端 MiniOS 运行轨迹，联动命令、输出、tick、PCB 和模块状态。",
        "phases": phases,
        "steps": steps,
        "evidence": evidence,
        "modules": modules,
    }


def main() -> int:
    if not EVIDENCE.exists() or not (EVIDENCE / "full_demo.log").exists():
        print("missing build/evidence/full_demo.log; restore evidence logs before exporting demo assets")
        return 1

    full_demo = read_text(EVIDENCE / "full_demo.log")
    tracebench = read_text(EVIDENCE / "tracebench.log")
    autotune = read_text(EVIDENCE / "autotune.log")
    core_tests = read_text(EVIDENCE / "core_tests.txt")

    final_tick_matches = re.findall(r"\[tick\s+(\d+)\]", full_demo)
    final_tick = int(final_tick_matches[-1]) if final_tick_matches else 0

    data = {
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "status": {
            "core_tests": "passed" if "core tests passed" in core_tests else "unknown",
            "record_count": len(list(EVIDENCE.glob("*"))) if EVIDENCE.exists() else 0,
            "full_demo_lines": len(full_demo.splitlines()),
            "full_demo_commands": count(r"^minios:(?:.*:)?\d+\$", full_demo),
            "final_tick": final_tick,
        },
        "phases": collect_phases(full_demo),
        "events": {
            "process_creates": count(r"create_process -> pid=", full_demo),
            "context_switches": count(r"sched dispatch", full_demo),
            "mlfq_events": count(r"sched mlfq", full_demo),
            "irq_wakeups": count(r"irq wake", full_demo),
            "mem_wait_wakeups": count(r"mm wake", full_demo),
            "vm_faults": count(r"vm fault", full_demo),
            "vm_hits": count(r"vm hit", full_demo),
            "fs_events": count(r"^\[tick \d+\] fs ", full_demo),
            "process_exits": count(r"proc exit", full_demo),
        },
        "autotune": {
            "scheduler": first_match(r"推荐调度策略:\s*(.+)", autotune),
            "vm": first_match(r"推荐 VM 策略:\s*(.+)", autotune),
            "concurrency": first_match(r"推荐并发配置:\s*(.+)", autotune),
            "speedup": first_match(r"speedup=([0-9.]+x)", autotune),
        },
        "tracebench": {
            "vm_summary": first_match(r"^(fifo\s+4\s+\d+\s+\d+\s+[0-9.]+\s*%)", tracebench),
            "scheduler_summary": first_match(r"^(RR q=2\s+\d+\s+\d+\s+\d+\s+[0-9.]+\s+[0-9.]+\s+[0-9.]+)", tracebench),
            "fs_summary": first_match(r"^(medium\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+)", tracebench),
        },
        "recent_dmesg": recent_tick_lines(full_demo),
    }

    ASSETS.mkdir(parents=True, exist_ok=True)
    (ASSETS / "evidence.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (ASSETS / "story_full.json").write_text(
        json.dumps(build_story_asset(full_demo), ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (ASSETS / "full_demo_tail.txt").write_text("\n".join(data["recent_dmesg"]) + "\n", encoding="utf-8")
    print(f"demo assets written to {ASSETS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
