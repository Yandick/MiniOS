# data

This directory contains deterministic input data for MiniOS demos, shell tours, benchmark runs, and acceptance evidence generation.

## File Groups

| Pattern | Purpose |
| --- | --- |
| `tour_script.txt` | Default command timeline used by `make tour`. |
| `os_workflow_demo.txt` / `os_workflow_full_demo.txt` | Longer scripted workflows for cinematic and full-demo runs. |
| `evidence_*.txt` | Focused command scripts used by `make evidence` to produce reproducible logs. |
| `*.csv` | Structured inputs for process scheduling, partition memory, TinyFS, and real-time scheduling experiments. |
| `page_refs.txt` | Page-reference sequence for FIFO/LRU virtual-memory demos. |
| `workbench_*.txt` | Browser Workbench story and teacher-test command sets. |
| `shell_all_commands.txt` | Coverage-oriented command list for shell feature checks. |

## Common Uses

```bash
make tour
build/os_project tour --script data/tour_script.txt
build/os_project tour --script data/evidence_tracebench.txt
make evidence
```

## Maintenance Notes

- Keep scripts deterministic so generated logs remain comparable across runs.
- Prefer short, representative command sequences over long manual transcripts.
- Generated evidence belongs in `build/evidence/`, not in this directory.
- Use UTF-8 text files and keep CSV headers stable because the C loaders expect specific columns.
