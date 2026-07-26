#!/usr/bin/env bash
# Build the render harness and run every Multi-Synth core gate.
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
echo "########## preset audit ##########"
# preset_render is built by the cmake step above (target in tests/CMakeLists.txt).
python3 presets/preset_audit.py || fail=1

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
