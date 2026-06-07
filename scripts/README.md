# scripts

This directory contains Python helper scripts used by the browser demo and local development workflow.

## Scripts

| Script | Purpose |
| --- | --- |
| `export_demo_assets.py` | Converts verification logs from `build/evidence/` into browser-readable assets under `demo/assets/`. |
| `workbench_server.py` | Serves `demo/` and bridges Workbench API requests to a real `build/os_project tour --interactive` subprocess. |

## Common Commands

```bash
python3 scripts/export_demo_assets.py
python3 scripts/workbench_server.py --port 8001 --start-path /workbench.html
make evidence
make workbench
```

## Maintenance Notes

- Keep scripts compatible with Linux and macOS POSIX workflows.
- Prefer deterministic parsing so generated demo assets are stable in reviews.
- Keep browser API responses small and explicit; Workbench polls these endpoints frequently.
- `__pycache__/` and `*.pyc` files are generated locally and ignored by git.
