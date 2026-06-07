# src

This directory contains the MiniOS C implementation.

## Modules

| File | Responsibility |
| --- | --- |
| `main.c` | Shell entry point, tour runner, command parser, and user-facing command output. |
| `kernel.c` | Integrated MiniKernel state: PCB table, scheduling ticks, memory ownership, VM state, TinyFS integration, fd table, and logs. |
| `scheduling.c` | Batch CPU scheduling algorithms and comparison reports. |
| `memory.c` | FIFO/LRU page replacement and first-fit dynamic partition management. |
| `tinyfs.c` | In-memory TinyFS directories, files, persistence, and image validation. |
| `sync_demo.c` | Producer-consumer, readers-writers, and dining-philosophers pthread demos. |
| `realtime.c` | EDF and RMS real-time scheduling simulation. |
| `benchmark.c` | Tracebench, benchmark subsets, autotuning, CSV export, and Markdown performance reports. |
| `utils.c` | Shared string, parsing, and file utility helpers. |

## Build Targets

```bash
make all
make test
make tour
```

## Maintenance Notes

- Keep public declarations in `include/os_project.h`.
- Keep module behavior deterministic where it feeds tests or demo evidence.
- Add focused assertions in `tests/test_core.c` when changing scheduling, memory, TinyFS, fd, or kernel behavior.
- Avoid broad rewrites in `main.c`; shell commands should stay thin wrappers around module functions.
