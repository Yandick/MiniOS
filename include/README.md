# include

This directory contains the public C interface for MiniOS.

## Files

| File | Purpose |
| --- | --- |
| `os_project.h` | Shared structs, constants, and function declarations for all MiniOS modules. |

## API Areas

`os_project.h` groups the project surface into:

- Utility helpers
- Batch scheduling reports and algorithms
- Real-time EDF/RMS scheduling
- Page replacement and first-fit memory management
- Synchronization demos
- TinyFS operations
- State benchmarking and performance export
- Integrated MiniKernel and shell-facing syscall helpers

## Maintenance Notes

- Keep this header implementation-agnostic; module internals should stay in `src/`.
- Update declarations here before wiring new module functions into `main.c` or tests.
- Preserve fixed-size limits carefully because several structs are stored, printed, or serialized directly.
- Run `make test` after changing public structs or function signatures.
