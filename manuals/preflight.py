#!/usr/bin/env python3
"""
preflight.py — Lint Dusk Audio plugin manual source markdowns.

Run before build_manuals.py to fail fast on common authoring mistakes:

  1. No em dash (U+2014) or en dash (U+2013) in prose.
  2. No bare " -- " ASCII surrogate in prose (LaTeX would render as en dash).
  3. Every screenshot reference resolves on disk.
  4. Each chapter's front-matter version matches _data/plugins.yml.

Code fences (```...```), indented code blocks, and HTML comments are
exempt from the dash check; long horizontal-rule sequences (--- or ---|)
are tolerated as table separators or YAML delimiters.

Exit codes:
  0  all good
  1  one or more checks failed (errors printed to stderr)
  2  configuration error (missing input files, etc.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("error: PyYAML is required (pip install pyyaml or apt install python3-yaml)",
          file=sys.stderr)
    sys.exit(2)


# ---------- paths ----------

REPO_ROOT = Path(__file__).resolve().parent.parent
MANUALS_DIR = REPO_ROOT / "manuals"

# Website repo lives next to plugins repo. Allow override via env var.
WEBSITE_REPO = Path(os.environ.get(
    "DUSK_WEBSITE_REPO",
    Path.home() / "projects" / "dusk-audio.github.io",
))

PLUGINS_YML = WEBSITE_REPO / "_data" / "plugins.yml"

# Manual-specific screenshots live IN the plugins repo at
# manuals/screenshots/<slug>/ so they version with the manual prose. The
# website repo's assets/images/plugins/ tree (marketing screenshots) is
# untouched by this work.
SCREENSHOTS_DIR = MANUALS_DIR / "screenshots"


# ---------- markdown stripping ----------

FENCED_CODE_RE = re.compile(r"^```.*?^```", re.MULTILINE | re.DOTALL)
HTML_COMMENT_RE = re.compile(r"<!--.*?-->", re.DOTALL)
FRONT_MATTER_RE = re.compile(r"^---\n(.*?)\n---\n", re.DOTALL)
INDENTED_CODE_RE = re.compile(r"^(    .*\n)+", re.MULTILINE)


def strip_for_prose(md: str) -> str:
    """Strip front matter, fenced code, indented code, and HTML comments
    so the dash-check only sees user-facing prose."""
    md = FRONT_MATTER_RE.sub("", md, count=1)
    md = FENCED_CODE_RE.sub("", md)
    md = HTML_COMMENT_RE.sub("", md)
    md = INDENTED_CODE_RE.sub("", md)
    return md


# ---------- checks ----------

EM_DASH = "—"
EN_DASH = "–"

# A bare " -- " between non-space characters becomes an en dash in LaTeX.
# Long sequences of dashes (table separators "----", YAML delimiters "---")
# are fine; we only flag a literal " -- " (single space, exactly two dashes,
# single space) in prose.
BARE_DOUBLE_DASH_RE = re.compile(r"(?<![-\w]) -- (?![-\w])")


def check_dashes(path: Path, prose: str, errors: list[str]) -> None:
    for lineno, line in enumerate(prose.splitlines(), start=1):
        if EM_DASH in line:
            col = line.index(EM_DASH) + 1
            errors.append(f"{path}:{lineno}:{col}: em dash (U+2014) in prose: {line.strip()!r}")
        if EN_DASH in line:
            col = line.index(EN_DASH) + 1
            errors.append(f"{path}:{lineno}:{col}: en dash (U+2013) in prose: {line.strip()!r}")
        if BARE_DOUBLE_DASH_RE.search(line):
            errors.append(f"{path}:{lineno}: bare ' -- ' in prose (LaTeX renders as en dash): {line.strip()!r}")


SCREENSHOT_RE = re.compile(r"!\[[^\]]*\]\(([^)]+)\)")


def check_screenshots(path: Path, raw_md: str, errors: list[str]) -> None:
    """Resolve every ![alt](path) image reference. The convention is
    `screenshots/<slug>/<file>.png`, relative to the markdown file's
    location (which is manuals/<slug>.md). That resolves to
    manuals/screenshots/<slug>/<file>.png."""
    for match in SCREENSHOT_RE.finditer(raw_md):
        ref = match.group(1).strip()
        # Skip remote URLs.
        if ref.startswith(("http://", "https://", "//")):
            continue
        resolved = (path.parent / ref).resolve()
        if not resolved.exists():
            errors.append(f"{path}: screenshot not found: {ref} (resolved to {resolved})")


def parse_front_matter(raw_md: str, path: Path, errors: list[str]) -> dict | None:
    m = FRONT_MATTER_RE.match(raw_md)
    if not m:
        errors.append(f"{path}: missing YAML front matter")
        return None
    try:
        fm = yaml.safe_load(m.group(1))
    except yaml.YAMLError as e:
        errors.append(f"{path}: front matter YAML parse error: {e}")
        return None
    if not isinstance(fm, dict):
        errors.append(f"{path}: front matter must be a mapping")
        return None
    return fm


def load_plugin_versions(errors: list[str]) -> dict[str, str]:
    """Read _data/plugins.yml and return {slug: version}."""
    if not PLUGINS_YML.exists():
        errors.append(f"plugins.yml not found at {PLUGINS_YML} "
                      f"(set DUSK_WEBSITE_REPO env var to override)")
        return {}
    try:
        with PLUGINS_YML.open() as f:
            data = yaml.safe_load(f)
    except yaml.YAMLError as e:
        errors.append(f"{PLUGINS_YML}: YAML parse error: {e}")
        return {}
    versions: dict[str, str] = {}
    if not isinstance(data, list):
        errors.append(f"{PLUGINS_YML}: expected list of plugins")
        return {}
    for entry in data:
        if not isinstance(entry, dict):
            continue
        slug = entry.get("slug")
        version = entry.get("version")
        if slug and version:
            versions[slug] = str(version)
    return versions


def check_version_match(path: Path, fm: dict, plugin_versions: dict[str, str],
                       errors: list[str]) -> None:
    slug = fm.get("slug")
    fm_version = fm.get("version")
    if not slug:
        errors.append(f"{path}: front matter missing 'slug'")
        return
    if not fm_version:
        errors.append(f"{path}: front matter missing 'version'")
        return
    expected = plugin_versions.get(slug)
    if expected is None:
        errors.append(f"{path}: slug '{slug}' not found in {PLUGINS_YML}")
        return
    if str(fm_version) != expected:
        errors.append(
            f"{path}: front matter version {fm_version!r} does not match "
            f"plugins.yml version {expected!r} for slug {slug!r}"
        )


def discover_chapters(manuals_dir: Path) -> list[Path]:
    """Return chapter markdowns. Skips underscore-prefixed and README."""
    chapters: list[Path] = []
    for p in sorted(manuals_dir.glob("*.md")):
        if p.name.startswith("_") or p.name.lower() == "readme.md":
            continue
        chapters.append(p)
    return chapters


# ---------- driver ----------

def run_preflight(slug: str | None) -> int:
    errors: list[str] = []
    plugin_versions = load_plugin_versions(errors)
    chapters = discover_chapters(MANUALS_DIR)

    if slug is not None:
        chapters = [c for c in chapters if c.stem == slug]
        if not chapters:
            print(f"error: no chapter found for slug {slug!r} in {MANUALS_DIR}",
                  file=sys.stderr)
            return 2

    if not chapters:
        print(f"error: no chapter markdowns under {MANUALS_DIR}", file=sys.stderr)
        return 2

    for path in chapters:
        raw = path.read_text(encoding="utf-8")
        prose = strip_for_prose(raw)
        check_dashes(path, prose, errors)
        check_screenshots(path, raw, errors)
        fm = parse_front_matter(raw, path, errors)
        if fm is not None and plugin_versions:
            check_version_match(path, fm, plugin_versions, errors)

    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        print(f"\npreflight: {len(errors)} issue(s) across {len(chapters)} chapter(s)",
              file=sys.stderr)
        return 1

    print(f"preflight: clean ({len(chapters)} chapter(s) checked)")
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    p.add_argument("--slug", help="check only the chapter with this slug")
    args = p.parse_args(argv)
    return run_preflight(args.slug)


if __name__ == "__main__":
    sys.exit(main())
