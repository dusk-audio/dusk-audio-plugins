#!/usr/bin/env python3
"""clap-validator gate for Dusk Audio DPF plugins.

Purpose-built gate for our dusk-audio/DPF fork (see the DPF_REF comment in
.github/workflows/dpf-build.yml). The fork exists because upstream DPF's CLAP
wrapper called clap_host_latency::changed OUTSIDE clap_plugin::activate(),
violating the CLAP spec. This script runs clap-validator on a built .clap and:

  HARD FAIL  If the validator reports the 'clap_host_latency::changed' host
             callback error, DPF has regressed to the upstream bug -> exit 1.
             (In clap-validator 0.4.1 this shows only as a WARNING and does NOT
             fail any test, so the exit code alone would miss it -- hence the
             explicit signature check.)

  SUITE      Every other clap-validator test also gates the build, because the
             workflow sets SUITE_STRICT=1. Failure is decided by BOTH the reported
             failure tally and the process exit code, since the latency case above
             is proof that the exit code alone can miss a real regression.

             CI is the source of truth for which plugins currently pass. No pass
             counts are recorded here on purpose: they are a property of the
             validator version as much as of the plugins, and a baked-in tally rots
             silently into a lie the next time either moves.

             Both of the process-varying-sample-rates failures that kept this
             advisory (TapeMachine 2 at 8 kHz, 4K EQ 2 at 1234.57 Hz) had the same
             root cause: filter design frequencies were not clamped to Nyquist, so
             at low host rates the RBJ/bilinear prewarp wrapped and the biquads got
             poles outside the unit circle. Fixed in the DBiquad designers and in
             the shared duskaudio::Biquad designers respectively.

             process-audio-denormals can emit a timing WARNING on a loaded machine.
             That one does not gate: it sets no failure tally and no exit code, and
             it carries no gated signature. That is specific to timing warnings, NOT
             a general property of warnings, the latency check above deliberately
             hard-fails on a WARNING by scanning for its signature.

             If a plugin ever has to be exempted, gate per plugin rather than
             returning the whole matrix to advisory. NEVER add a per-test
             allowlist -- fix the plugin instead.

Usage: dpf_clap_validate.py <path-to-plugin.clap>
Env:
  CLAP_VALIDATOR_BIN      Use this binary instead of downloading a release
                          (Linux ARM64 has no prebuilt: cargo-install and point
                          this at it, or put clap-validator on PATH).
  CLAP_VALIDATOR_VERSION  Release tag to download (default 0.4.1).
  SUITE_STRICT=1          Also fail the build on any failed validator test. The
                          workflow sets this; unset it (or set 0) only for a local
                          advisory run. Unset, empty and whitespace-only all mean
                          advisory. Any OTHER unrecognized value is an error rather
                          than a silent fallback to advisory.
"""
import glob
import hashlib
import re
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

VERSION = os.environ.get("CLAP_VALIDATOR_VERSION", "0.4.1")
# The 0.4.1 release assets carry a git-describe suffix in their filenames. Pin it
# alongside VERSION; bump both together (the inner tarball misreports an older
# version, so we locate the binary by name, never by filename).
ASSET_SUFFIX = "127-g152b982"
# SHA-256 digests published by GitHub for the exact 0.4.1 release assets above.
# Asset names are the keys so a version/suffix change fails closed until its
# corresponding digest is deliberately reviewed and pinned here.
ASSET_SHA256 = {
    "clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip":
        "49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90",
    "clap-validator-0.4.1-127-g152b982-macos-universal.zip":
        "bbec8cd7d18274e549d5d8c12ece3cec54be966129388dd2e742b9957f2ba9f1",
    "clap-validator-0.4.1-127-g152b982-windows.zip":
        "d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10",
}
LATENCY_SIGNATURE = "clap_host_latency::changed"
RUN_TIMEOUT_S = 600


def log(msg):
    print(msg, flush=True)


def _suite_strict():
    """True when SUITE_STRICT asks for strict mode; exits on an unrecognized value.

    Fails CLOSED on a value it cannot read. The obvious way to write this is
    `os.environ.get(...) == "1"`, which silently drops the whole matrix back to
    advisory the day somebody sets SUITE_STRICT: "true" in the workflow. A gate that
    quietly stops gating is worse than no gate, so anything not clearly on or off is
    an error -- with one deliberate exception: unset, empty and whitespace-only all
    mean "not configured" and yield advisory, because an unset GHA expression
    interpolates to the empty string and that is an absent setting, not a typo.
    """
    raw = os.environ.get("SUITE_STRICT")
    # UNSET and EMPTY are the same thing, and whitespace-only counts as empty.
    # `env: SUITE_STRICT: ${{ ... }}` with an unset expression interpolates to the
    # empty string, which is a configuration that never reached this script rather
    # than a value someone typed. Erroring on it would break the workflow for a
    # non-decision; erroring on "maybe" catches a real one.
    value = (raw or "").strip().lower()
    if value == "":
        return False
    if value in ("1", "true", "yes", "on"):
        return True
    if value in ("0", "false", "no", "off"):
        return False
    log(f"::error::SUITE_STRICT is set to {raw!r}, which is neither on nor off. "
        "Use 1 or 0 (true/false, yes/no and on/off are also accepted). Refusing to "
        "guess, because guessing wrong silently disables the suite gate.")
    sys.exit(2)


def _parse_failed_count(output):
    """Pull the failure tally out of clap-validator's summary line.

    The line looks like: '44 tests run, 33 passed, 0 failed, 0 warnings, 11 skipped'.
    Returns None when no summary is found, which the caller treats as "no tally
    signal" rather than as a pass, so a format change degrades to the exit code
    instead of silently reporting success.
    """
    matches = re.findall(r"(\d+)\s+failed", _strip_ansi(output))
    if not matches:
        return None
    return max(int(m) for m in matches)


def _strip_ansi(text):
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


def _asset_name():
    system = platform.system()
    machine = platform.machine().lower()
    if system == "Linux":
        if machine in ("aarch64", "arm64"):
            return None  # no prebuilt Linux ARM64 asset upstream
        return f"clap-validator-{VERSION}-{ASSET_SUFFIX}-ubuntu-22.04.zip"
    if system == "Darwin":
        return f"clap-validator-{VERSION}-{ASSET_SUFFIX}-macos-universal.zip"
    if system == "Windows":
        return f"clap-validator-{VERSION}-{ASSET_SUFFIX}-windows.zip"
    return None


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as archive:
        for chunk in iter(lambda: archive.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _reject_escaping_members(names, workdir):
    """Fail closed on any archive member that would land outside workdir.

    The pinned SHA-256 already fixes the archive's contents, so this is
    defence in depth: absolute member paths, ../ traversals and separator
    tricks all exit instead of writing outside the temp dir.
    """
    base = os.path.realpath(workdir)
    for name in names:
        target = os.path.realpath(os.path.join(base, name))
        if target != base and not target.startswith(base + os.sep):
            log("::error::clap-validator archive member escapes the extraction "
                f"directory: {name!r}")
            sys.exit(2)


def _extract_zip_safely(archive, workdir):
    _reject_escaping_members(archive.namelist(), workdir)
    archive.extractall(workdir)


def _extract_tar_safely(archive, workdir):
    """extractall() a tar, using the 'data' filter only where it exists.

    tarfile's filter= keyword arrived in 3.12 and was backported to the 3.8-3.11
    security releases (tarfile.data_filter is the feature probe). Older
    interpreters must not be passed it -- filter= raises TypeError there -- so
    the member checks below carry the protection on their own: paths are
    confined to workdir and only regular files and directories are extracted,
    which rules out the link/device members the data filter guards against.
    """
    members = archive.getmembers()
    _reject_escaping_members([m.name for m in members], workdir)
    for member in members:
        if not (member.isfile() or member.isdir()):
            log("::error::clap-validator tarball member is not a regular file "
                f"or directory: {member.name!r}")
            sys.exit(2)
    if hasattr(tarfile, "data_filter"):
        archive.extractall(workdir, filter="data")
    else:
        archive.extractall(workdir)


def resolve_binary(workdir):
    """Return a path to a runnable clap-validator, downloading if necessary."""
    env_bin = os.environ.get("CLAP_VALIDATOR_BIN")
    if env_bin:
        if not os.path.exists(env_bin):
            log(f"::error::CLAP_VALIDATOR_BIN set but not found: {env_bin}")
            sys.exit(2)
        return env_bin

    on_path = shutil.which("clap-validator")
    if on_path:
        return on_path

    asset = _asset_name()
    if asset is None:
        log("::error::No prebuilt clap-validator for this platform "
            f"({platform.system()}/{platform.machine()}). Install it (e.g. "
            "cargo install) and set CLAP_VALIDATOR_BIN or add it to PATH.")
        sys.exit(2)

    url = (f"https://github.com/free-audio/clap-validator/releases/download/"
           f"{VERSION}/{asset}")
    zpath = os.path.join(workdir, asset)
    expected_sha256 = ASSET_SHA256.get(asset)
    if expected_sha256 is None:
        log("::error::No pinned SHA-256 digest for clap-validator asset "
            f"{asset}. Add the reviewed release digest or set CLAP_VALIDATOR_BIN.")
        sys.exit(2)

    log(f"Downloading {url}")
    urllib.request.urlretrieve(url, zpath)
    actual_sha256 = _sha256(zpath)
    if actual_sha256 != expected_sha256:
        log("::error::clap-validator archive SHA-256 mismatch for "
            f"{asset}: expected {expected_sha256}, got {actual_sha256}")
        sys.exit(2)
    log(f"Verified SHA-256: {actual_sha256}")

    with zipfile.ZipFile(zpath) as z:
        _extract_zip_safely(z, workdir)
    # Linux/macOS assets wrap a .tar.gz; Windows ships the .exe directly.
    for tgz in glob.glob(os.path.join(workdir, "*.tar.gz")):
        with tarfile.open(tgz) as t:
            _extract_tar_safely(t, workdir)

    for root, _dirs, files in os.walk(workdir):
        for name in ("clap-validator", "clap-validator.exe"):
            if name in files:
                p = os.path.join(root, name)
                os.chmod(p, 0o755)
                return p
    log("::error::clap-validator binary not found after extraction")
    sys.exit(2)


def main():
    if len(sys.argv) != 2:
        log("usage: dpf_clap_validate.py <path-to.clap>")
        sys.exit(2)
    clap = sys.argv[1]
    if not os.path.exists(clap):
        log(f"::error::plugin not found: {clap}")
        sys.exit(2)

    # Resolve SUITE_STRICT UP FRONT, not lazily inside the failure branch. Checking
    # it only when the suite already failed means a misconfigured value sails
    # through every green run and is discovered on the day it matters least.
    strict = _suite_strict()
    log(f"suite gate: {'STRICT' if strict else 'ADVISORY'}")

    workdir = tempfile.mkdtemp(prefix="clapval_")
    try:
        vbin = resolve_binary(workdir)
        log(f"Using clap-validator: {vbin}")
        try:
            proc = subprocess.run(
                [vbin, "validate", clap],
                capture_output=True, text=True, timeout=RUN_TIMEOUT_S,
            )
        except subprocess.TimeoutExpired as exc:
            log(f"::error::clap-validator timed out after {RUN_TIMEOUT_S}s on {clap}")
            for stream_name, output in (("stdout", exc.stdout), ("stderr", exc.stderr)):
                if output:
                    if isinstance(output, bytes):
                        output = output.decode(errors="replace")
                    log(f"Partial {stream_name}:\n{output}")
            sys.exit(2)

        out = (proc.stdout or "") + (proc.stderr or "")
        log(out)

        # HARD GATE: the DPF CLAP latency contract.
        if LATENCY_SIGNATURE in out:
            log(f"::error::DPF CLAP latency regression: '{LATENCY_SIGNATURE}' "
                "reported -- latency changed outside clap_plugin::activate(). "
                "This is the upstream bug the dusk-audio/DPF fork fixes; DPF has "
                "regressed. Check DPF_REF against dusk-audio/DPF main.")
            sys.exit(1)

        # STRICT (or advisory) on the remaining suite.
        #
        # Do NOT trust the exit code alone. This script exists because a
        # clap-validator WARNING can carry a real regression while leaving the exit
        # code at 0 (the latency contract above is exactly that case), so keying the
        # suite gate purely on returncode repeats the mistake one level up. Parse the
        # reported failure tally as well and fail on either signal: the tally catches
        # a failing test that somehow exits 0, the returncode catches a crash or a
        # timeout that never prints a tally.
        failed = _parse_failed_count(out)
        suite_bad = proc.returncode != 0 or (failed is not None and failed > 0)

        if suite_bad:
            if strict:
                log(f"::error::clap-validator reported failing tests "
                    f"(SUITE_STRICT=1; failed={failed}, exit={proc.returncode}).")
                sys.exit(proc.returncode or 1)
            log("::warning::clap-validator reported failing tests (advisory -- not "
                "gating). Fix the suite, then set SUITE_STRICT=1. Latency contract PASSED.")
        else:
            log("clap-validator suite clean. Latency contract PASSED.")
        sys.exit(0)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
