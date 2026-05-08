#!/usr/bin/env python3
"""
build_manuals.py — Render Dusk Audio plugin manuals to PDF.

Reads chapter markdowns under manuals/, runs preflight, then calls pandoc
with style.tex (xelatex engine) to produce:

  * dusk-audio-manual.pdf       — combined: cover + TOC + every chapter
  * <slug>-manual.pdf           — per-plugin: single chapter, slim cover

Outputs land in $DUSK_WEBSITE_REPO/assets/manuals/. The website serves
those files at /assets/manuals/ via GitHub Pages; the release CI
fetches the per-plugin PDF from main and injects it into release zips.

Usage:
  python build_manuals.py --slug multi-comp     # one plugin
  python build_manuals.py --combined            # combined manual
  python build_manuals.py --all                 # both
  python build_manuals.py --skip-preflight ...  # skip the lint pass

Requires: pandoc, xelatex (TeX Live with xetex), Python 3.8+, PyYAML.
Install:
  Debian/Ubuntu: sudo apt install pandoc texlive-xetex texlive-fonts-recommended
  Fedora/RHEL:   sudo dnf install pandoc texlive-xetex texlive-collection-fontsrecommended
  openSUSE:      sudo zypper install pandoc texlive-xetex texlive-collection-fontsrecommended
  macOS:         brew install pandoc && brew install --cask mactex-no-gui

Exit codes:
  0  success
  1  build error (pandoc failure, etc.)
  2  configuration error (missing tools, missing files)
  3  preflight failure
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import shutil
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("error: PyYAML is required (pip install pyyaml or apt install python3-yaml)",
          file=sys.stderr)
    sys.exit(2)

from preflight import (
    MANUALS_DIR, WEBSITE_REPO, PLUGINS_YML, SCREENSHOTS_DIR,
    discover_chapters, load_plugin_versions, parse_front_matter,
    run_preflight, FRONT_MATTER_RE,
)


# Stable display order used for the combined manual TOC. Lives in this
# file (not _data/plugins.yml) because it's a manual-specific concern;
# we want the most useful chapters first, not whatever order plugins.yml
# happens to use.
CHAPTER_ORDER = [
    "multi-comp",
    "multi-q",
    "4k-eq",
    "tapemachine",
    "duskverb",
    "spectrum-analyzer",
    "chord-analyzer",
]

OUTPUT_DIR = WEBSITE_REPO / "assets" / "manuals"
STYLE_TEX = MANUALS_DIR / "style.tex"


# ---------- tooling ----------

def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        print(f"error: required tool not found on PATH: {name}", file=sys.stderr)
        print("       install hints:", file=sys.stderr)
        print("         Debian/Ubuntu: sudo apt install pandoc texlive-xetex texlive-fonts-recommended",
              file=sys.stderr)
        print("         Fedora/RHEL:   sudo dnf install pandoc texlive-xetex texlive-collection-fontsrecommended",
              file=sys.stderr)
        print("         openSUSE:      sudo zypper install pandoc texlive-xetex texlive-collection-fontsrecommended",
              file=sys.stderr)
        print("         macOS:         brew install pandoc && brew install --cask mactex-no-gui",
              file=sys.stderr)
        sys.exit(2)
    return path


# ---------- markdown prep ----------

def rewrite_screenshot_paths(md: str) -> str:
    """Rewrite screenshots/<slug>/foo.png references to absolute paths
    so pandoc can resolve them regardless of working directory.
    The convention puts manual-specific screenshots in the plugins repo at
    manuals/screenshots/<slug>/, version-controlled alongside the prose."""
    prefix = "screenshots/"
    abs_prefix = str(SCREENSHOTS_DIR) + "/"
    out_lines = []
    for line in md.splitlines(keepends=True):
        # Pandoc's image syntax: ![alt](path). Replace bare relative path
        # only when it appears inside parentheses, to avoid hitting prose.
        if "](screenshots/" in line:
            line = line.replace("](screenshots/", "](" + abs_prefix)
        out_lines.append(line)
    return "".join(out_lines)


def strip_front_matter(md: str) -> str:
    return FRONT_MATTER_RE.sub("", md, count=1)


def cover_block_for(fm: dict) -> str:
    """Render a small cover-block at the top of a per-plugin chapter:
    plugin name, version, last-updated date. Fed into pandoc as raw markdown."""
    title = fm.get("title") or ""  # tag-only chapters skip explicit title
    slug = fm.get("slug", "")
    version = fm.get("version", "")
    tagline = fm.get("tagline", "")
    last_updated = fm.get("last_updated", "")
    parts = []
    parts.append(f"\\begin{{center}}\n{{\\Huge\\bfseries {slug.replace('-', ' ').title()}}}\\par\n\\vspace{{4pt}}\n")
    if tagline:
        parts.append(f"{{\\large\\itshape {tagline}}}\\par\n")
    parts.append("\\vspace{6pt}\n")
    if version:
        meta = f"Version {version}"
        if last_updated:
            meta += f" \\quad Last updated: {last_updated}"
        parts.append(f"{{\\normalsize {meta}}}\\par\n")
    parts.append("\\end{center}\n\n\\vspace{12pt}\n\n")
    return "".join(parts)


def combined_cover_block() -> str:
    today = dt.date.today().isoformat()
    return (
        "\\begin{titlepage}\n"
        "\\begin{center}\n"
        "\\vspace*{2cm}\n"
        "{\\Huge\\bfseries Dusk Audio}\\par\n"
        "\\vspace{0.5cm}\n"
        "{\\LARGE Plugin User Manual}\\par\n"
        "\\vspace{2cm}\n"
        f"{{\\large Built {today}}}\\par\n"
        "\\vfill\n"
        "{\\itshape Free, professional audio plugins for VST3, LV2, and AU.}\\par\n"
        "\\vspace{1cm}\n"
        "\\end{center}\n"
        "\\end{titlepage}\n\n"
        "\\tableofcontents\n"
        "\\newpage\n\n"
    )


# ---------- pandoc invocation ----------

PANDOC_BASE_ARGS = [
    "--pdf-engine=xelatex",
    "--toc-depth=2",
    f"--include-in-header={STYLE_TEX}",
    "-V", "geometry:margin=1in",
    "-V", "fontsize=11pt",
    "-V", "linkcolor=blue",
    "-V", "urlcolor=blue",
    "-V", "toccolor=black",
]


def run_pandoc(input_md: str, output_pdf: Path, with_toc: bool = False) -> None:
    """Pipe markdown into pandoc, write PDF to output_pdf."""
    pandoc = require_tool("pandoc")
    require_tool("xelatex")
    args = [pandoc, "-f", "markdown", "-o", str(output_pdf)]
    args += PANDOC_BASE_ARGS
    if with_toc:
        args.append("--toc")
    output_pdf.parent.mkdir(parents=True, exist_ok=True)
    res = subprocess.run(args, input=input_md, text=True, capture_output=True)
    if res.returncode != 0:
        print(f"pandoc failed (exit {res.returncode}):", file=sys.stderr)
        print(res.stderr, file=sys.stderr)
        sys.exit(1)
    print(f"  wrote {output_pdf}")


# ---------- per-plugin / combined renderers ----------

def render_per_plugin(slug: str) -> Path:
    chapter = MANUALS_DIR / f"{slug}.md"
    if not chapter.exists():
        print(f"error: chapter not found: {chapter}", file=sys.stderr)
        sys.exit(2)

    raw = chapter.read_text(encoding="utf-8")
    fm = parse_front_matter(raw, chapter, errors=[])
    if fm is None:
        print(f"error: missing front matter in {chapter}", file=sys.stderr)
        sys.exit(2)

    body = strip_front_matter(raw)
    body = rewrite_screenshot_paths(body)

    md = cover_block_for(fm) + body
    out = OUTPUT_DIR / f"{slug}-manual.pdf"
    print(f"render: {slug}")
    run_pandoc(md, out, with_toc=False)
    return out


def render_combined() -> Path:
    chapters = discover_chapters(MANUALS_DIR)
    by_slug = {c.stem: c for c in chapters}
    # Order per CHAPTER_ORDER, then any leftover slugs alphabetically.
    ordered: list[Path] = []
    for slug in CHAPTER_ORDER:
        if slug in by_slug:
            ordered.append(by_slug[slug])
    for slug, path in sorted(by_slug.items()):
        if path not in ordered:
            ordered.append(path)

    parts = [combined_cover_block()]
    for path in ordered:
        raw = path.read_text(encoding="utf-8")
        body = strip_front_matter(raw)
        body = rewrite_screenshot_paths(body)
        parts.append(body)
        parts.append("\n\n\\newpage\n\n")

    md = "".join(parts)
    out = OUTPUT_DIR / "dusk-audio-manual.pdf"
    print("render: combined manual")
    run_pandoc(md, out, with_toc=False)  # combined includes its own \tableofcontents
    return out


# ---------- driver ----------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--slug", help="render a single per-plugin manual")
    g.add_argument("--combined", action="store_true",
                   help="render the combined dusk-audio-manual.pdf")
    g.add_argument("--all", action="store_true",
                   help="render both the combined manual AND every per-plugin manual")
    p.add_argument("--skip-preflight", action="store_true",
                   help="skip the lint pass (use only when iterating fast)")
    args = p.parse_args(argv)

    if not args.skip_preflight:
        preflight_slug = args.slug if args.slug else None
        rc = run_preflight(preflight_slug)
        if rc != 0:
            return 3

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    if args.slug:
        render_per_plugin(args.slug)
    elif args.combined:
        render_combined()
    elif args.all:
        for chapter in discover_chapters(MANUALS_DIR):
            render_per_plugin(chapter.stem)
        render_combined()

    return 0


if __name__ == "__main__":
    sys.exit(main())
