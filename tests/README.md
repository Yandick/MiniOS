# tests

This directory contains the C test suite for MiniOS core behavior.

## Files

| File | Purpose |
| --- | --- |
| `test_core.c` | Assertion-based unit and integration tests for scheduling, memory, TinyFS, fd behavior, kernel state, benchmarks, and image validation. |

## Run

```bash
make test
```

`make test` builds `build/test_core` and executes it. A successful run exits with status `0`.

## Maintenance Notes

- Keep tests deterministic and self-contained.
- Prefer focused assertions over transcript-style output checks.
- Add regression coverage when changing any public function in `include/os_project.h`.
- Temporary artifacts should be written under `build/` or cleaned up inside the test.
