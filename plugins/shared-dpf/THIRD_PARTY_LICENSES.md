# Third-Party Licenses — Dusk Audio DPF plugins

The Dusk Audio plugins built on the DISTRHO Plugin Framework (everything under
`plugins/shared-dpf/` and each plugin's `dpf-plugin/` directory) are distributed
under the **GNU General Public License v3.0 or later** (see the repository
[`LICENSE`](../../LICENSE)).

These plugins incorporate the third-party components listed below. Each is under
a permissive license that is compatible with the GPLv3, and its copyright and
permission notice is reproduced here as those licenses require. Per the DPF
[LICENSING.md](https://github.com/DISTRHO/DPF/blob/main/LICENSING.md), **DPF
itself must be attributed regardless of the plugin format built.**

---

## DISTRHO Plugin Framework (DPF) — ISC

Includes DGL, the VST3 and AudioUnit "travesty" backends, and the LV2 support
code. Copyright (C) 2012-2025 Filipe Coelho <falktx@falktx.com>.

> Permission to use, copy, modify, and/or distribute this software for any
> purpose with or without fee is hereby granted, provided that the above
> copyright notice and this permission notice appear in all copies.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
> REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
> AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
> INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
> LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
> OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
> PERFORMANCE OF THIS SOFTWARE.

## pugl (bundled inside DGL) — ISC

Copyright 2011-2022 David Robillard <d@drobilla.net> (ISC, same terms as above).

## Dear ImGui (via DPF-Widgets `opengl/DearImGui`) — MIT

Copyright (c) 2014-2025 Omar Cornut.

> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in
> the Software without restriction, including without limitation the rights to
> use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
> of the Software, and to permit persons to whom the Software is furnished to do
> so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## CLAP (clap-audio) — MIT

Used only by the CLAP build. Copyright (c) 2014-2022 Alexandre Bique (MIT, same
terms as Dear ImGui above).

## RtAudio / RtMidi — MIT

Used only by the JACK / standalone build. Copyright (c) 2001-2021 Gary P.
Scavone (MIT-style; the above-copyright-notice clause applies).

---

## Per-format attribution (from DPF LICENSING.md)

| Format          | License(s)             | Attribution |
|-----------------|------------------------|-------------|
| JACK / Standalone | MIT (RtAudio, RtMidi) | Gary P. Scavone |
| LV2             | ISC                    | Steve Harris, David Robillard, et al. |
| VST3            | ISC                    | DPF files (Filipe Coelho) |
| CLAP            | MIT                    | Alexandre Bique |
| AU              | ISC                    | DPF files (Filipe Coelho) |

The plugins in this repository build the CLAP, VST3, LV2, JACK-standalone (and,
on macOS, AudioUnit) formats. VST2, LADSPA and DSSI are not built.

## Dusk Audio plugin code

All original Dusk Audio DSP, UI and shell code in this repository is
Copyright (C) 2026 Dusk Audio and is licensed under the GNU GPL v3.0 or later.
