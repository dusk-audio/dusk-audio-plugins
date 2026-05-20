# DuskVerb Hall Calibration — Mac Handoff Prompt

Paste the block below into the first Claude session on the Mac:

---

You're continuing work on DuskVerb Hall reverb calibration. Read these
files in order before touching anything:

1. `MAC_SESSION_CONTEXT.md` (repo root) — full architecture state +
   active todo. Read this first.
2. `plugins/DuskVerb/tools/tuner_configs/hall_med_hall_vs_lex.yaml` —
   the current 10/19 P11 baseline calibration.
3. `plugins/DuskVerb/tools/targets/lex_med_hall.json` — frozen Lex
   anchor metrics (Mac-safe; no Lex VST2 required).

The Lex VST2 plugins are Windows-only and unavailable on this Mac.
The optimizer and graders consume static JSON snapshots in
`plugins/DuskVerb/tools/targets/` — never try to re-render the
reference live.

**Active task:** tune `HybridHallReverb` (algo 12 "Hall (Hybrid)") to
beat P11's 10/19 ceiling. Kick off with:

```bash
cd build && cmake .. && cmake --build . --target DuskVerb_VST3 duskverb_render -j8
cd ..
python3 plugins/DuskVerb/tools/tuner/hybrid_optuna.py \
    --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json \
    --trials 300 --workers 6
```

Validate any winning config via:

```bash
python3 plugins/DuskVerb/tools/tuner/hall_iter.py \
    --param "Algorithm=Hall (Hybrid)" \
    --param "Hybrid Macro Mix=<value>" ... \
    --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json
```

**Constraints (mandatory):**

- 19/19 mandate active — no shipping at lower counts.
- Volume-first rule — re-trim Gain Trim to bring `a_weighted_rms_db`
  within JND before judging other metrics.
- 100% wet renders only (Bus Mode=on + Dry/Wet=1.0).
- No Co-Authored-By Claude trailer in commits.
- No em dashes in plugin manuals.
- Don't try to live-render Lex anchors on this machine — there is no
  yabridge or Windows VST2 support.
- HybridHallReverb uses INDEPENDENT `er_level` + `ring_level` (Fix B).
  Don't reintroduce the old `macro_mix` zero-sum crossfade.

If Hybrid plateaus below P11's 10/19, the next path is the
`ConcertHall` anchor (`targets/lex_concert_hall.json`) which may map
better to the Ring or Hybrid topology. Document the Pareto observations
and move on.

---

End of handoff.
