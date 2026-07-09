# Handoff: Convolution Reverb → DPF (hardest — do last)

> Prompt for the executing agent: Read `docs/dpf-migration/00-OVERVIEW.md`
> first. Execute on branch `convolution-reverb/dpf-core`. Requires
> shared-dpf and experience from at least two prior ports.

## Source inventory

`plugins/convolution-reverb/`:
- `PluginProcessor.*` — hosts 6× `juce::dsp::Convolution` (THE dependency),
  5× `juce::dsp::StateVariableTPTFilter` (wet EQ), ProcessSpec/AudioBlock glue.
- `ConvolutionEngine.h` — wrapper around juce Convolution.
- `EnvelopeProcessor.h` — IR envelope shaping (likely plain math — port).
- `WetSignalEQ.h` — wet-path EQ → shared Biquad/TPT SVF.
- `IRBrowser.*`, `IRWaveformDisplay.*` — file browser + waveform UI.
- `AifcStreamWrapper.h` — AIFC loading shim.
- ~27 param mentions.

## The hard parts and their answers

1. **Partitioned convolution engine** (replaces `juce::dsp::Convolution`):
   - Use **FFTConvolver** (github.com/HiFi-LoFi/FFTConvolver, MIT) — small,
     proven, non-uniform partitioned, zero-latency mode. Vendor it into
     `shared-dpf/third_party/`. Alternative: hand-rolled uniform-partition
     engine on pffft — only if FFTConvolver proves unsuitable.
   - True stereo / stereo IR handling: mirror what the JUCE version does
     (check `ConvolutionEngine.h` for channel strategy: mono→stereo,
     stereo→stereo, true-stereo 4-channel).
   - Latency: zero-latency partitioning or report block latency via DPF —
     match the JUCE build's reported latency behavior.
2. **IR file loading** (replaces juce audio format readers):
   - dr_wav + dr_flac (public domain single-headers) for WAV and FLAC. dr_wav
     has no AIFF/AIFC support. If AIFC must be kept, port `AifcStreamWrapper`
     logic over libsndfile instead (libsndfile is a system dependency on Linux;
     for Windows/macOS bundles, vendor it or drop AIFC — decide with the user).
   - Sample-rate conversion of IRs: use r8brain-free (or libsamplerate) —
     offline quality matters, this is not the audio thread.
3. **Async IR loading** (CLAUDE.md pattern applies): load/resample on a
   worker thread; never allocate or load on the audio thread. DPF has no
   MessageManager — use a std::thread. Ownership/reclamation must be
   unambiguous so `processBlock` can never touch an object the worker frees:
   Use ONE scheme — single-owner raw pointer, published atomically, read
   LOCK-FREE by processBlock (no try-lock in the audio path):
   - processBlock does an ACQUIRE load of the active engine pointer and always
     processes with whatever it loaded. It never takes a lock and never clears
     the output just because a load is in progress — a swap in flight still
     yields a valid engine (the old one until the store lands, the new one
     after), so there is no dropout. Output is silent ONLY when the pointer is
     null (no IR ever loaded). **Do not** gate processBlock on a `try-lock` that
     zeroes the buffer on a miss: under contention that silences whole blocks
     (audible dropouts every time an IR loads).
   - The worker prepares the new object entirely off-thread, then publishes it
     with a RELEASE store / `exchange` into the active pointer — that store is
     the instant the new engine goes live.
   - Reclaiming the OLD engine is the only part that needs care: after the swap
     the worker must not free the old object until no processBlock can still be
     inside it. Gate the delete on a reader handshake — a generation/epoch
     counter, or an "audio-thread in-use" flag the worker spins on OFF-THREAD
     (it may block) until a full processBlock has elapsed. Only then delete;
     processBlock never deletes. A bare `exchange` alone is NOT enough — it
     publishes the new pointer but says nothing about when the old one is safe
     to free.
   - A `shared_ptr<Engine>` is a valid *alternative*, kept C++17-safe: publish
     the active engine as a `std::shared_ptr` and have processBlock take its own
     copy with the free-function `std::atomic_load(&activePtr)` (the C++11/17
     shared_ptr atomic overloads — NOT C++20 `std::atomic<std::shared_ptr>`,
     which this codebase can't use); the worker publishes with
     `std::atomic_store(&activePtr, newEngine)`. The last reader dropping its
     copy frees the engine, so reclaim is automatic — no epoch handshake needed.
     **Real-time caveat:** "automatic" reclaim means the final `shared_ptr`
     release runs the engine *destructor* — a `free()`, and for a convolver a
     large one — wherever the last copy lives. If that is `processBlock` (it
     holds the only copy the instant after a swap, until the worker's own copy
     is dropped), the destructor runs ON THE AUDIO THREAD. To keep reclaim off
     the audio thread, either (a) prefer the raw-pointer + epoch scheme above,
     or (b) have the loader thread keep its `shared_ptr` to the OLD engine and
     release it only after an audio-thread quiescence handshake (a full
     processBlock has elapsed since the `atomic_store` swap), so the final
     release — and the destructor — is guaranteed to run on the loader thread,
     never in `processBlock`.
     (If those overloads are unavailable on the toolchain, fall back to the
     raw-pointer + epoch scheme above.) Pick one model or the other, never a
     blend. (A SpinLock, if used at all, serializes only the worker's swap +
     reclaim handshake — never the processBlock audio read.)
   - **Worker teardown / join:** the loader thread must be joinable and joined
     before the plugin instance is destroyed. Hold a `std::atomic<bool> stop`
     the worker checks between load stages, plus a condition variable / counting
     semaphore to wake it when it is idle-waiting for a request. In the plugin
     destructor (and in `deactivate()` if a load is in flight and the DSP is
     stopping), set `stop`, signal the CV, then `join()`. Only after the join
     returns is it safe to free the active engine and any pending/old objects —
     no other thread can still publish or dereference them. Never `detach()` the
     worker: a detached loader can wake up and call back into a half-destroyed
     plugin (swap a pointer, touch the SpinLock) after the object is gone.
4. **State**: IR file path must persist → `DISTRHO_PLUGIN_WANT_STATE 1` +
   `initState/setState` (first use of DPF state in the fleet — this port
   defines the pattern). File browser: DPF has a native file-request API
   (`requestStateFile`/UI file browser support with USE_FILE_BROWSER) —
   evaluate it vs an ImGui browser; prefer DPF's host-integrated dialog.
5. **IR waveform display**: decimated min/max envelope of the loaded IR,
   pushed to UI via direct-access bridge (pointer to an immutable snapshot).

## Validation

- [ ] **Null vs JUCE build**: same IR, same settings → render both through
      `duskverb_render`; convolution output should null to ≤ −100 dB after
      latency alignment (both engines are linear; differences are windowing/
      partitioning edges). Test at 44.1/48/96k, mono and stereo IRs, IR
      sample rates ≠ session rate (exercises the resampler).
- [ ] IR swap under playback: no clicks/dropouts (record output while
      swapping IRs every 500 ms).
- [ ] Envelope/EQ params A/B vs JUCE build.
- [ ] State round-trip: save/reload session → same IR + params (pluginval
      covers part; add an explicit host-cycle test).
- [ ] pluginval strictness 8; LV2 instantiation (state + worker threads are
      the risk areas); Xvfb sweep incl. loading an IR from disk.

## Warnings

- This port defines three fleet-first patterns (state files, worker-thread
  resource loading, vendored third-party DSP). Get each reviewed against
  the overview's landmine list before building on top.
- License hygiene: FFTConvolver (MIT), dr_libs (PD), r8brain (MIT) — keep
  a `third_party/LICENSES` file.
