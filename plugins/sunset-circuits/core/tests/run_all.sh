#!/usr/bin/env bash
# Build the render harness and run every Multi-Synth core gate.
#
# ---------------------------------------------------------------------------
# KNOWN COVERAGE GAPS -- read this before assuming "the suite is green" means
# "the change is safe". These areas have NO automated gate; a regression in any
# of them reaches a user unless someone listens for it. Listed roughly in the
# order they would bite.
#
#   * Effects chain parameter coverage. zipper_gate only proves smoothing does
#     not click, and fx_alias_gate is report-only and mostly about aliasing.
#     Nothing asserts what drive / tape / chorus / delay / reverb parameters
#     actually DO to the signal, or that their ranges behave monotonically.
#     (reverb_gate covers the reverb alone.)
#   * Mod matrix routing. No gate walks source x destination and confirms the
#     modulation lands on the right target with the right depth and polarity,
#     or that an unrouted slot is inert.
#   * Unison spread. steal_gate covers unisonVoices only as far as the poly
#     budget (effectivePoly = min(modeVoices, 16 / unisonCount)); unisonDetune
#     and unisonSpread are set but never measured, so the detune amount and the
#     stereo placement of unison voices are unverified. cpu_bench runs 8x unison
#     for cost only.
#   * Stereo width. No gate measures the width control's effect on correlation,
#     mono compatibility, or that width=0 is genuinely mono.
#   * Portamento / glide at the synth level. acid/slide_gate covers the Acid
#     engine's slide only -- the other five modes' portamento is untested.
#   * Velocity curve. Nothing asserts the velocity-to-level (or velocity-to-
#     anything) mapping, so a curve inversion or a dead top/bottom would pass.
#
# These are known omissions, not oversights -- they were scoped out of the work
# that wired this suite into CI. If you are adding one of these gates, add it to
# the loop below and delete its bullet.
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"
cmake -B build -GNinja >/dev/null 2>&1 || cmake -B build >/dev/null
cmake --build build >/dev/null
echo "== render_test built =="

fail=0
for g in pitch env reverb arp lfo_sync acid stuck sustain polyat steal zipper user_preset; do
    echo
    echo "########## ${g}_gate ##########"
    python3 "${g}_gate.py" || fail=1
done

echo
echo "########## fm suite ##########"
(cd fm && ./run_all.sh) || fail=1

echo
echo "########## acid suite ##########"
# Standalone Acid-engine harness (acid_test.cpp): slope/scream/accent/slide/seq.
# Self-contained build, same as the fm suite; the acid_gate.py above is the
# whole-synth counterpart that goes through render_test.
(cd acid && ./run_all.sh) || fail=1

echo
echo "########## preset audit ##########"
# preset_render is built by the cmake step above (target in tests/CMakeLists.txt).
python3 presets/preset_audit.py || fail=1

echo
echo "########## lv2_smoke (LV2 host integration) ##########"
# The only gate that exercises the shipped plugin instead of the core: a minimal
# lilv host instantiates the BUILT LV2 bundle, sweeps oversampling against the
# reported-latency output port, injects a MIDI note-on, and injects in-range and
# out-of-range MIDI program changes. Exits 6 on a failed check.
#
# Two optional dependencies, both skipped loudly rather than failed: lilv at
# configure time (see the tests CMakeLists) and a locally built LV2 bundle.
lv2_bundle_dir="../../dpf-plugin/build/bin"
if [ ! -x build/lv2_smoke ]; then
    echo "SKIP: lv2_smoke was not built (lilv-0 missing at cmake time; apt install liblilv-dev)"
elif [ ! -d "$lv2_bundle_dir/sunset_circuits.lv2" ]; then
    echo "SKIP: no LV2 bundle at $lv2_bundle_dir/sunset_circuits.lv2"
    echo "      build it with: (cd ../../dpf-plugin && cmake --build build --target sunset_circuits-lv2)"
else
    # Point LV2_PATH at a directory holding ONLY the bundle: dpf-plugin/build/bin
    # also contains the .vst3 and .clap, and lilv logs errors trying to read a
    # manifest.ttl out of each of them.
    lv2_scan_dir="build/lv2_scan"
    rm -rf "$lv2_scan_dir"
    mkdir -p "$lv2_scan_dir"
    ln -s "$(cd "$lv2_bundle_dir/sunset_circuits.lv2" && pwd)" "$lv2_scan_dir/sunset_circuits.lv2"
    # A renamed/moved plugin URI makes lv2_smoke exit 2 (plugin not found), which
    # is a real failure here, not a skip -- the bundle exists but does not expose
    # the URI we ship.
    LV2_PATH="$(cd "$lv2_scan_dir" && pwd)" ./build/lv2_smoke \
        "https://dusk-audio.github.io/plugins/sunset-circuits" || fail=1
fi

echo
echo "########## cpu_bench sanity ##########"
# Gross-regression guard, NOT a performance target. cpu_bench is built by the
# cmake step above and always exits 0, so its table is parsed here.
#
# EXCLUDED FROM THE BAR: scenarios (e) "retire edge" and (f) "CONTROL steady 16
# FM banks". Both run 16 Prism FM operator banks at 4x oversampling and sit on a
# known, pre-existing CPU wall -- 97% and 99% of real time on the dev box this
# bar was calibrated on. Gating them would fail on any machine slower than that
# box, so they are still run and printed (a regression there remains visible in
# the log) but they are not gated. Fix the wall, then gate them.
#
# Everything else is gated at SC_CPU_MAX_PCT (default 250% of real time). The
# worst gated scenario, (a) 8-voice Prism FM at 4x, measures ~56% on the dev
# box, so the ceiling tolerates a runner roughly 4x slower while still catching
# what this step exists to catch: a per-sample allocation, a lost early-out, or
# oversampling running when it should not. Override with SC_CPU_MAX_PCT=<n>;
# skip entirely with SC_SKIP_CPU_BENCH=1.
if [ "${SC_SKIP_CPU_BENCH:-0}" = "1" ]; then
    echo "(skipped: SC_SKIP_CPU_BENCH=1)"
else
    cpu_out=$(./build/cpu_bench all 3) || { echo "cpu_bench failed to run"; fail=1; }
    echo "$cpu_out"
    echo "$cpu_out" | awk -v max="${SC_CPU_MAX_PCT:-250}" '
        $1 ~ /^\([a-f]\)$/ {
            pct = $(NF-2); sub(/%$/, "", pct); pct += 0
            if ($1 == "(e)" || $1 == "(f)") {
                printf "  %s %6.2f%%rt   ungated (known 16-bank 4x CPU wall)\n", $1, pct
                next
            }
            n++
            if (pct > max+0) { printf "  %s %6.2f%%rt   EXCEEDS the %s%% ceiling\n", $1, pct, max; bad++ }
            else             { printf "  %s %6.2f%%rt   ok (ceiling %s%%)\n", $1, pct, max }
        }
        END {
            if (n < 4) {
                print "cpu_bench: parsed " n+0 " gated scenarios, expected 4 -- table format changed?"
                exit 1
            }
            if (bad) { print "cpu_bench sanity: FAIL"; exit 1 }
            print "cpu_bench sanity: PASS"
        }' || fail=1
fi

echo
echo "########## alias_gate (report only) ##########"
python3 alias_gate.py || echo "alias_gate exited nonzero (report-only, not fatal)"

echo
echo "########## fx_alias_gate (report only) ##########"
# Effects-chain counterpart to alias_gate: the drive/tape nonlinearities run at
# host rate, downstream of the voice decimation, so voice oversampling cannot
# reach them. Report only, same as alias_gate.
#
# Sections 0-2 by default (~30 s). Section 3's 54-preset sweep costs 108 extra
# preset_render subprocesses plus ~1500 8x resamples, which makes it the single
# slowest item in this suite; it only changes answer when the factory presets
# change, so it is opt-in. FX_ALIAS_FULL=1 ./run_all.sh runs it.
if [ "${FX_ALIAS_FULL:-0}" = "1" ]; then
    python3 fx_alias_gate.py || echo "fx_alias_gate exited nonzero (report-only, not fatal)"
else
    python3 fx_alias_gate.py --no-presets || echo "fx_alias_gate exited nonzero (report-only, not fatal)"
    echo "(section 3, the 54-preset sweep, was skipped: re-run with FX_ALIAS_FULL=1)"
fi

echo
if [ "$fail" -eq 0 ]; then echo "ALL PASS/FAIL GATES GREEN"; else echo "SOME GATES FAILED"; fi
exit $fail
