#!/usr/bin/env python3
"""render_matrix.py — render both plugins across the full 18-config matrix
(configs.py) into renders/matrix/{tapemachine,uad}/{key}/{stim}.wav.

  python3 render_matrix.py            # both plugins, all configs
  python3 render_matrix.py --mine     # TapeMachine only (fast tuning re-render)
  python3 render_matrix.py --uad      # UAD only (freeze the reference once)
  python3 render_matrix.py --only 456_NAB_15   # a single config key

UAD's transport flutter is intrinsic; TapeMachine uses shipped Wow=7/Flutter=3
for the wf tone and 0/0 for the clean sweep/THD tones.
"""
import os
import sys
import shutil
import tempfile
import subprocess
from configs import configs, STIMULI

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN = os.path.join(REPO, "build/tests/duskverb_render/duskverb_render")
PLUGS = {
    "tapemachine": os.path.expanduser("~/Library/Audio/Plug-Ins/Components/tape_machine_2.component"),
    "uad": "/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component",
}
STIM = os.path.join(HERE, "stimuli")
OUTROOT = os.path.join(HERE, "renders", "matrix")
PRERUN = "2"


def render(plugin_path, params, inp, dest):
    tmp = tempfile.mkdtemp()
    cmd = [BIN, "--au", plugin_path, "--input-wav", inp, "--slug", "s",
           "--output-dir", tmp, "--prerun-seconds", PRERUN]
    for name, val in params:
        cmd += ["--param", f"{name}={val}"]
    subprocess.run(cmd, capture_output=True, text=True)
    stem = os.path.join(tmp, "s_stem.wav")
    ok = os.path.exists(stem)
    if ok:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.move(stem, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    return ok


def main():
    args = sys.argv[1:]
    which = ["tapemachine", "uad"]
    if "--mine" in args:
        which = ["tapemachine"]
    if "--uad" in args:
        which = ["uad"]
    only = args[args.index("--only") + 1] if "--only" in args else None
    stim_filter = args[args.index("--stim") + 1] if "--stim" in args else None
    stimuli = [stim_filter] if stim_filter else STIMULI

    if not os.path.exists(BIN):
        sys.exit(f"renderer not built: {BIN}")

    total = fails = 0
    for c in configs():
        if only and c["key"] != only:
            continue
        for plug in which:
            cfgkey = "mine" if plug == "tapemachine" else "uad"
            for st in stimuli:
                inp = os.path.join(STIM, f"{st}.wav")
                dest = os.path.join(OUTROOT, plug, c["key"], f"{st}.wav")
                params = list(c[cfgkey])
                if plug == "tapemachine":
                    params += [("Wow", "7"), ("Flutter", "3")] if st == "wf_3150" \
                              else [("Wow", "0"), ("Flutter", "0")]
                total += 1
                if not render(PLUGS[plug], params, inp, dest):
                    fails += 1
                    print(f"  ! FAIL {plug}/{c['key']}/{st}")
            print(f"  {plug:11s} {c['key']}")
    print(f"Done: {total - fails}/{total} rendered into {OUTROOT}")
    if fails:
        sys.exit(1)


if __name__ == "__main__":
    main()
