#!/usr/bin/env python3
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
"""Package the retained, framework-free headless LV2 separately from desktop products."""
import argparse
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    if args.bundle.name != "Chord Analyzer Headless.lv2":
        parser.error("expected the retained Chord Analyzer Headless.lv2 bundle")
    source = Path(__file__).resolve().parent
    files = [(args.bundle / name, f"LV2/{args.bundle.name}/{name}")
             for name in ("manifest.ttl", "chord-analyzer-headless.ttl", "ChordAnalyzerHeadless.so")]
    files += [(source.parents[2] / "LICENSE", "LICENSE"), (source / "README.md", "README.md"),
              (source / "THIRD_PARTY_LICENSES.md", "THIRD_PARTY_LICENSES.md")]
    for path, _ in files:
        if not path.is_file() or path.is_symlink():
            parser.error(f"missing/nonregular package input: {path}")
    args.archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.archive, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path, name in files:
            archive.write(path, name)
    print(f"Packaged {args.archive}: standalone headless LV2, installation guide and licenses")


if __name__ == "__main__":
    main()
