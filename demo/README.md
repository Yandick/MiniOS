# demo

This directory contains the browser-facing MiniOS pages. The pages present the project overview, the full scripted story, and the live Workbench that bridges browser commands to the real MiniOS shell.

## Pages

| File | Role |
| --- | --- |
| `index.html` | Project home page and control center. |
| `workbench.html` | Interactive browser Workbench backed by `build/os_project tour --interactive`. |
| `minios_story.html` | Full visual demo page driven by scripted evidence and generated story assets. |
| `assets/` | CSS, JavaScript, screenshots, and generated JSON/text used by the pages. |

## Run Locally

```bash
make home
make workbench
make demo
```

The static pages can be opened directly for inspection, but `workbench.html` needs the Python bridge started by `make workbench` before browser commands can reach the real MiniOS process.

## Maintenance Notes

- Keep page copy aligned with the root `README.md`.
- Put shared styles and frontend behavior in `assets/`.
- Regenerate evidence-backed assets with `make evidence` after changing scripted demo output.
