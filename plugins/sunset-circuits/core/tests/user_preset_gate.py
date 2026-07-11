#!/usr/bin/env python3
"""user_preset_gate — runs the UserPresetStore unit binary.

The heavy lifting is in user_preset_test.cpp (built by tests/CMakeLists.txt): it
saves a 222-float patch, reloads it, and asserts a bit-exact round-trip plus
malformed-file rejection, missing-symbol defaults, sanitization, overwrite, and
delete. This wrapper just locates and runs it so run_all.sh treats it like the
other gates.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "user_preset_test")


def main():
    if not os.path.exists(BIN):
        print(f"user_preset_test binary not found at {BIN} "
              f"(did tests/CMakeLists.txt build it?)", file=sys.stderr)
        return 1
    r = subprocess.run([BIN])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
