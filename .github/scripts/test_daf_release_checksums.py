#!/usr/bin/env python3
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
"""Exercise the production release gate with every registered product and damaged sets."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    workflow = (root / ".github/workflows/daf-build.yml").read_text()
    registry = json.loads(re.search(r"\n          (\[\n.*?\n          \])\n          JSON", workflow, re.S)[1])
    gate = root / ".github/scripts/daf_release_checksums.sh"
    with tempfile.TemporaryDirectory(prefix="dusk-release-assets-") as temporary:
        directory = Path(temporary)
        for product in registry:
            asset, base = product["asset"], product["base"]
            assert re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", asset)
            dist = directory / base
            dist.mkdir()
            names = [f"{asset}-{arch}.zip" for arch in ("linux", "linux-arm64", "macos", "windows")]
            if base == "chord-analyzer-2":
                names += ["chord-analyzer-headless-linux.zip", "chord-analyzer-headless-linux-arm64.zip"]
            for name in names:
                # Match download-artifact's nested layout while hashing the public basename.
                folder = dist / name.removesuffix(".zip")
                folder.mkdir()
                (folder / name).write_bytes(name.encode())
            command = ["bash", str(gate), asset, base, str(dist)]
            subprocess.run(command, check=True)
            records = [line.split() for line in (dist / "SHA256SUMS").read_text().splitlines()]
            assert sorted(name for _, name in records) == sorted(names)
            assert all(digest == hashlib.sha256(name.encode()).hexdigest() for digest, name in records)
            missing = dist / names[0].removesuffix(".zip") / names[0]
            for damage in ("missing", "substituted", "whitespace-substituted", "duplicate", "duplicate-substituted", "extra"):
                changed = dist / "unexpected.zip"
                if damage == "whitespace-substituted":
                    changed = dist / (names[0] + " unexpected.zip")
                if damage in ("missing", "substituted", "whitespace-substituted", "duplicate-substituted"):
                    missing.unlink()
                if damage in ("substituted", "whitespace-substituted", "extra"):
                    changed.write_bytes(b"unexpected archive")
                elif damage in ("duplicate", "duplicate-substituted"):
                    changed = dist / "elsewhere" / names[1 if damage == "duplicate-substituted" else 0]
                    changed.parent.mkdir(exist_ok=True)
                    changed.write_bytes(b"same basename, different payload")
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                assert result.returncode != 0, (base, damage, result.stdout)
                if changed.is_file():
                    changed.unlink()
                if not missing.exists():
                    missing.write_bytes(names[0].encode())
            subprocess.run(command, check=True)
    print(f"Release checksums: {len(registry)} product sets pass; missing, substituted, duplicate and extra assets reject")


if __name__ == "__main__":
    main()
