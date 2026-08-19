#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One apt front end for every workflow in this repo.
#
# Ubuntu mirrors are the least reliable thing in this CI. Two distinct failure
# shapes have both taken the fleet down, and they need different defences:
#
#   STALL. An unreachable mirror does not fail apt, it hangs it. On run
#   32157020545 five of the eleven juce-compile-check jobs sat in their install
#   step until the 45-minute job timeout killed them: three hours of runner
#   time, reported as "cancelled" with no error to read. A retry loop is no
#   help here, because a call that hangs never comes back to be retried. Hence
#   timeout(1) around every apt invocation.
#
#   HARD FAIL. On run 32176674366 the DPF container's prerequisite step failed
#   outright in 81 seconds. That one WOULD have survived a retry, but the loop
#   only existed in a later step which never ran. Hence retries everywhere,
#   from one place, rather than hand-copied into whichever step someone
#   remembered.
#
#   WEDGED DPKG. A timed-out install can kill dpkg mid-transaction; every
#   later attempt then fails on "dpkg was interrupted" no matter how healthy
#   the network is (2026-08-19). Hence a bounded `dpkg --configure -a`
#   before each retry.
#
# Mirror policy (owner decision, 2026-08-19): stock Ubuntu repos ONLY.
# azure.archive.ubuntu.com has been the sick side of every incident to date
# and is purged from every mirror file up front, including the hosted
# runners' /etc/apt/apt-mirrors.txt mirrorlist, which earlier rewrites
# missed entirely (apt kept fetching azure while the sources files looked
# clean). No third-party mirrors: Ubuntu 22.04 is supported until 2027 and
# the stock archive is the reference.
#
# Usage:
#   apt_install.sh pkg [pkg...]   update, then install those packages
#   apt_install.sh --update-only  refresh the lists only (after adding a PPA)
#
# Runs as root in a container and via sudo on a hosted runner, detected rather
# than configured, because the call sites are split between both.
set -uo pipefail

# The whole point of this script is to fail fast and legibly, so its own worst
# case has to fit inside the caller's job budget -- otherwise a pathological run
# is killed by the JOB timeout and reports as "cancelled" with no error, which is
# the exact failure mode this exists to remove. Callers run under 30-, 40- and
# 45-minute job limits, so the defaults are sized against the smallest:
#
#   3 x (120 update + 240 install) + 2 x (45 dpkg repair + 10 sleep) = 1190s,
#   just under 20 minutes.
#
# A caller with a tighter step budget can lower any of these from the workflow.
ATTEMPTS="${APT_ATTEMPTS:-3}"
UPDATE_TIMEOUT="${APT_UPDATE_TIMEOUT:-120}"
INSTALL_TIMEOUT="${APT_INSTALL_TIMEOUT:-240}"
RETRY_SLEEP="${APT_RETRY_SLEEP:-10}"
APT_OPTS=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30)

# A bad override must not pass silently, and ATTEMPTS is the one that would:
# `for i in $(seq 1 abc)` prints an error, runs the body zero times, and leaves
# the script exiting 0 having installed NOTHING. A typo'd APT_ATTEMPTS in a
# workflow would therefore turn this into a no-op that reports success, and the
# missing packages would surface much later as a confusing compile error.
# Zero and negatives fail the same way. The timeouts are checked too, since a
# non-numeric one makes every attempt fail for a reason that has nothing to do
# with the mirror.
require_positive_int() {
    case "$2" in
        '' | *[!0-9]* ) ;;
        * ) [ "$2" -gt 0 ] && return 0 ;;
    esac
    echo "::error::$1 must be a positive integer, got '$2'" >&2
    exit 2
}
require_positive_int APT_ATTEMPTS        "$ATTEMPTS"
require_positive_int APT_UPDATE_TIMEOUT  "$UPDATE_TIMEOUT"
require_positive_int APT_INSTALL_TIMEOUT "$INSTALL_TIMEOUT"
case "$RETRY_SLEEP" in
    '' | *[!0-9]* )
        echo "::error::APT_RETRY_SLEEP must be a non-negative integer, got '$RETRY_SLEEP'" >&2
        exit 2 ;;
esac

# Two URL families, and arm64 is not a footnote: aarch64 Ubuntu serves from
# ports.ubuntu.com/ubuntu-ports, which shares no substring with the x86 archive
# host. An earlier version rewrote only the archive pair, so on the arm64 jobs
# the sed matched nothing and every fetch went back to the same dead mirror.
# Rewritten at the HOST level so the scheme is preserved: an https:// azure
# entry is just as real as an http:// one, and a URL-with-scheme pattern would
# silently skip it (the verification below caught exactly that in testing).
AZURE_ARCHIVE_HOST="azure.archive.ubuntu.com"
STOCK_ARCHIVE_HOST="archive.ubuntu.com"
AZURE_PORTS_HOST="azure.ports.ubuntu.com"
STOCK_PORTS_HOST="ports.ubuntu.com"

if [ "$(id -u)" -eq 0 ]; then
    SUDO=()
else
    SUDO=(sudo)
fi

update_only=0
if [ "${1:-}" = "--update-only" ]; then
    update_only=1
    shift
fi
packages=("$@")

if [ "$update_only" -eq 0 ] && [ ${#packages[@]} -eq 0 ]; then
    echo "apt_install: no packages given" >&2
    exit 2
fi

# All the places a mirror can hide. 24.04 images ship deb822 .sources files
# and no sources.list; hosted runners additionally route apt through a
# MIRRORLIST at /etc/apt/apt-mirrors.txt (the sources say
# "mirror+file:/etc/apt/apt-mirrors.txt"). Run 32286384090's sibling proved
# that skipping the mirrorlist makes every rewrite here a silent no-op: the
# log said "mirror: stock" while every fetch still hit azure.
mirror_files() {
    local f
    for f in /etc/apt/sources.list \
             /etc/apt/sources.list.d/*.list \
             /etc/apt/sources.list.d/*.sources \
             /etc/apt/apt-mirrors.txt; do
        [ -f "$f" ] && printf '%s\n' "$f"
    done
}

# Plain loop rather than `xargs -r grep`: -r is a GNU extension, so the xargs
# form silently reported "no match" when this was exercised on macOS and both
# mirror families looked identical. It would have worked on the Linux runners
# and been untestable anywhere else.
#
# grep runs under the same privilege as the sed that rewrites these files. A
# file readable only by root must not come back as a clean "no match", because
# the one caller is a verification pass and that is how such a pass talks
# itself into succeeding. Three outcomes, kept distinct:
#   0  matched somewhere
#   1  no match, every file read
#   2  at least one file could not be read, so the answer is unknown
sources_match() {
    local f rc status=1
    while read -r f; do
        "${SUDO[@]}" grep -q "$1" "$f" 2>/dev/null
        rc=$?
        # A match anywhere is conclusive even if another file was unreadable.
        [ "$rc" -eq 0 ] && return 0
        [ "$rc" -ge 2 ] && status=2
    done < <(mirror_files)
    return "$status"
}

# A failed rewrite is not cosmetic here: the whole point of this script is that
# apt never talks to azure, so a sed that could not write its file must be a
# hard failure rather than a silent pass. The loop reads from a process
# substitution, not a pipe, so it runs in this shell and its status survives.
rewrite_sources() {
    local from="$1" to="$2" f rc=0
    while read -r f; do
        "${SUDO[@]}" sed -i -e "s|${from}|${to}|g" "$f" || rc=1
    done < <(mirror_files)
    return "$rc"
}

# Purge azure from every mirror file (including the runner mirrorlist) before
# the first attempt, so apt talks only to the stock Ubuntu archive. Both URL
# families are rewritten unconditionally rather than picking one by
# architecture: a machine listing both (a cross-arch image, or a stray
# .sources) would otherwise keep whichever family was not chosen, which is
# exactly the silent no-op this script exists to end.
banish_azure() {
    local rc=0
    rewrite_sources "$AZURE_ARCHIVE_HOST" "$STOCK_ARCHIVE_HOST" || rc=1
    rewrite_sources "$AZURE_PORTS_HOST" "$STOCK_PORTS_HOST" || rc=1

    if [ "$rc" -ne 0 ]; then
        echo "::error::apt could not rewrite its mirror files to drop azure" >&2
        exit 1
    fi

    # Verify rather than assume. Matched on the exact Ubuntu mirror hosts, so
    # unrelated azure repositories on the runner (packages.microsoft.com's
    # azure-cli, for one) are left alone and do not trip this.
    local archive_rc ports_rc
    sources_match "azure\.archive\.ubuntu\.com"; archive_rc=$?
    sources_match "azure\.ports\.ubuntu\.com"; ports_rc=$?

    # Unreadable is not the same as clean, and only one of those is safe to
    # continue on.
    if [ "$archive_rc" -ge 2 ] || [ "$ports_rc" -ge 2 ]; then
        echo "::error::apt could not read its mirror files to confirm azure is gone" >&2
        exit 1
    fi

    if [ "$archive_rc" -eq 0 ] || [ "$ports_rc" -eq 0 ]; then
        echo "::error::azure survived the mirror rewrite; refusing to fetch from it" >&2
        exit 1
    fi
}

attempt() {
    "${SUDO[@]}" timeout "$UPDATE_TIMEOUT" apt-get update "${APT_OPTS[@]}" || return 1
    [ "$update_only" -eq 1 ] && return 0
    "${SUDO[@]}" timeout "$INSTALL_TIMEOUT" env DEBIAN_FRONTEND=noninteractive \
        apt-get install -y --no-install-recommends "${APT_OPTS[@]}" "${packages[@]}"
}

# A timed-out install can kill dpkg mid-transaction; every later attempt then
# dies on "dpkg was interrupted, you must manually run 'dpkg --configure -a'"
# no matter how healthy the mirror is. Repair before each retry.
repair_dpkg() {
    "${SUDO[@]}" timeout 45 env DEBIAN_FRONTEND=noninteractive \
        dpkg --configure -a || true
}

banish_azure

for i in $(seq 1 "$ATTEMPTS"); do
    if attempt; then
        # A non-zero exit is not the only way apt disappoints. Confirm the
        # packages are actually present, so a missing one fails here with a
        # clear message instead of surfacing as a confusing compile or loader
        # error several steps later.
        missing=()
        for pkg in ${packages+"${packages[@]}"}; do
            dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
        done
        if [ ${#missing[@]} -eq 0 ]; then
            exit 0
        fi
        echo "::warning::apt reported success but these are missing: ${missing[*]}"
    fi

    if [ "$i" -eq "$ATTEMPTS" ]; then
        echo "::error::apt failed after ${ATTEMPTS} attempts (stock Ubuntu mirror)"
        exit 1
    fi

    echo "::warning::apt attempt ${i} failed or timed out; retrying in ${RETRY_SLEEP}s"
    repair_dpkg
    sleep "$RETRY_SLEEP"
done
