# Notice for in-flight Multi-Comp 2 work: DPF is now DAF

Paste this to Codex mid-task. It supersedes any path, macro, or repository name
in your current brief.

## What happened

While you were working, the framework was renamed and its public API forked.
This landed on `main` as `f7edaea` (PR #223) and CI is green across all four
platforms. **Your working tree almost certainly references paths and macros
that no longer exist.** Before continuing, inspect the working-tree status and
tell the user about any uncommitted work. The user must preserve that work and
select the starting branch themselves: `origin/main` is appropriate for
unrelated work, while Multi-Comp 2 Opto campaign validation requires
`multi-comp-2/opto-campaign-snapshot` and its intended gate set.

## The substitution table

Apply this to every file you have touched and to every path in your brief.

| old | new |
|---|---|
| `github.com/dusk-audio/DPF` | `github.com/dusk-audio/DAF` |
| `github.com/dusk-audio/DPF-Widgets` | `github.com/dusk-audio/DAF-Widgets` |
| `~/projects/DPF`, `~/projects/DPF-Widgets` | `~/projects/DAF`, `~/projects/DAF-Widgets` |
| `../DPF`, `../DPF-Widgets` | `../DAF`, `../DAF-Widgets` |
| `plugins/<name>/dpf-plugin/` | `plugins/<name>/daf-plugin/` |
| `plugins/shared-dpf/` | `plugins/shared-daf/` |
| `PatreonBackersDpf.hpp` | `PatreonBackersDaf.hpp` |
| `DistrhoPluginInfo.h` | `DafPluginInfo.h` |
| `DistrhoPlugin.hpp`, `DistrhoUI.hpp` | `DafPlugin.hpp`, `DafUI.hpp` |
| `#include "distrho/..."` | `#include "daf/..."` |
| `DISTRHO_*` (every macro) | `DAF_*` |
| `START_NAMESPACE_DISTRHO` | `START_NAMESPACE_DAF` |
| `END_NAMESPACE_DISTRHO` | `END_NAMESPACE_DAF` |
| `USE_NAMESPACE_DISTRHO` | `USE_NAMESPACE_DAF` |
| `namespace DISTRHO` | `namespace DAF` |
| `duskdpf::` | `duskdaf::` |
| `dpf_add_plugin(...)` | `daf_add_plugin(...)` |
| `DuskDpfPlugin.cmake` | `DuskDafPlugin.cmake` |
| `-DDPF_PATH`, `-DDPFWIDGETS_PATH` | `-DDAF_PATH`, `-DDAFWIDGETS_PATH` |
| `-DDUSK_DPF_*` | `-DDUSK_DAF_*` |
| `DPF_REF` | `DAF_REF` |
| `DPF_SHA` | `DAF_SHA` |
| `DPFWIDGETS_*` | `DAFWIDGETS_*` |
| `.github/workflows/dpf-*.yml` | `.github/workflows/daf-*.yml` |
| `docker/check_dpf_pins.sh` | `docker/check_daf_pins.sh` |
| `.github/scripts/dpf_clap_validate.py` | `.github/scripts/daf_clap_validate.py` |
| `docs/dpf-migration/` | `docs/daf-migration/` |

## What deliberately did NOT change. Do not "finish the rename" on these.

1. **`d_cconst('D','P','F',' ')` in `daf/src/DafPluginVST3.cpp`** (in the DAF
   repo). Those four characters are hashed into every VST3 class UID the
   framework emits. Changing them to `'D','A','F',' '` rewrites the UID of every
   shipped VST3 and orphans every saved session that references one. The line
   carries a comment saying so. Leave it.
2. **Per-file licence headers.** Every framework file still opens with
   `DISTRHO Plugin Framework (DPF)` above the copyright line and the ISC notice.
   That is required attribution, not a leftover.
3. **Plugin identity.** `DAF_PLUGIN_URI`, `DAF_PLUGIN_CLAP_ID`,
   `DAF_PLUGIN_UNIQUE_ID` and `DAF_PLUGIN_BRAND_ID` values are unchanged
   (`https://dusk-audio.github.io/plugins/...`, `com.duskaudio.*`, `DsMc`,
   `Dusk`). Only the macro names moved. Do not touch the values.
4. **Example plugins' own `"DISTRHO"` brand strings and `distrho.sf.net` URIs**
   inside the DAF repo's `examples/`.

## Hard rule about upstream

DAF and DAF-Widgets are **hard forks**. `DISTRHO/DPF` and `DISTRHO/DPF-Widgets`
are not references: do not read, fetch, diff, cherry-pick, or merge from them,
and do not cite upstream to explain a behaviour here. The `upstream` remote has
been removed from the local checkout. If inherited code has a bug, it is our bug
and it gets fixed here. Attribution lives in README.md and
`plugins/shared-daf/THIRD_PARTY_LICENSES.md` and is not up for revision.

## Your Multi-Comp 2 Opto campaign state moved

The uncommitted Opto work that lived in the `/private/tmp/.../scratchpad/mc2`
worktree (Opto detector changes in `MultiCompModes.hpp`, the extra gates in
`MultiCompCoreTests.cpp`, the knob-splitter UI work) has been ported onto the
renamed tree and pushed:

    branch: multi-comp-2/opto-campaign-snapshot   (off main, f7edaea)

**The user-selected campaign base must be that snapshot, not `main` or the old
worktree.** `main` does not contain the extra Opto gates; measurements from it
use a smaller gate set and are not comparable to the campaign log. The old
worktree is now stale: two of its five files sit at deleted paths.

The port is verified faithful. Every gate reports the same numbers as before
the rename, to the digit: crest sweep -0.141 / -0.540 / +0.615 held out /
-0.709 dB, burst rate fitted RMS 0.561 dB, detector weighting shape RMS
0.097 dB, short-event release worst 1.386 dB.

## Commands, updated

Core suite (fast, this is the one that carries the Opto gates):

    c++ -O2 -std=c++17 -Iplugins/multi-comp/core -Iplugins/shared-daf/dsp \
      plugins/multi-comp/core/tests/MultiCompCoreTests.cpp \
      plugins/multi-comp/core/MultiCompDSP.cpp -o /tmp/mc2tests && /tmp/mc2tests

Full plugin build plus both ctest targets:

    cmake -U 'SDL2*' -U 'pkgcfg_lib_SDL2*' \
      -S plugins/multi-comp/daf-plugin -B build-mc2 \
      -DCMAKE_BUILD_TYPE=Release -DDAF_PATH=$HOME/projects/DAF \
      -DDAFWIDGETS_PATH=$HOME/projects/DAF-Widgets -DDUSK_DAF_INSTALL_LOCAL=OFF \
      -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
    cmake --build build-mc2 -j8 && ctest --test-dir build-mc2 --output-on-failure

Linux parity check, still mandatory before declaring a DSP change done:

    podman run --rm --arch amd64 -v "$PWD/plugins:/src/plugins:ro" \
      docker.io/library/gcc:12 bash -c \
      'g++ -O3 -std=c++17 -I/src/plugins/multi-comp/core \
       -I/src/plugins/shared-daf/dsp \
       /src/plugins/multi-comp/core/tests/MultiCompCoreTests.cpp \
       /src/plugins/multi-comp/core/MultiCompDSP.cpp -o /tmp/t && /tmp/t'

Framework pins are `DAF 788eb019` and `DAF-Widgets 91e0004e`. Confirm your
checkouts match with `./docker/check_daf_pins.sh` (exits 0 when they do).

## One trap worth knowing

A blanket search-and-replace over this rename silently disarmed two guard
scripts, because their whole job was to match the old name:

- `check_fork_sources.sh` grepped for `DISTRHO/(DPF|DPF-Widgets|pugl)`; the
  rename rewrote that to `DISTRHO/(DAF|...)`, which matches nothing upstream, so
  the check that stops a build fetching from DISTRHO would have passed on a real
  leak.
- A stale CMake include path, `shared-dpf/dsp`, survived inside a quoted string
  in `target_include_directories`. CMake does not validate include directories,
  so nothing complains at configure time; the directory is simply absent from
  the compiler's header search path, and the first symptom is a header that
  fails to resolve when that target compiles. It was caught by grepping for the
  old token, not by a build.

If you run any bulk substitution, grep afterwards for the old tokens — but read
the hits against the allowlist above before changing any of them. The DPF tokens
listed under "What deliberately did NOT change" are supposed to still be there:
`d_cconst('D','P','F',' ')`, the per-file `DISTRHO Plugin Framework (DPF)`
licence headers, and the examples' own `"DISTRHO"` brand strings and
`distrho.sf.net` URIs. "Finishing" those rewrites every shipped VST3's class UID
and strips required attribution. Treat only hits outside that allowlist as
leftovers to fix.

Then re-run `./docker/check_daf_pins.sh` and
`./.github/scripts/check_fork_sources.sh ~/projects/DAF`. Both must exit 0.
