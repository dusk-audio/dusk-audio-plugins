#!/usr/bin/env python3
"""
preflight.py — Lint Dusk Audio plugin manual source markdowns.

Run before build_manuals.py to fail fast on common authoring mistakes:

  1. No em dash (U+2014) or en dash (U+2013) in prose.
  2. No bare "--" (double hyphen) in prose, anywhere it appears not adjacent
     to another "-". LaTeX renders bare "--" as an en dash regardless of
     surrounding spaces (e.g., "word -- word", "word--word", "X--Y all match).
  3. Every screenshot reference resolves on disk.
  4. Each chapter's front-matter version matches _data/plugins.yml. For the
     combined manual (--combined), unreleased plugins' chapters are left out,
     and a chapter whose release is in early access may be one version ahead.

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
import subprocess
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
    so the dash-check only sees user-facing prose. Each removed block is
    replaced with newlines equal to the number of lines it spanned so
    downstream line numbers in check_dashes diagnostics stay aligned with
    the original source file."""
    def keep_lines(m: "re.Match[str]") -> str:
        return "\n" * m.group(0).count("\n")
    md = FRONT_MATTER_RE.sub(keep_lines, md, count=1)
    md = FENCED_CODE_RE.sub(keep_lines, md)
    md = HTML_COMMENT_RE.sub(keep_lines, md)
    md = INDENTED_CODE_RE.sub(keep_lines, md)
    return md


# ---------- checks ----------

EM_DASH = "—"
EN_DASH = "–"

# A bare "--" anywhere becomes an en dash in LaTeX. Match exactly two
# hyphens not part of a longer run; "---" (em dash), "----" (table
# separator), and YAML delimiters "---" must not match. The lookbehind
# and lookahead exclude any neighbouring hyphen so only a "double" run
# is caught.
BARE_DOUBLE_DASH_RE = re.compile(r"(?<!-)--(?!-)")


def check_dashes(path: Path, prose: str, errors: list[str]) -> None:
    for lineno, line in enumerate(prose.splitlines(), start=1):
        if EM_DASH in line:
            col = line.index(EM_DASH) + 1
            errors.append(f"{path}:{lineno}:{col}: em dash (U+2014) in prose: {line.strip()!r}")
        if EN_DASH in line:
            col = line.index(EN_DASH) + 1
            errors.append(f"{path}:{lineno}:{col}: en dash (U+2013) in prose: {line.strip()!r}")
        if BARE_DOUBLE_DASH_RE.search(line):
            errors.append(f"{path}:{lineno}: bare '--' (double hyphen) in prose, LaTeX renders as en dash: {line.strip()!r}")


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


# Website statuses of a plugin that is not out yet. Its manual chapter is a draft.
UNRELEASED_STATUSES = {"in-dev", "coming-soon"}
# Every status the website knows. Anything else is a typo, and a typo must not
# count as released and put a draft chapter in the combined PDF.
KNOWN_STATUSES = {"released", "pre-release"} | UNRELEASED_STATUSES


def load_plugin_catalog(errors: list[str]) -> tuple[dict[str, str], set[str]]:
    """Read _data/plugins.yml and return ({slug: version}, unreleased slugs).

    A plugin is unreleased when the website lists it without a version or with an
    unreleased status; its chapter must stay out of the combined PDF.
    """
    if not PLUGINS_YML.exists():
        errors.append(f"plugins.yml not found at {PLUGINS_YML} "
                      f"(set DUSK_WEBSITE_REPO env var to override)")
        return {}, set()
    try:
        with PLUGINS_YML.open() as f:
            data = yaml.safe_load(f)
    except yaml.YAMLError as e:
        errors.append(f"{PLUGINS_YML}: YAML parse error: {e}")
        return {}, set()
    versions: dict[str, str] = {}
    unreleased: set[str] = set()
    if not isinstance(data, list):
        errors.append(f"{PLUGINS_YML}: expected list of plugins")
        return {}, set()
    for entry in data:
        if not isinstance(entry, dict):
            continue
        slug = entry.get("slug")
        version = entry.get("version")
        status = entry.get("status")
        if slug and status is not None and status not in KNOWN_STATUSES:
            errors.append(f"{PLUGINS_YML}: {slug}: unknown status {status!r} "
                          f"(known: {', '.join(sorted(KNOWN_STATUSES))})")
            unreleased.add(slug)
        if slug and version:
            versions[slug] = str(version)
        if slug and (not version or status in UNRELEASED_STATUSES):
            unreleased.add(slug)
    return versions, unreleased


def load_plugin_versions(errors: list[str]) -> dict[str, str]:
    """Read _data/plugins.yml and return {slug: version}."""
    return load_plugin_catalog(errors)[0]


def release_tag_exists(slug: str, version: str) -> bool:
    """True when this repository has the release tag <slug>-v<version>.

    Manuals are built from a clone (/release-plugin), which has its tags. Without
    git or a tag, the answer is False, so a chapter ahead of the website is
    reported as a version mismatch rather than let into the combined PDF.
    """
    try:
        res = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "-q", "--verify",
             f"refs/tags/{slug}-v{version}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    except OSError:
        return False
    return res.returncode == 0


def _version_key(version: str) -> tuple[int, ...] | None:
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version.strip())
    return tuple(int(g) for g in m.groups()) if m else None


def in_early_access(slug: str, chapter_version: str, site_version: str) -> bool:
    """A chapter ahead of the website is in early access while its release tag
    exists: /release-plugin bumps the manual with the tag, but website main keeps
    the previous version until the early-access/<public date> branch merges."""
    ahead, shown = _version_key(chapter_version), _version_key(site_version)
    return (ahead is not None and shown is not None and ahead > shown
            and release_tag_exists(slug, chapter_version))


def check_version_match(path: Path, fm: dict, plugin_versions: dict[str, str],
                       errors: list[str], early_access_ok: bool = False,
                       notes: list[str] | None = None) -> None:
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
        if early_access_ok and in_early_access(slug, str(fm_version), expected):
            if notes is not None:
                notes.append(f"early access: {path.name} is {fm_version}; the website "
                             f"shows {expected} until its public date")
            return
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


def select_combined_chapters(unreleased: set[str], notes: list[str]) -> list[Path]:
    """The chapters the combined manual includes: every chapter except an
    unreleased plugin's draft. A slug the website does not list at all stays in,
    so its version check fails instead of the chapter quietly vanishing."""
    selected: list[Path] = []
    for path in discover_chapters(MANUALS_DIR):
        fm = parse_front_matter(path.read_text(encoding="utf-8"), path, errors=[])
        slug = fm.get("slug") if fm else None
        if slug in unreleased:
            notes.append(f"skip: {path.name} ({slug} is not released on the website)")
            continue
        selected.append(path)
    return selected


# ---------- driver ----------

def run_preflight(slug: str | None, combined: bool = False) -> int:
    """Lint the chapters. combined=True checks what the combined manual includes:
    unreleased plugins' chapters are left out and early-access chapters pass."""
    errors: list[str] = []
    notes: list[str] = []
    plugin_versions, unreleased = load_plugin_catalog(errors)
    if combined:
        chapters = select_combined_chapters(unreleased, notes)
    else:
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
        if fm is not None:
            check_version_match(path, fm, plugin_versions, errors,
                                early_access_ok=combined, notes=notes)

    for note in notes:
        print(note)
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
    p.add_argument("--combined", action="store_true",
                   help="check the chapters the combined manual includes")
    args = p.parse_args(argv)
    return run_preflight(args.slug, combined=args.combined)


if __name__ == "__main__":
    sys.exit(main())
