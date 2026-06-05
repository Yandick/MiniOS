const files = {
  "/home/project/main.c": {
    title: "main.c",
    language: "c",
    content: `#include <minios.h>

int main(void) {
  create("shell", 4, 12, 1);
  create("editor", 5, 18, 2);
  sched_mlfq(1);
  run_ticks(8);
  return 0;
}
`
  },
  "/home/project/README.md": {
    title: "README.md",
    language: "markdown",
    content: `# MiniOS Workspace

- Unified Shell: process, memory, VM, TinyFS, fd, sync, realtime, custom performance tests.
- Current backend: build/os_project tour --interactive.
- Acceptance entry: make verify.
`
  },
  "/home/project/build.log": {
    title: "build.log",
    language: "log",
    content: `[build] gcc -std=c11 -Wall -Wextra -Werror
[test] core tests passed
[verify] shell command coverage passed
`
  },
  "/var/log/dmesg.log": {
    title: "dmesg.log",
    language: "log",
    content: `[tick 000] boot init scheduler=RR quantum=2 memory=128 fs_blocks=128 vm=LRU frames=3
`
  },
  "/docs/performance.md": {
    title: "performance.md",
    language: "markdown",
    content: `# State Analysis Notes

Run:

\`\`\`bash
tracebench
autotune 16 3
\`\`\`
`
  }
};

const shellCommands = [
  "about", "access", "autotune", "bench", "benchsubset", "benchcsv", "block", "cat", "cd", "clear", "close", "create", "dmesg",
  "df", "echo", "exit", "fd", "fds", "free", "fscheck", "help", "hostname", "jobs", "kill", "loadfs", "ls", "lsof",
  "man", "mem", "mkdir", "mount", "open", "uname", "uptime", "whoami",
  "note", "overview", "page", "perfreport", "ps", "put", "pwd", "quit", "readfd", "realtime", "rm", "rmdir", "run",
  "savefs", "sched", "seekfd", "shutdown", "sleep", "status", "sync", "tick", "top", "touch", "tracebench", "tree",
  "vm", "write", "writefd"
];

const commandSnippets = [
  ["Core", "help"], ["Core", "man files"], ["Core", "man proc"], ["Core", "about"], ["Core", "overview"],
  ["Core", "status"], ["Core", "ps"], ["Core", "top"], ["Core", "dmesg"], ["Core", "pwd"], ["Core", "cd /"],
  ["Core", "ls"], ["Core", "tree"], ["Core", "clear"], ["Core", "uname"], ["Core", "uptime"],
  ["Core", "whoami"], ["Core", "hostname"], ["Core", "echo shell-ready"],
  ["Files", "mkdir /workspace"], ["Files", "mkdir /workspace/src"], ["Files", "touch /workspace/log.txt boot"],
  ["Files", "put /workspace/src/app.c init\\nrun\\nflush"], ["Files", "write /workspace/log.txt -append"],
  ["Files", "cat /workspace/log.txt"], ["Files", "rm /workspace/log.txt"], ["Files", "rmdir /workspace/src"],
  ["Files", "savefs build/workbench_tinyfs.img"], ["Files", "fscheck build/workbench_tinyfs.img"],
  ["Files", "loadfs build/workbench_tinyfs.img"], ["Files", "df"], ["Files", "mount"],
  ["FD", "open /workspace/log.txt r"], ["FD", "open /workspace/log.txt w"], ["FD", "open /workspace/log.txt a"],
  ["FD", "open /workspace/log.txt rw"], ["FD", "readfd $fd 64"], ["FD", "writefd $fd packet"],
  ["FD", "seekfd $fd 0"], ["FD", "fd"], ["FD", "fds"], ["FD", "lsof"], ["FD", "close $fd"],
  ["Process", "create ingest 7 18 2"], ["Process", "create parser 5 22 1"], ["Process", "run 4"],
  ["Process", "tick 1"], ["Process", "sleep 2 3"], ["Process", "block 2 2"], ["Process", "jobs"], ["Process", "kill 1"],
  ["Scheduler", "sched"], ["Scheduler", "sched compare"], ["Scheduler", "sched demo 2"], ["Scheduler", "sched 2"],
  ["Scheduler", "sched rr 2"], ["Scheduler", "sched mlfq 1"], ["Scheduler", "sched fcfs"],
  ["Scheduler", "sched sjf"], ["Scheduler", "sched priority"],
  ["Memory", "mem"], ["Memory", "free"], ["VM", "access 1 0"], ["VM", "vm"], ["VM", "vm fifo"], ["VM", "vm lru"],
  ["VM", "vm frames 4"], ["VM", "vm reset"], ["VM", "page 3"], ["VM", "page demo 3"],
  ["Sync", "sync pc"], ["Sync", "sync rw"], ["Sync", "sync dp"], ["Sync", "sync all"],
  ["Realtime", "realtime 30"],
  ["Analysis", "tracebench"], ["Analysis", "benchsubset 32 10"], ["Analysis", "autotune 16 3"],
  ["Analysis", "benchcsv build/workbench_bench.csv 32 50"],
  ["Analysis", "perfreport build/workbench_perf.md 32 50"]
].map(([category, command]) => ({ category, command }));

const workspaceReadmeContent = `# MiniOS Workbench

Use the terminal below like a small VS Code integrated terminal for MiniOS.

Start here:
cat /workspace/docs/files.md
cat /workspace/docs/process.md
cat /workspace/docs/vm.md
cat /workspace/docs/timing.md

Core checks:
status
dmesg
tree
df
`;

const workspaceFilesDocContent = `# Files And FD

uname
uptime
df
mount
mkdir /workspace/src
touch /workspace/src/app.c boot
put /workspace/src/app.c init\\nrun\\nflush
cat /workspace/src/app.c
tree
savefs build/workbench.img
fscheck build/workbench.img
loadfs build/workbench.img

open /workspace/src/app.c rw
readfd $fd 32
seekfd $fd 0
writefd $fd PATCH
fd
lsof
close $fd
`;

const workspaceProcessDocContent = `# Process And Scheduling

create shell 3 8 2
create worker 4 8 1
jobs
run 3
block 2 2
sched compare
sched rr 2
sched mlfq 1
status
dmesg
`;

const workspaceVmDocContent = `# Memory And VM

mem
free
access 1 0
access 1 1
access 1 0
vm
vm fifo
vm lru
page 3
`;

const workspaceTimingDocContent = `# Sync Realtime State Analysis

sync pc
sync rw
sync dp
realtime 30
tracebench
benchsubset 32 10
autotune 16 3
`;

const state = {
  activePath: "",
  openPaths: [],
  commandHistory: [],
  historyIndex: 0,
  online: false,
  sending: false,
  refreshingTree: false,
  fsTree: { dirs: ["/"], files: [] },
  expandedDirs: new Set(["/"]),
  dirtyPaths: new Set(),
  loadingPath: "",
  terminalMaximized: false,
  shellCwd: "/",
  lastTabPrefix: "",
  lastTabAt: 0
};

const els = {
  workspacePath: document.getElementById("workspacePath"),
  connectionStatus: document.getElementById("connectionStatus"),
  openCommandPaletteTop: document.getElementById("openCommandPaletteTop"),
  restartMiniOS: document.getElementById("restartMiniOS"),
  fileTree: document.getElementById("fileTree"),
  refreshTree: document.getElementById("refreshTree"),
  treeSummary: document.getElementById("treeSummary"),
  tabs: document.getElementById("tabs"),
  activePath: document.getElementById("activePath"),
  editorState: document.getElementById("editorState"),
  lineNumbers: document.getElementById("lineNumbers"),
  editor: document.getElementById("editor"),
  saveFile: document.getElementById("saveFile"),
  copyPath: document.getElementById("copyPath"),
  openCommandPalette: document.getElementById("openCommandPalette"),
  clearTerminal: document.getElementById("clearTerminal"),
  toggleTerminalMax: document.getElementById("toggleTerminalMax"),
  terminalScreen: document.getElementById("terminalScreen"),
  terminalOutput: document.getElementById("terminalOutput"),
  terminalForm: document.getElementById("terminalForm"),
  commandInput: document.getElementById("commandInput"),
  promptLabel: document.getElementById("promptLabel"),
  terminalResizer: document.getElementById("terminalResizer"),
  contextMenu: document.getElementById("contextMenu"),
  commandPalette: document.getElementById("commandPalette"),
  commandSearch: document.getElementById("commandSearch"),
  closeCommandPalette: document.getElementById("closeCommandPalette"),
  scriptList: document.getElementById("scriptList"),
  commandList: document.getElementById("commandList"),
  moduleActions: document.getElementById("moduleActions"),
  workspace: document.getElementById("workspace"),
  inspector: document.querySelector(".inspector"),
  kernelPid: document.getElementById("kernelPid"),
  kernelTick: document.getElementById("kernelTick"),
  kernelScheduler: document.getElementById("kernelScheduler"),
  kernelStarted: document.getElementById("kernelStarted"),
  statusCwd: document.getElementById("statusCwd"),
  statusScheduler: document.getElementById("statusScheduler"),
  statusTick: document.getElementById("statusTick"),
  statusSummary: document.getElementById("statusSummary"),
  vmSummary: document.getElementById("vmSummary"),
  fdSummary: document.getElementById("fdSummary"),
  dmesgSummary: document.getElementById("dmesgSummary")
};

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function basename(path) {
  return path.split("/").filter(Boolean).pop() || path;
}

function dirname(path) {
  if (path === "/") return "/";
  const parts = path.split("/").filter(Boolean);
  parts.pop();
  return parts.length === 0 ? "/" : `/${parts.join("/")}`;
}

function updateLineNumbers() {
  const count = Math.max(1, els.editor.value.split("\n").length);
  els.lineNumbers.textContent = Array.from({ length: count }, (_value, index) => String(index + 1)).join("\n");
}

function setEditorValue(value) {
  els.editor.value = value;
  updateLineNumbers();
}

function cacheActiveDraft() {
  if (!state.activePath || !files[state.activePath] || !state.dirtyPaths.has(state.activePath)) return;
  files[state.activePath].draft = els.editor.value;
}

function updateEditorState(label) {
  const dirty = state.activePath && state.dirtyPaths.has(state.activePath);
  els.editorState.textContent = label || (dirty ? "dirty" : "clean");
  els.editorState.classList.toggle("dirty", Boolean(dirty));
  if (els.saveFile) els.saveFile.disabled = !state.activePath || els.editor.readOnly;
  renderTabs();
}

function renderTabs() {
  els.tabs.innerHTML = state.openPaths.map(path => `
    <button class="tab ${path === state.activePath ? "active" : ""}" type="button" data-path="${escapeHtml(path)}">
      <span>${escapeHtml(basename(path))}</span>
      ${state.dirtyPaths.has(path) ? '<i class="tab-dirty" aria-hidden="true"></i>' : ""}
      <i class="tab-close" data-close="${escapeHtml(path)}" aria-label="Close ${escapeHtml(basename(path))}">×</i>
    </button>
  `).join("");
}

function showEmptyEditor() {
  cacheActiveDraft();
  state.activePath = "";
  els.activePath.textContent = "No file selected";
  setEditorValue("");
  els.editor.readOnly = true;
  document.querySelectorAll(".file").forEach(button => button.classList.remove("active"));
  updateEditorState("clean");
}

function setActiveFile(path) {
  if (path !== state.activePath) cacheActiveDraft();
  if (!files[path]) {
    files[path] = { title: basename(path), language: "text", content: "" };
  }
  state.activePath = path;
  if (!state.openPaths.includes(path)) state.openPaths.push(path);
  els.activePath.textContent = `${path}${state.dirtyPaths.has(path) ? " *" : ""}`;
  setEditorValue(files[path].draft ?? files[path].content);
  els.editor.readOnly = false;
  document.querySelectorAll(".file").forEach(button => {
    button.classList.toggle("active", button.dataset.path === path);
  });
  updateEditorState();
}

function parseTinyFsTree(output) {
  const clean = withoutPrompts(output);
  const dirs = new Set(["/"]);
  const filesSeen = [];
  for (const line of clean.split("\n")) {
    const dirMatch = line.match(/^\s*\[DIR\]\s+(\S+)/);
    if (dirMatch) {
      dirs.add(dirMatch[1]);
      continue;
    }
    const fileMatch = line.match(/^\s*\[FILE\]\s+(\S+)\s+size=(\d+)\s+blocks=(\d+)/);
    if (fileMatch) {
      const path = fileMatch[1];
      dirs.add(dirname(path));
      filesSeen.push({ path, size: Number(fileMatch[2]), blocks: Number(fileMatch[3]) });
    }
  }
  return { dirs: [...dirs].sort(), files: filesSeen.sort((a, b) => a.path.localeCompare(b.path)) };
}

function buildTreeModel(snapshot) {
  const root = { path: "/", name: "/", dirs: new Map(), files: [] };
  const ensureDir = path => {
    if (path === "/") return root;
    const parts = path.split("/").filter(Boolean);
    let node = root;
    let current = "";
    for (const part of parts) {
      current += `/${part}`;
      if (!node.dirs.has(part)) {
        node.dirs.set(part, { path: current, name: part, dirs: new Map(), files: [] });
      }
      node = node.dirs.get(part);
    }
    return node;
  };

  snapshot.dirs.forEach(ensureDir);
  snapshot.files.forEach(file => {
    const parent = ensureDir(dirname(file.path));
    parent.files.push(file);
  });
  return root;
}

function renderTreeNode(node, level = 0) {
  const folders = [...node.dirs.values()].sort((a, b) => a.name.localeCompare(b.name));
  const fileRows = [...node.files].sort((a, b) => basename(a.path).localeCompare(basename(b.path)));
  const label = node.path === "/" ? "/" : node.name;
  const expanded = state.expandedDirs.has(node.path);
  const openClass = expanded ? " open" : "";
  const rows = [`<button class="folder${openClass}" type="button" style="--level:${level}" data-path="${escapeHtml(node.path)}" aria-expanded="${expanded ? "true" : "false"}"><span class="twisty" aria-hidden="true"></span><span>${escapeHtml(label)}</span></button>`];
  if (expanded) {
    folders.forEach(child => rows.push(renderTreeNode(child, level + 1)));
    fileRows.forEach(file => {
      const active = file.path === state.activePath ? " active" : "";
      const loading = file.path === state.loadingPath ? " loading" : "";
      rows.push(`<button class="file${active}${loading}" type="button" style="--level:${level + 1}" data-path="${escapeHtml(file.path)}" title="${escapeHtml(file.path)}"><span class="file-icon" aria-hidden="true"></span><span class="file-name">${escapeHtml(basename(file.path))}</span><span class="file-size">${file.size}B</span></button>`);
    });
  }
  return rows.join("");
}

function renderExplorerFromSnapshot(snapshot) {
  state.fsTree = snapshot;
  const dirs = new Set(snapshot.dirs);
  state.expandedDirs = new Set([...state.expandedDirs].filter(path => dirs.has(path)));
  state.expandedDirs.add("/");
  els.fileTree.innerHTML = renderTreeNode(buildTreeModel(snapshot));
}

async function saveActiveFile() {
  if (!state.activePath || !files[state.activePath]) return;
  const path = state.activePath;
  const nextContent = els.editor.value;
  const content = encodeShellText(els.editor.value.replace(/\n$/, ""));
  if (els.saveFile) els.saveFile.disabled = true;
  updateEditorState("saving");
  const output = await sendMiniOsCommand(`put ${path} ${content}`, { terminal: false, waitSeconds: 0.45 });
  if (output) {
    files[path].content = nextContent;
    delete files[path].draft;
    state.dirtyPaths.delete(path);
    els.activePath.textContent = path;
    updateEditorState("saved");
    window.setTimeout(() => {
      if (state.activePath === path) updateEditorState();
    }, 900);
    await refreshExplorer();
  } else {
    updateEditorState("failed");
  }
  if (els.saveFile) els.saveFile.disabled = false;
}

function scrollTerminal() {
  els.terminalScreen.scrollTop = els.terminalScreen.scrollHeight;
}

function appendTerminal(text) {
  if (!text) return;
  els.terminalOutput.textContent += text;
  scrollTerminal();
}

function appendCommandLine(command) {
  appendTerminal(`${els.promptLabel.textContent} ${command}\n`);
}

function appendBackendOutput(text) {
  if (!text) return;
  if (text.includes("\x1b[2J") || text.includes("\u001b[2J")) {
    els.terminalOutput.textContent = "";
  }
  updatePrompt(text);
  const rendered = text
    .replace(/\x1b\[2J\x1b\[H/g, "")
    .replace(/(^|\n)minios:(?:.*?:)?\d+\$\s?/g, (_match, prefix) => prefix);
  appendTerminal(rendered);
}

function shouldRefreshExplorer(command) {
  const name = command.trim().split(/\s+/)[0] || "";
  return ["mkdir", "rmdir", "touch", "put", "write", "rm", "loadfs", "ls", "tree", "status"].includes(name);
}

function allFsPaths() {
  return [
    ...state.fsTree.dirs.filter(path => path !== "/").map(path => `${path}/`),
    ...state.fsTree.files.map(file => file.path)
  ];
}

function shellPathPrefix(input) {
  const parts = input.split(/\s+/);
  return parts.length <= 1 ? "" : parts[parts.length - 1];
}

function displayPathForCompletion(path, prefix) {
  if (prefix.startsWith("/")) return path;
  if (state.shellCwd !== "/" && path.startsWith(`${state.shellCwd}/`)) {
    return path.slice(state.shellCwd.length + 1);
  }
  return path.startsWith("/") ? path.slice(1) : path;
}

function encodeShellText(text) {
  return text
    .replace(/\\/g, "\\\\")
    .replace(/\t/g, "\\t")
    .replace(/\n/g, "\\n");
}

function relativePath(path) {
  if (state.shellCwd !== "/" && path.startsWith(`${state.shellCwd}/`)) {
    return path.slice(state.shellCwd.length + 1);
  }
  return path.startsWith("/") ? path.slice(1) : path;
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
  } catch {
    appendTerminal(`\n[workbench] ${text}\n`);
  }
}

function closeContextMenu() {
  els.contextMenu.hidden = true;
}

function showContextMenu(event, target) {
  const path = target.dataset.path || "/";
  const isFile = target.classList.contains("file");
  const actions = isFile ? [
    `<button type="button" data-action="open">Open</button>`,
    `<button type="button" data-action="copy-name">Copy Name</button>`,
    `<button type="button" data-action="copy-abs">Copy Absolute Path</button>`,
    `<button type="button" data-action="copy-rel">Copy Relative Path</button>`,
    `<button type="button" data-action="delete">Delete</button>`
  ] : [
    `<button type="button" data-action="new-file">New File</button>`,
    `<button type="button" data-action="new-folder">New Folder</button>`,
    `<button type="button" data-action="cd">cd Here</button>`,
    `<button type="button" data-action="refresh">Refresh</button>`,
    `<button type="button" data-action="copy-name">Copy Name</button>`,
    `<button type="button" data-action="copy-abs">Copy Absolute Path</button>`,
    `<button type="button" data-action="copy-rel">Copy Relative Path</button>`,
    path === "/" ? "" : `<button type="button" data-action="delete">Delete</button>`
  ];
  event.preventDefault();
  els.contextMenu.innerHTML = actions.join("");
  els.contextMenu.dataset.path = path;
  els.contextMenu.dataset.kind = isFile ? "file" : "dir";
  els.contextMenu.style.left = `${Math.min(event.clientX, window.innerWidth - 210)}px`;
  els.contextMenu.style.top = `${Math.min(event.clientY, window.innerHeight - 124)}px`;
  els.contextMenu.hidden = false;
}

function childPath(parent, name) {
  const clean = name.trim();
  if (!/^[A-Za-z0-9._-]+$/.test(clean)) return "";
  return parent === "/" ? `/${clean}` : `${parent}/${clean}`;
}

async function createExplorerChild(parent, kind) {
  const fallback = kind === "folder" ? "new-folder" : "new-file.txt";
  const name = window.prompt(kind === "folder" ? "Folder name" : "File name", fallback);
  if (name == null) return;
  const path = childPath(parent, name);
  if (!path) {
    appendTerminal("[workbench] invalid name; use letters, numbers, dot, dash or underscore\n");
    return;
  }
  state.expandedDirs.add(parent);
  await runCommand(kind === "folder" ? `mkdir ${path}` : `touch ${path}`);
  await refreshExplorer();
  if (kind !== "folder") await openTinyFsFile(path);
}

async function deleteExplorerPath(path, kind) {
  if (!path || path === "/") return;
  if (!window.confirm(`Delete ${path}?`)) return;
  await runCommand(kind === "file" ? `rm ${path}` : `rmdir ${path}`);
  if (kind === "file") {
    delete files[path];
    state.dirtyPaths.delete(path);
    closeTab(path, { force: true });
  }
  await refreshExplorer();
}

function completionCandidates(input) {
  const trimmedLeft = input.replace(/^\s+/, "");
  const parts = trimmedLeft.split(/\s+/);
  if (parts.length <= 1 && !input.endsWith(" ")) {
    return shellCommands.filter(command => command.startsWith(parts[0] || ""));
  }
  const prefix = shellPathPrefix(input);
  return allFsPaths()
    .map(path => displayPathForCompletion(path, prefix))
    .filter(path => path.startsWith(prefix));
}

function applyCompletion(input, completion) {
  const commandOnly = input.trim().indexOf(" ") < 0 && !input.endsWith(" ");
  if (commandOnly) return completion;
  const prefix = shellPathPrefix(input);
  return input.slice(0, input.length - prefix.length) + completion;
}

function completeInput() {
  const input = els.commandInput.value;
  const candidates = [...new Set(completionCandidates(input))].sort();
  const now = Date.now();
  if (candidates.length === 0) return;
  if (candidates.length === 1) {
    els.commandInput.value = applyCompletion(input, candidates[0]);
    return;
  }
  const prefix = input.trim();
  if (prefix === state.lastTabPrefix && now - state.lastTabAt < 1400) {
    appendTerminal(`${els.promptLabel.textContent} ${input}\n${candidates.join("    ")}\n`);
    scrollTerminal();
  }
  state.lastTabPrefix = prefix;
  state.lastTabAt = now;
}

function renderCommandList() {
  const query = els.commandSearch.value.trim().toLowerCase();
  const matches = commandSnippets.filter(item => {
    const haystack = `${item.category} ${item.command}`.toLowerCase();
    return !query || haystack.includes(query);
  });
  els.commandList.innerHTML = matches.map(item => `
    <button class="command-row" type="button" data-command="${escapeHtml(item.command)}">
      <span class="command-category">${escapeHtml(item.category)}</span>
      <span class="command-code">${escapeHtml(item.command)}</span>
    </button>
  `).join("");
}

function openCommandPalette(filter = "") {
  els.commandPalette.hidden = false;
  els.commandSearch.value = filter;
  renderCommandList();
  window.setTimeout(() => els.commandSearch.focus(), 0);
}

function closeCommandPalette() {
  els.commandPalette.hidden = true;
  els.commandInput.focus();
}

function setConnection(online, label) {
  state.online = online;
  els.connectionStatus.textContent = label;
  els.connectionStatus.classList.toggle("offline", !online);
}

function updatePrompt(text) {
  const matches = [...text.matchAll(/minios:(?:(.*?):)?(\d+)\$\s?/g)];
  if (matches.length === 0) return;
  const last = matches[matches.length - 1];
  const cwd = last[1] || state.shellCwd || "/";
  const tick = last[2].padStart(3, "0");
  state.shellCwd = cwd;
  els.promptLabel.textContent = `minios:${cwd}:${tick}$`;
  els.kernelTick.textContent = tick;
  els.statusTick.textContent = `tick ${tick}`;
  els.statusCwd.textContent = `MiniOS ${cwd}`;
  els.workspacePath.textContent = `MiniOS ${cwd}`;
}

function withoutPrompts(text) {
  return text
    .replace(/^minios:(?:.*?:)?\d+\$\s?/gm, "")
    .replace(/\x1b\[2J\x1b\[H/g, "")
    .replace(/\n\[workbench\].*$/gm, "")
    .trim();
}

function sectionBetween(text, start, end) {
  const startIndex = text.indexOf(start);
  if (startIndex < 0) return "";
  const afterStart = text.slice(startIndex);
  if (!end) return afterStart.trim();
  const endIndex = afterStart.indexOf(end);
  return (endIndex >= 0 ? afterStart.slice(0, endIndex) : afterStart).trim();
}

function keepLines(text, count) {
  const lines = withoutPrompts(text).split("\n").filter(line => line.trim() !== "");
  return lines.slice(-count).join("\n");
}

function formatFdCapture(fdState, detail = "") {
  if (!fdState || fdState.last_fd == null) return detail || "no open fd";
  const openFds = Array.isArray(fdState.open_fds) ? fdState.open_fds : [];
  const current = openFds.find(item => item.fd === fdState.last_fd);
  const alias = current
    ? `$fd = ${fdState.last_fd}  ${current.mode || "-"}  ${current.path || ""}`
    : `$fd = ${fdState.last_fd}`;
  if (detail) return `${alias}\n\n${detail}`;
  const rows = openFds.map(item => {
    const offset = item.offset == null ? "-" : item.offset;
    return `${item.fd}  ${item.mode || "-"}  off=${offset}  ${item.state || "open"}  ${item.path || ""}`;
  });
  return [alias, ...rows].join("\n");
}

function updateInspectorFromOutput(command, output, payload = {}) {
  if (payload.pid) els.kernelPid.textContent = String(payload.pid);
  if (payload.started_at) {
    const date = new Date(payload.started_at);
    els.kernelStarted.textContent = Number.isNaN(date.getTime())
      ? payload.started_at
      : date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  }
  if (payload.cwd && !state.shellCwd) els.workspacePath.textContent = payload.cwd;
  if (payload.fd_state) els.fdSummary.textContent = formatFdCapture(payload.fd_state);

  const tickLine = output.match(/Kernel tick=(\d+).*scheduler=([A-Z]+)/);
  if (tickLine) {
    els.kernelTick.textContent = tickLine[1].padStart(3, "0");
    els.kernelScheduler.textContent = tickLine[2];
    els.statusTick.textContent = `tick ${tickLine[1].padStart(3, "0")}`;
  }

  if (/sched mlfq/i.test(output)) els.kernelScheduler.textContent = "MLFQ";
  if (/scheduler=RR/i.test(output)) els.kernelScheduler.textContent = "RR";
  if (/scheduler=FCFS/i.test(output)) els.kernelScheduler.textContent = "FCFS";
  if (/scheduler=SJF/i.test(output)) els.kernelScheduler.textContent = "SJF";
  if (/scheduler=PRIORITY/i.test(output)) els.kernelScheduler.textContent = "PRIORITY";
  els.statusScheduler.textContent = els.kernelScheduler.textContent;

  const clean = withoutPrompts(output);
  const commandName = command.trim().split(/\s+/)[0] || "";
  if (commandName === "status" || output.includes("Kernel tick=")) {
    const firstBlock = sectionBetween(clean, "Kernel tick=", "Kernel VM state:");
    if (firstBlock) els.statusSummary.textContent = firstBlock;

    const vmBlock = sectionBetween(clean, "Kernel VM state:", "Kernel open file table:");
    if (vmBlock) els.vmSummary.textContent = vmBlock;

    const fdBlock = sectionBetween(clean, "Kernel open file table:", "Kernel memory state:");
    if (fdBlock) els.fdSummary.textContent = formatFdCapture(payload.fd_state, fdBlock);

    const fsBlock = sectionBetween(clean, "Kernel file state:", "");
    if (fsBlock) els.treeSummary.textContent = fsBlock;
  }

  if (commandName === "tree" && clean) {
    const snapshot = parseTinyFsTree(output);
    renderExplorerFromSnapshot(snapshot);
    els.treeSummary.textContent = clean;
  }
  if (commandName === "vm" && clean) {
    els.vmSummary.textContent = clean;
  }
  if ((commandName === "fd" || commandName === "fds") && clean) {
    els.fdSummary.textContent = formatFdCapture(payload.fd_state, clean);
  }
  if (commandName === "dmesg" && clean) {
    els.dmesgSummary.textContent = keepLines(clean, 12);
    files["/var/log/dmesg.log"].content = els.dmesgSummary.textContent + "\n";
    if (state.activePath === "/var/log/dmesg.log") els.editor.value = files[state.activePath].content;
  }
}

async function requestJson(path, options = {}) {
  const response = await fetch(path, {
    cache: "no-store",
    headers: { "Content-Type": "application/json" },
    ...options
  });
  if (!response.ok) {
    const text = await response.text();
    try {
      const payload = JSON.parse(text);
      throw new Error(payload.error || text || `HTTP ${response.status}`);
    } catch (error) {
      if (error instanceof SyntaxError) throw new Error(text || `HTTP ${response.status}`);
      throw error;
    }
  }
  return response.json();
}

async function sendMiniOsCommand(command, options = {}) {
  const terminal = options.terminal !== false;
  const trimmed = command.trim();
  if (!trimmed || state.sending) return "";
  state.sending = true;
  if (terminal) {
    state.commandHistory.push(trimmed);
    state.historyIndex = state.commandHistory.length;
    appendCommandLine(trimmed);
  }
  try {
    const payload = await requestJson("/api/command", {
      method: "POST",
      body: JSON.stringify({ command: trimmed, silent: !terminal, wait_seconds: options.waitSeconds })
    });
    setConnection(payload.running, payload.running ? "connected" : "stopped");
    if (terminal) {
      appendBackendOutput(payload.output || "");
    } else {
      updatePrompt(payload.output || "");
    }
    updateInspectorFromOutput(trimmed, payload.output || "", payload);
    return payload.output || "";
  } catch (error) {
    setConnection(false, "offline");
    if (terminal) {
      appendTerminal(`[workbench] command failed: ${error.message}\n`);
    }
    return "";
  } finally {
    state.sending = false;
    if (terminal) els.commandInput.focus();
  }
}

async function refreshExplorer() {
  if (state.refreshingTree || state.sending) return;
  state.refreshingTree = true;
  try {
    const output = await sendMiniOsCommand("tree", { terminal: false });
    if (output) {
      const clean = withoutPrompts(output);
      els.treeSummary.textContent = clean;
      renderExplorerFromSnapshot(parseTinyFsTree(output));
    }
  } finally {
    state.refreshingTree = false;
  }
}

function fsHasPath(path) {
  return state.fsTree.dirs.includes(path) || state.fsTree.files.some(file => file.path === path);
}

async function bootstrapWorkspaceDocs() {
  if (fsHasPath("/workspace/README.md")) return;
  await sendMiniOsCommand("mkdir /workspace", { terminal: false, waitSeconds: 0.3 });
  await sendMiniOsCommand("mkdir /workspace/src", { terminal: false, waitSeconds: 0.3 });
  await sendMiniOsCommand("mkdir /workspace/logs", { terminal: false, waitSeconds: 0.3 });
  await sendMiniOsCommand("mkdir /workspace/docs", { terminal: false, waitSeconds: 0.3 });
  await sendMiniOsCommand(`put /workspace/README.md ${encodeShellText(workspaceReadmeContent)}`, {
    terminal: false,
    waitSeconds: 0.45
  });
  await sendMiniOsCommand(`put /workspace/docs/files.md ${encodeShellText(workspaceFilesDocContent)}`, {
    terminal: false,
    waitSeconds: 0.45
  });
  await sendMiniOsCommand(`put /workspace/docs/process.md ${encodeShellText(workspaceProcessDocContent)}`, {
    terminal: false,
    waitSeconds: 0.45
  });
  await sendMiniOsCommand(`put /workspace/docs/vm.md ${encodeShellText(workspaceVmDocContent)}`, {
    terminal: false,
    waitSeconds: 0.45
  });
  await sendMiniOsCommand(`put /workspace/docs/timing.md ${encodeShellText(workspaceTimingDocContent)}`, {
    terminal: false,
    waitSeconds: 0.45
  });
}

async function openTinyFsFile(path) {
  state.loadingPath = path;
  renderExplorerFromSnapshot(state.fsTree);
  const output = await sendMiniOsCommand(`cat ${path}`, { terminal: false, waitSeconds: 0.45 });
  state.loadingPath = "";
  renderExplorerFromSnapshot(state.fsTree);
  const clean = withoutPrompts(output);
  if (!output || /read failed|no such|not a file|usage:/i.test(clean)) {
    appendTerminal(`[workbench] open failed: ${path}\n`);
    return;
  }
  files[path] = { title: basename(path), language: "text", content: clean ? `${clean}\n` : "" };
  state.dirtyPaths.delete(path);
  setActiveFile(path);
}

async function refreshKernelInspector() {
  await sendMiniOsCommand("status", { terminal: false, waitSeconds: 0.45 });
  await sendMiniOsCommand("vm", { terminal: false, waitSeconds: 0.3 });
  await sendMiniOsCommand("dmesg", { terminal: false, waitSeconds: 0.3 });
}

async function pollState() {
  if (state.sending) return;
  try {
    const payload = await requestJson("/api/state");
    setConnection(payload.running ? true : false, payload.running ? "connected" : "stopped");
    if (payload.output) {
      appendBackendOutput(payload.output);
      updateInspectorFromOutput("", payload.output, payload);
    } else {
      updateInspectorFromOutput("", "", payload);
    }
  } catch (error) {
    setConnection(false, "offline");
    if (!els.terminalOutput.dataset.offline) {
      appendTerminal(`[workbench] backend unavailable: ${error.message}\n[workbench] start it with: make workbench\n`);
      els.terminalOutput.dataset.offline = "1";
    }
  }
}

async function runCommand(command, options = {}) {
  const trimmed = command.trim();
  if (!trimmed) return;
  if (state.sending) {
    appendTerminal("[workbench] MiniOS is busy; retry when the prompt returns.\n");
    return;
  }
  await sendMiniOsCommand(trimmed, { terminal: true, waitSeconds: options.waitSeconds });
  if (shouldRefreshExplorer(trimmed) && trimmed.split(/\s+/)[0] !== "tree") {
    await refreshExplorer();
  }
}

function setupResizer() {
  let startY = 0;
  let startHeight = 360;
  els.terminalResizer.addEventListener("pointerdown", event => {
    startY = event.clientY;
    startHeight = parseInt(getComputedStyle(els.workspace).getPropertyValue("--terminal-height"), 10) || 360;
    els.terminalResizer.classList.add("dragging");
    document.body.classList.add("workbench-resizing");
    els.terminalResizer.setPointerCapture(event.pointerId);
    event.preventDefault();
  });
  els.terminalResizer.addEventListener("pointermove", event => {
    if (!els.terminalResizer.hasPointerCapture(event.pointerId)) return;
    const workspaceHeight = els.workspace.getBoundingClientRect().height;
    const maxHeight = Math.max(320, workspaceHeight - 150);
    const next = Math.max(180, Math.min(maxHeight, startHeight - (event.clientY - startY)));
    els.workspace.style.setProperty("--terminal-height", `${next}px`);
  });
  els.terminalResizer.addEventListener("pointerup", event => {
    if (els.terminalResizer.hasPointerCapture(event.pointerId)) {
      els.terminalResizer.releasePointerCapture(event.pointerId);
    }
    els.terminalResizer.classList.remove("dragging");
    document.body.classList.remove("workbench-resizing");
  });
  els.terminalResizer.addEventListener("pointercancel", event => {
    if (els.terminalResizer.hasPointerCapture(event.pointerId)) {
      els.terminalResizer.releasePointerCapture(event.pointerId);
    }
    els.terminalResizer.classList.remove("dragging");
    document.body.classList.remove("workbench-resizing");
  });
}

function closeTab(path, options = {}) {
  const target = path || state.activePath;
  if (!target) return;
  if (state.dirtyPaths.has(target) && !options.force && !window.confirm(`Close unsaved ${target}?`)) return;
  const index = state.openPaths.indexOf(target);
  state.openPaths = state.openPaths.filter(openPath => openPath !== target);
  if (options.force) state.dirtyPaths.delete(target);
  if (state.activePath !== target) {
    renderTabs();
    return;
  }
  const next = state.openPaths[Math.max(0, index - 1)] || state.openPaths[0];
  if (next) {
    state.activePath = "";
    setActiveFile(next);
  } else {
    showEmptyEditor();
  }
}

function setActivityView(view) {
  document.querySelectorAll(".activitybar button").forEach(button => {
    button.classList.toggle("active", button.dataset.view === view);
  });
  document.body.dataset.workbenchView = view;
  if (view === "commands") {
    openCommandPalette();
  } else if (view === "terminal") {
    els.commandInput.focus();
  } else if (view === "explorer") {
    els.fileTree.focus();
  } else if (view === "kernel") {
    els.inspector?.focus?.();
  }
}

function toggleTerminalMaximized() {
  state.terminalMaximized = !state.terminalMaximized;
  els.workspace.classList.toggle("terminal-maximized", state.terminalMaximized);
  els.toggleTerminalMax.textContent = state.terminalMaximized ? "Restore" : "Max";
  scrollTerminal();
}

function wireEvents() {
  els.fileTree.addEventListener("click", event => {
    const folder = event.target.closest(".folder");
    if (folder) {
      const path = folder.dataset.path;
      if (state.expandedDirs.has(path)) {
        if (path !== "/") state.expandedDirs.delete(path);
      } else {
        state.expandedDirs.add(path);
      }
      renderExplorerFromSnapshot(state.fsTree);
      return;
    }
    const button = event.target.closest(".file");
    if (!button) return;
    openTinyFsFile(button.dataset.path);
  });
  els.fileTree.addEventListener("contextmenu", event => {
    const target = event.target.closest(".file, .folder");
    if (!target) return;
    showContextMenu(event, target);
  });
  els.contextMenu.addEventListener("click", event => {
    const button = event.target.closest("button[data-action]");
    const path = els.contextMenu.dataset.path;
    const kind = els.contextMenu.dataset.kind;
    if (!button || !path) return;
    closeContextMenu();
    if (button.dataset.action === "open") openTinyFsFile(path);
    if (button.dataset.action === "cd") runCommand(`cd ${path}`);
    if (button.dataset.action === "new-file") createExplorerChild(path, "file");
    if (button.dataset.action === "new-folder") createExplorerChild(path, "folder");
    if (button.dataset.action === "refresh") refreshExplorer();
    if (button.dataset.action === "delete") deleteExplorerPath(path, kind);
    if (button.dataset.action === "copy-name") copyText(basename(path));
    if (button.dataset.action === "copy-abs") copyText(path);
    if (button.dataset.action === "copy-rel") copyText(relativePath(path));
  });
  document.addEventListener("click", event => {
    if (!event.target.closest("#contextMenu")) closeContextMenu();
  });
  els.tabs.addEventListener("click", event => {
    const close = event.target.closest("[data-close]");
    if (close) {
      event.stopPropagation();
      closeTab(close.dataset.close);
      return;
    }
    const tab = event.target.closest(".tab");
    if (!tab) return;
    setActiveFile(tab.dataset.path);
  });
  if (els.saveFile) els.saveFile.addEventListener("click", saveActiveFile);
  if (els.copyPath) els.copyPath.addEventListener("click", () => copyText(state.activePath));
  els.openCommandPalette.addEventListener("click", () => openCommandPalette());
  els.openCommandPaletteTop.addEventListener("click", () => openCommandPalette());
  els.clearTerminal.addEventListener("click", () => {
    els.terminalOutput.textContent = "";
    delete els.terminalOutput.dataset.offline;
  });
  els.toggleTerminalMax.addEventListener("click", toggleTerminalMaximized);
  document.querySelector(".activitybar").addEventListener("click", event => {
    const button = event.target.closest("button[data-view]");
    if (!button) return;
    setActivityView(button.dataset.view);
  });
  els.moduleActions.addEventListener("click", event => {
    const button = event.target.closest("[data-filter]");
    if (!button) return;
    openCommandPalette(button.dataset.filter);
  });
  els.refreshTree.addEventListener("click", refreshExplorer);
  els.restartMiniOS.addEventListener("click", async () => {
    els.terminalOutput.textContent = "";
    try {
      const payload = await requestJson("/api/restart", { method: "POST", body: "{}" });
      setConnection(payload.running, payload.running ? "connected" : "stopped");
      appendBackendOutput(payload.output || "");
      updateInspectorFromOutput("", payload.output || "", payload);
    } catch (error) {
      setConnection(false, "offline");
      appendTerminal(`[workbench] restart failed: ${error.message}\n`);
    }
  });
  els.terminalForm.addEventListener("submit", event => {
    event.preventDefault();
    const command = els.commandInput.value;
    if (!command.trim()) return;
    if (state.sending) {
      appendTerminal("[workbench] MiniOS is busy; retry when the prompt returns.\n");
      return;
    }
    els.commandInput.value = "";
    runCommand(command);
  });
  els.terminalScreen.addEventListener("click", event => {
    if (window.getSelection().toString()) return;
    if (event.target === els.terminalScreen || event.target === els.terminalForm || event.target === els.promptLabel) {
      els.commandInput.focus();
    }
  });
  els.commandSearch.addEventListener("input", renderCommandList);
  els.commandSearch.addEventListener("keydown", event => {
    if (event.key === "Escape") {
      event.preventDefault();
      closeCommandPalette();
    }
    if (event.key === "Enter") {
      const first = els.commandList.querySelector("[data-command]");
      if (first) {
        event.preventDefault();
        closeCommandPalette();
        runCommand(first.dataset.command);
      }
    }
  });
  els.closeCommandPalette.addEventListener("click", closeCommandPalette);
  els.commandPalette.addEventListener("click", event => {
    if (event.target === els.commandPalette) closeCommandPalette();
  });
  els.commandList.addEventListener("click", event => {
    const button = event.target.closest("[data-command]");
    if (!button) return;
    closeCommandPalette();
    runCommand(button.dataset.command);
  });
  els.commandInput.addEventListener("keydown", event => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
      event.preventDefault();
      saveActiveFile();
      return;
    }
    if (event.key === "Tab") {
      event.preventDefault();
      completeInput();
      return;
    }
    if (event.key === "ArrowUp") {
      event.preventDefault();
      state.historyIndex = Math.max(0, state.historyIndex - 1);
      els.commandInput.value = state.commandHistory[state.historyIndex] || "";
    }
    if (event.key === "ArrowDown") {
      event.preventDefault();
      state.historyIndex = Math.min(state.commandHistory.length, state.historyIndex + 1);
      els.commandInput.value = state.commandHistory[state.historyIndex] || "";
    }
  });
  els.editor.addEventListener("keydown", event => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
      event.preventDefault();
      saveActiveFile();
    }
  });
  els.editor.addEventListener("input", () => {
    if (!state.activePath || !files[state.activePath]) return;
    files[state.activePath].draft = els.editor.value;
    state.dirtyPaths.add(state.activePath);
    els.activePath.textContent = `${state.activePath} *`;
    updateLineNumbers();
    updateEditorState();
  });
  els.editor.addEventListener("scroll", () => {
    els.lineNumbers.scrollTop = els.editor.scrollTop;
  });
  document.addEventListener("keydown", event => {
    const key = event.key.toLowerCase();
    if ((event.ctrlKey || event.metaKey) && key === "p") {
      event.preventDefault();
      openCommandPalette();
    }
    if ((event.ctrlKey || event.metaKey) && key === "w" && state.activePath) {
      event.preventDefault();
      closeTab(state.activePath);
    }
    if (event.ctrlKey && event.key === "`") {
      event.preventDefault();
      setActivityView("terminal");
    }
    if (event.key === "Escape" && !els.commandPalette.hidden) {
      closeCommandPalette();
    }
  });
}

async function boot() {
  renderTabs();
  showEmptyEditor();
  setupResizer();
  wireEvents();
  renderCommandList();
  await pollState();
  await refreshExplorer();
  await bootstrapWorkspaceDocs();
  await refreshExplorer();
  state.expandedDirs.add("/workspace");
  state.expandedDirs.add("/workspace/docs");
  renderExplorerFromSnapshot(state.fsTree);
  if (fsHasPath("/workspace/README.md")) await openTinyFsFile("/workspace/README.md");
  await refreshKernelInspector();
  window.setInterval(pollState, 900);
}

boot();
