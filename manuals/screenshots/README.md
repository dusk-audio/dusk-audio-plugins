# Manual Screenshots

Manual-specific screenshots, organized one folder per plugin. Each folder's `README.md` lists the expected file names and what each one should depict. The chapter markdowns in `manuals/<slug>.md` reference these files at relative paths, and `make preflight` fails if a referenced screenshot is missing.

## Conventions

- File names: `NN-<short-name>.png`, where `NN` is a zero-padded two-digit ordinal (01, 02, 03, ...) so files sort in capture order.
- Format: PNG, lossless, 1200 to 1600 px wide minimum.
- File size: under 500 KB per shot. Compress losslessly with `optipng` or `pngcrush` if needed.
- Per-plugin folder size: under 5 MB total.
- macOS users: capture with `Cmd+Shift+4` then `Space` to grab the plugin window cleanly. Linux: use `flameshot gui` or `gnome-screenshot --window`. Windows: Snipping Tool or Win+Shift+S.

## Workflow

1. Open the relevant per-plugin folder. Read its `README.md` to see what shots are needed.
2. Capture each one and save it with the exact filename listed.
3. Run `make preflight` from `manuals/` to confirm every reference resolves.
4. Run `make all` (with pandoc + xelatex installed) to render the PDFs.
