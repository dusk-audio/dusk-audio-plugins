# Dusk Audio Plugin Manuals — House Style

Source markdown for the user-facing PDF manual. The generator at `build_manuals.py` consumes every `*.md` in this directory (excluding underscore-prefixed files like `_template.md`) and produces `dusk-audio-manual.pdf` (combined) plus `<slug>-manual.pdf` per plugin.

## Hard rules

1. **No em dashes** (`—`, U+2014). Also no bare ` -- ` ASCII surrogates in prose (LaTeX would render those as en dashes). Use commas, semicolons, parentheses, or break the sentence in two. The preflight script `preflight.py` rejects any em dash and exits non-zero before pandoc runs.
2. **Voice is engineer-to-engineer**. Say "you'll want to drop the threshold to..." rather than "the user should adjust the threshold to...". Friendly, direct, second-person, present tense.
3. **No marketing copy**. The website's `_plugins/*.md` files are marketing. The manual is how-to. Skip the "legendary sound", "completely free", "professional grade" language; describe what the user does and what they hear.
4. **Show, don't list**. A worked example with five concrete knob settings beats a parameter list with hand-wavy adjectives. Prefer "Threshold around -18 dB, ratio 4:1, attack 10 ms, release 80 ms" over "set the compression to medium".

## Chapter structure

Every plugin chapter has the same seven sections in the same order. Word counts are targets, not limits, but a chapter that doubles a target probably needs cutting.

1. **Overview** (~150 words). What the plugin does. Where it shines. Where it does not. One paragraph or two; do not turn this into a feature list.
2. **Quick Start** (~200 words). First-time use. Install pointer (link to website install page rather than duplicating the platform table here), the three knobs to learn first, what the user should hear.
3. **Workflows** (~600 to 900 words). Three to five concrete worked examples. Each H3 heading is just the descriptive name of the task (e.g. "Lead vocal with Vintage Opto"); no "Recipe N," prefix. Each entry has: source material (e.g., "lead vocal recorded at -18 dBFS RMS"), target outcome (e.g., "consistent level with audible squeeze on plosives"), exact knob settings, and a one-sentence why. Pull ideas from the plugin's strengths (Multi-Comp's multiband for de-essing, Multi-Q's Match mode for tonal matching, etc.).
4. **Parameter Reference** (~400 to 600 words). Every user-facing parameter, grouped by panel section. For each: range, default, audible effect, when to reach for it, common mistake. Skip internal parameters (smoothing rates, debug flags). Source ranges and defaults from the C++ parameter-layout function; do not guess.
5. **Tips and Traps** (~200 words). Non-obvious behavior. Mode interactions. CPU costs (oversampling). Routing surprises. Things that bite beginners.
6. **Presets Explained** (~300 to 500 words). Each factory preset: starting point, intended source material, what to tweak first. Source comments and intent from each plugin's `<Plugin>Presets.h` file.
7. **Troubleshooting** (~150 words). The top three "I hear nothing / I hear something wrong" issues per plugin. Plugin-specific gotchas (Chord Analyzer's three variants, Multi-Q's mode persistence, etc.).

## Front matter

Every chapter begins with YAML front matter:

```yaml
---
slug: <plugin-slug>             # Matches _data/plugins.yml entry
version: <X.Y.Z>                # Matches CMakeLists.txt; bumped by /release-plugin
last_updated: <YYYY-MM-DD>      # Bumped by /release-plugin
tagline: <one-line description> # For chapter title page
---
```

The generator reads `version` from `~/projects/dusk-audio.github.io/_data/plugins.yml` at build time and confirms the chapter's front-matter `version` matches. Mismatch fails the build.

## Screenshots

Manual-specific screenshots live in this repo at `manuals/screenshots/<slug>/`. They are committed alongside the prose so a manual change and its image change move together. Each per-plugin folder has its own `README.md` listing the expected file names and what each one should depict.

Reference screenshots in markdown using a path relative to the chapter file:

```markdown
![Vintage Opto mode](screenshots/multi-comp/02-mode-vintage-opto.png)
```

The generator rewrites these paths to absolute disk locations before invoking pandoc; do not include the absolute path in the markdown.

The website's `assets/images/plugins/` tree (marketing screenshots used on the per-plugin web pages) is a separate set and is not touched by this manual workflow. If a website screenshot is the right shot for a manual section, copy it into `manuals/screenshots/<slug>/` rather than referencing the website file across repos.

### Capture guidelines

- Capture at native plugin resolution. 1200 to 1600 px wide is a safe minimum for legible print at 6-inch column width.
- PNG, lossless. Avoid JPEG (compression artefacts on UI text).
- File size budget: under ~500 KB per shot, under ~5 MB total per plugin folder.
- File names follow the per-plugin `screenshots/<slug>/README.md` exactly. The build fails if a referenced file is missing.

## Source-of-truth pointers

When you need to know what a parameter actually does:

| What | Where |
| --- | --- |
| Parameter ranges, defaults, IDs | `plugins/<plugin>/createParameterLayout()` (typically in `<plugin>.cpp`) |
| Factory presets (names, values, intent) | `plugins/<plugin>/<Plugin>Presets.h` |
| DSP behavior (attack curves, filter slopes) | The plugin's per-mode `process()` or equivalent |
| Existing prose to refer to (not copy) | `~/projects/dusk-audio.github.io/_plugins/<slug>.md` |

If a parameter's behavior surprises you while writing, read the source. If the source surprises you, ask the user; do not guess in the manual.

## Generator outputs

- `dusk-audio-manual.pdf` (combined): cover, TOC, then each chapter in order, each with a colored section break sourced from the plugin's brand color.
- `<slug>-manual.pdf` (per-plugin): single chapter, no overall TOC, slimmer cover.

Both land in `~/projects/dusk-audio.github.io/assets/manuals/` and are committed to the website repo by `/release-plugin`.

## Editing checklist before requesting review

- `make preflight` exits clean (no em dashes, all screenshots resolve, version matches).
- Section word counts are within ~20% of the targets above.
- No marketing language; no "the user", only "you".
- Every workflow has concrete numbers, not adjectives.
- Every parameter in the Parameter Reference has all five fields (range, default, audible effect, when to use, common mistake).
