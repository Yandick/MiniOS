# demo/assets

This directory stores static and generated assets used by the MiniOS browser pages.

## Key Files

| File | Purpose |
| --- | --- |
| `minios.css` | Shared styles for the home and story pages. |
| `workbench.css` | Styles for the interactive Workbench UI. |
| `workbench.js` | Browser-side Workbench state, terminal UI, command helpers, and API polling. |
| `main_page.png` | Screenshot used by the root README preview. |
| `evidence.json` | Browser-readable summary generated from `build/evidence/` logs. |
| `story_full.json` | Structured command/story data for the full demo page. |
| `full_demo_tail.txt` | Compact tail of the full demo log for display. |

## Generated Assets

These files are refreshed by:

```bash
make evidence
```

Internally, `make evidence` runs `scripts/export_demo_assets.py` after producing logs under `build/evidence/`.

## Maintenance Notes

- Edit CSS and JavaScript directly when changing browser behavior.
- Do not hand-edit generated JSON or log-tail files unless you also update the generation script.
- Keep asset paths relative to `demo/` so the Python static server and direct browser previews both work.
