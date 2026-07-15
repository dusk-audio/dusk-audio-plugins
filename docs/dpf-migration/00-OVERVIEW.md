# DPF Migration — Master Playbook

**Read this document first, in full, before executing any per-plugin handoff
prompt in this directory.** Every per-plugin document assumes you know
everything in here.

## Mission

Convert the Dusk Audio JUCE plugins to DPF (DISTRHO Plugin Framework) with
Dear ImGui UIs, following the completed **Tape Echo** migration as the proven
template. Tape Echo lives at:

- `plugins/tape-echo/core/` — framework-free C++17 DSP (`TapeEchoDSP.hpp/cpp`)
- `plugins/tape-echo/dpf-plugin/` — DPF shell + ImGui UI + CMake
- branch `tape-echo/dpf-core` — full commit history of the migration; read the
  commit messages, they document every pitfall discovered

Study those files before writing anything. They are the answer key.

## Architecture contract (non-negotiable)

1. **Framework-free DSP core**: one class, pure C++17, zero JUCE/DPF includes.
   API: `prepare(sampleRate, maxBlock)`, `reset()`,
   `processBlock(const float* const* in, float* const* out, nCh, nSamples)`,
   thread-safe atomic setters (`std::memory_order_relaxed`), per-sample
   `SmoothedValue` smoothing inside. In-place processing supported.
   `numSamples <= 0` early-out. No allocation/locks/IO in processBlock
   (CLAUDE.md audio-thread rules apply unchanged).
2. **Thin DPF shell** (`<Name>Plugin.cpp`): param table in `initParameter`,
   atomic forwarding in `setParameterValue`, `activate()` → `dsp.prepare()`.
3. **ImGui UI** (`<Name>UI.cpp`): custom ImDrawList rendering in a fixed
   design space, uniformly scaled. No stock ImGui widgets except combos.
4. **Offline validation harness**: every DSP behavior gets a rendered
   measurement before the port is declared done.

## Reusable building blocks (extract per 01-shared-dpf-extraction.md)

Already written and validated inside tape-echo — reuse, do not rewrite:

| Component | Where (today) | Replaces |
|---|---|---|
| `SmoothedValue` (one-pole) | `core/TapeEchoDSP.hpp` | `juce::LinearSmoothedValue` |
| `OnePoleLP/HP`, `DCBlocker` | same | misc JUCE filters |
| `ShelfFilter` (RBJ biquad TDF2) | same | `juce::dsp::IIR` shelves/peaks (extend with peak/HP/LP coefficient functions as needed) |
| `HalfbandFIR` + 4x oversampling pattern | `core/TapeEchoDSP.{hpp,cpp}` | `juce::dsp::Oversampling` |
| `ScopedFlushDenormals` (SSE **and ARM64 FPCR**) | `core/TapeEchoDSP.cpp` | `juce::ScopedNoDenormals` |
| Chrome knob / LED / VU / toggle / preset combo / crisp font loader | `dpf-plugin/TapeEchoUI.cpp` | LookAndFeel classes |
| Weak-symbol meter bridge | `dpf-plugin/TapeEchoAccess.hpp` + `TapeEchoPlugin.cpp` | editor timers reading the processor |
| Factory-preset table + host programs | `dpf-plugin/TapeEchoParams.hpp` + shell | preset headers |
| CMake template | `dpf-plugin/CMakeLists.txt` | JUCE CMake |

DPF checkout: `~/projects/DPF` (needs `git submodule update --init` for pugl).
DPF-Widgets checkout: `~/projects/DPF-Widgets` (DearImGui wrapper).

## Landmines (all hit and fixed during tape-echo — do not rediscover them)

1. **LV2 + direct access = MONOLITHIC.** `DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1`
   makes DPF's LV2 TTL export declare the UI inside the dsp binary
   (`DistrhoPluginLV2export.cpp:70`). You MUST pass `MONOLITHIC` to
   `dpf_add_plugin` or LV2 hosts refuse to load the plugin.
2. **CLAP never forwards output parameters to the UI.** VST3 does, CLAP does
   not, LV2 uses port events. For meters: read the DSP atomic via
   `getPluginInstancePointer()` through a weak-linked bridge function
   (see `TapeEchoAccess.hpp` — weak so the split LV2 UI links; with
   MONOLITHIC it resolves everywhere anyway). Keep the output parameter too
   as fallback for generic UIs.
3. **VST3 bundle rename**: the inner `.so` must match the bundle name
   (`Foo.vst3/Contents/x86_64-linux/Foo.so`) or hosts find zero plugins.
4. **ImGui font atlas is rasterized at 13 px.** Text drawn at other sizes
   blurs. Load a real bold TTF at `30 * getScaleFactor()` in the UI
   constructor with a fallback path list (see `TapeEchoUI.cpp` constructor).
   Never fake bold with double-draw on the real font.
5. **ARM64 denormals**: the SSE FTZ guard is a no-op on Apple Silicon; the
   shared `ScopedFlushDenormals` already handles `__aarch64__` via FPCR.
   Use it, don't write a new one.
6. **Oversampling latency compensation**: subtract fixed group delay from
   each delay-line read AFTER any ratio scaling, never from the base time
   (tape-echo commit `9b0f6b5` documents the bug this caused).
7. **Bypass**: implement as a host-designated parameter
   (`p.initDesignation(kParameterDesignationBypass)`), crossfaded ~30 ms in
   the DSP to a bit-exact passthrough. Verify `max|out-in| == 0` when
   bypassed and settled.
8. **Aliasing**: any nonlinearity fed full-band input must be oversampled
   (4x via two cascaded halfbands; taps in `TapeEchoDSP.cpp`, designed with
   scipy — never invent filter coefficients from memory). Nonlinearities fed
   band-limited signal may run at base rate; measure to justify.
9. **pluginval timeout**: use `--timeout-ms 120000`; the 30 s default
   fails spuriously under load.
10. **Do not use pedalboard for anything, ever** (project rule).

## Validation methodology (required for every port)

1. **A/B against the JUCE build** — the killer step. The hosted render tool
   `~/projects/plugins/build/tests/duskverb_render/duskverb_render` loads any
   VST3 (`--vst3 <path> --param "Name=value" --input-wav ... --output-dir ...`).
   Render the existing JUCE VST3 and your DPF VST3 with identical input and
   matched parameters; compare in Python (numpy/scipy/soundfile available).
   Where DSP translates 1:1, target near-null; otherwise write per-behavior
   spectral gates and justify every deviation in the commit message.
   Volume-match before comparing spectra. Write WAVs as float
   (`subtype='FLOAT'`), never PCM16.
2. **pluginval** strictness 8 on the installed VST3 — must pass.
3. **LV2 host contract**: lv2ls discovery + a real instantiation test
   (`lv2apply` cannot host atom-port plugins; a minimal lilv host with
   urid:map + options + atom buffers is required — one exists in scratch
   history; rewrite from tape-echo commit `dcc2934`'s description if needed).
4. **UI interaction sweep under Xvfb**: launch the JACK standalone on a
   virtual display, drag every control with xdotool, screenshot, verify
   value readouts and direction. Synthetic input does not work on the
   Wayland desktop itself — always use Xvfb (`Xvfb :99`, `DISPLAY=:99`).
5. **DSP unit suite**: per-plugin offline renders + measurements for every
   behavior the plugin claims (see `tests/` harness patterns and the
   tape-echo validation history).

## Install & build commands

```bash
cd plugins/<name>/dpf-plugin
cmake -B build -GNinja && cmake --build build
# install (adjust names)
cp build/bin/<name>.clap ~/.clap/<Name>.clap
cp -r build/bin/<name>.lv2 ~/.lv2/<Name>.lv2
cp -r build/bin/<name>.vst3 ~/.vst3/<Name>.vst3
mv ~/.vst3/<Name>.vst3/Contents/x86_64-linux/<name>.so \
   ~/.vst3/<Name>.vst3/Contents/x86_64-linux/<Name>.so   # names must match
pluginval --strictness-level 8 --timeout-ms 120000 --validate ~/.vst3/<Name>.vst3
```

## Process rules

- One branch per plugin: `<slug>/dpf-core`. Commit granularly with detailed
  messages (follow the tape-echo branch's style). No Claude co-author
  trailers. Do not push without being asked.
- The JUCE version stays installed and buildable — DPF versions are new
  plugin IDs; users' sessions need the old ones. Never delete JUCE targets.
- **Naming policy (decided)**: DPF builds ship as versioned successors of
  the same products — display name "<Name> 2" (e.g. "Multi-Comp 2",
  "DuskVerb 2"), brand "Dusk Audio". Directory slugs and class names stay
  unversioned. IDs (VST3 4-char code, CLAP id, LV2 URI) must NOT collide
  with the JUCE versions' (check each JUCE CMakeLists for `PLUGIN_CODE` and
  `LV2URI`); distinct IDs are what actually protect existing sessions — the
  "2" exists so humans can tell them apart in plugin menus. JUCE versions
  stay installed and buildable, maintenance-only, retired from the website's
  featured list only after the v2 is proven. Everything lives in THIS repo
  (the A/B validation renders JUCE and DPF builds from the same commit).
  Note: the completed tape-echo DPF port still displays plain "Tape Echo" —
  rename its DISTRHO_PLUGIN_NAME to "Tape Echo 2" as a first task.
- No third-party trademarks anywhere in names, strings, UI, or docs
  (no Roland/RE-201/British/Neve/Pultec/tracking deck etc. — describe hardware
  generically).
- The user's ear is final sign-off. Self-verify everything measurable first;
  hand off renders/screenshots, never claims.

## Recommended order

1. `01-shared-dpf-extraction.md` (prerequisite for everything)
2. `02-4k-eq.md` (pilot: simplest audio FX)
3. `03-tapemachine.md`
4. `04-multi-q.md`
5. `05-multi-comp.md`
6. `06-chord-analyzer.md`
7. `07-duskverb.md`
8. `08-convolution-reverb.md` (hardest; needs conv engine replacement)
