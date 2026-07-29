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

  ADVISORY   The rest of the clap-validator suite is run and printed, but does
             NOT gate the build yet: TapeMachine 2 currently fails
             process-varying-sample-rates (subnormals at extreme sample rates),
             a separate DSP issue. Once the suite is clean, set SUITE_STRICT=1
             to also fail on any failed test. NEVER add a per-test allowlist --
             fix the plugin, then flip SUITE_STRICT on.

Usage: dpf_clap_validate.py <path-to-plugin.clap>
Env:
  CLAP_VALIDATOR_BIN      Use this binary instead of downloading a release
                          (Linux ARM64 has no prebuilt: cargo-install and point
                          this at it, or put clap-validator on PATH).
  CLAP_VALIDATOR_VERSION  Release tag to download (default 0.4.1).
  SUITE_STRICT=1          Also fail the build on any failed validator test.
"""
import glob
import hashlib
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
        z.extractall(workdir)
    # Linux/macOS assets wrap a .tar.gz; Windows ships the .exe directly.
    for tgz in glob.glob(os.path.join(workdir, "*.tar.gz")):
        with tarfile.open(tgz) as t:
            t.extractall(workdir)

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

    workdir = tempfile.mkdtemp(prefix="clapval_")
    try:
        vbin = resolve_binary(workdir)
        log(f"Using clap-validator: {vbin}")
        try:
            proc = subprocess.run(
                [vbin, "validate", clap],
                capture_output=True, text=True, timeout=RUN_TIMEOUT_S,
            )
        except subprocess.TimeoutExpired:
            log(f"::error::clap-validator timed out after {RUN_TIMEOUT_S}s on {clap}")
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

        # ADVISORY (or strict) on the remaining suite.
        if proc.returncode != 0:
            if os.environ.get("SUITE_STRICT") == "1":
                log("::error::clap-validator reported failing tests (SUITE_STRICT=1).")
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
