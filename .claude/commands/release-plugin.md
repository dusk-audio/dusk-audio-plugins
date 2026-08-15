# Release Plugin

Release one or more Dusk Audio plugins with automated version bumps, website updates, tagging, and push.

## Usage

```
/release-plugin <plugin-name> [version]
/release-plugin <plugin1> <plugin2> ...    # Batch: auto-increment patch for each
```

**Arguments:**
- `plugin-name`: Plugin slug (e.g., `multi-comp`, `4k-eq`, `tapemachine`)
- `version` (optional): Explicit version (e.g., `1.2.0`). Omit to auto-increment patch.
- Multiple plugin names: Batch release with patch bumps for all listed plugins.

**Examples:**
- `/release-plugin multi-comp 1.2.0` - Release Multi-Comp v1.2.0
- `/release-plugin 4k-eq` - Release 4K EQ with auto-incremented patch
- `/release-plugin 4k-eq multi-comp tapemachine multi-q` - Batch patch bump all four

## Plugin Slug Mapping

| Plugin Name | Slug | Directory | CMake Var | Build Shortcut |
|-------------|------|-----------|-----------|----------------|
| 4K EQ | 4k-eq | plugins/4k-eq | FOURKEQ | 4keq |
| Multi-Comp | multi-comp | plugins/multi-comp | MULTICOMP | compressor |
| TapeMachine | tapemachine | plugins/TapeMachine | TAPEMACHINE | tape |
| TapeMachine 2 | tapemachine-2 | plugins/TapeMachine/dpf-plugin | (inline: TapeMachine2DPF) | tape |
| 4K EQ 2 | 4k-eq-2 | plugins/4k-eq/dpf-plugin | (inline: FourKEQ2DPF) | 4keq |
| Tape Echo | tape-echo | plugins/tape-echo | (inline: TapeEcho) | tapeecho |
| Tape Echo 2 | tape-echo-2 | plugins/tape-echo/dpf-plugin | (inline: TapeEchoDPF) | tapeecho |
| Multi-Q | multi-q | plugins/multi-q | MULTIQ | multiq |
| Multi-Q 2 | multi-q-2 | plugins/multi-q/dpf-plugin | (inline: MultiQ2DPF) | multiq |
| Convolution Reverb | convolution-reverb | plugins/convolution-reverb | (inline: ConvolutionReverb) | convolution |
| GrooveMind | groovemind | plugins/groovemind | (PLUGIN_VERSION) | groovemind |
| Sunset Circuits | sunset-circuits | plugins/sunset-circuits/dpf-plugin | SUNSETCIRCUITS (DPF) | sunset |

**Sunset Circuits is DPF, but it is NOT one of the "-2" plugins.** The "-2" plugins
are DPF rewrites that ship alongside a released JUCE original and take its version
lineage; Sunset Circuits has never been released as JUCE (the `multisynth` prototype
still in-tree is unreleased and is not versioned or tagged), so it owns its own
version line. It keeps a `set(SUNSETCIRCUITS_DEFAULT_VERSION "X.Y.Z")` var
in `plugins/sunset-circuits/dpf-plugin/CMakeLists.txt` (which single-sources
`project(VERSION)`, the C++ `getVersion()` via `SC_VERSION_*` compile defs, and the
UI nameplate tooltip) rather than an inline `project()` literal, and it releases
through its own `.github/workflows/dpf-release.yml` (tag `sunset-circuits-v*`),
not `dpf-build.yml`. Everything else (website flow, changelog, push rules) is
identical to the other plugins.

## Paths

- **Plugins repo**: Current working directory (the repo where this skill is invoked)
- **Website repo**: `~/projects/dusk-audio.github.io`

## Instructions

When this skill is invoked, execute ALL steps automatically. Do NOT stop to ask questions unless there is an ambiguity that cannot be resolved. Speed is critical.

### Step 0: Branch Guard

**Before doing anything else**, verify the current git branch is `main`:

```bash
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
```

If `CURRENT_BRANCH` is NOT `main`, **stop immediately** and tell the user:

> "Cannot release from branch `<branch>`. Releases must be made from `main` to ensure version bumps are not lost during PR merges. Please switch to `main` first."

Do NOT proceed with any further steps. Do NOT offer to release anyway.

### Step 1: Parse Arguments and Determine Versions

For EACH plugin specified:

1. **Find current version** from CMakeLists.txt. The JUCE plugins do NOT all
   share one form, and assuming they do is how a release gets tagged at the old
   version. There are three, verified against the files 2026-08-14:
   - **`set(<VAR>_DEFAULT_VERSION "X.Y.Z")`**, with
     `project(<Name> VERSION ${<VAR>_VERSION})` reading it a few lines below:
     4k-eq (FOURKEQ), multi-comp (MULTICOMP), tapemachine (TAPEMACHINE),
     multi-q (MULTIQ), duskverb (DUSKVERB), chord-analyzer (CHORDANALYZER),
     duskamp (DUSKAMP). Multi-Q used to be a literal and no longer is.
   - **Literal version inside `project()`**, no variable at all:
     convolution-reverb → `project(ConvolutionReverb VERSION X.Y.Z)`,
     tape-echo → `project(TapeEcho VERSION X.Y.Z)`.
   - **`set(PLUGIN_VERSION "X.Y.Z")`**, consumed by a `VERSION ${PLUGIN_VERSION}`
     further down: groovemind ONLY.
   - **NOT WIRED AT ALL: spectrum-analyzer.** It sets `PLUGIN_VERSION` but its
     `juce_add_plugin()` never takes a `VERSION` argument, so the variable is
     dead and the binaries are stamped with the root `project(DuskPlugins
     VERSION 1.0.0)`. Bumping that variable edits nothing, and the `grep -q`
     guard below would still pass because the `set()` line really did change.
     Releasing this plugin needs `VERSION ${PLUGIN_VERSION}` added to its
     `juce_add_plugin()` first. Do not ship a "version bump" for it until then.

   Read the file before editing rather than trusting this list, and update the
   list if a plugin changes shape. The `grep -q` guards in Step 3 turn a wrong
   entry into a loud abort rather than a silent no-op, but only if the guard is
   written against the form the file actually uses.
   - **DPF "-2" plugins** use an inline `project(<Project> VERSION X.Y.Z)` in
     `plugins/<dir>/dpf-plugin/CMakeLists.txt` (version is plumbed into the code via
     compile definitions, so no other file needs editing). Project tokens:
     - tapemachine-2 → `project(TapeMachine2DPF …)`
     - 4k-eq-2 → `project(FourKEQ2DPF …)`
     - tape-echo-2 → `project(TapeEchoDPF …)`
     - multi-q-2 → `project(MultiQ2DPF …)`
     The DPF version guard in `dpf-build.yml` strips any `-beta`/`-rc`/`-alpha` suffix
     from the tag, then rejects the release unless the tag's numeric BASE version equals
     this `project()` VERSION — so bump it here to match exactly (e.g. tag
     `tapemachine-2-v2.0.1-rc1` requires `project(TapeMachine2DPF VERSION 2.0.1)`).
   - **Sunset Circuits** (DPF, own version line) uses
     `set(SUNSETCIRCUITS_DEFAULT_VERSION "X.Y.Z")` in
     `plugins/sunset-circuits/dpf-plugin/CMakeLists.txt`.
2. **Determine new version**:
   - If explicit version provided: use it
   - If omitted: auto-increment patch (1.0.2 → 1.0.3)
3. **Validate**: Check the tag `<slug>-v<new-version>` doesn't already exist

### Step 2: Gather Changelog

**For patch bumps (auto-increment)**: Auto-generate changelog from git log since the last tag.
`<Directory>` is the full path from the slug table's Directory column (it already includes
the `plugins/` prefix — do NOT add it again; DPF "-2" plugins end in `/dpf-plugin`):
```bash
git log <slug>-v<old-version>..HEAD --oneline -- <Directory>/
```
Summarize the commits into a concise changelog. If no plugin-specific commits exist, check for shared code changes:
```bash
git log <slug>-v<old-version>..HEAD --oneline -- plugins/shared/
```

**For minor/major bumps**: Ask the user for changelog entries using AskUserQuestion. Require SPECIFIC entries (not generic "bug fixes").

### Step 3: Update All Version Files (Automated)

For EACH plugin, update the CMakeLists.txt:

Use the form Step 1 identified for THAT plugin. Editing the wrong form is a
silent no-op, so each case below carries the guard that catches it.

**Var form** (4k-eq, multi-comp, tapemachine, multi-q, duskverb, chord-analyzer,
duskamp):
```bash
set(<VAR>_DEFAULT_VERSION "<new-version>")
grep -q "set(<VAR>_DEFAULT_VERSION \"<new-version>\")" <Directory>/CMakeLists.txt \
  || { echo "ERROR: <VAR> version bump did not apply"; exit 1; }
```

**Literal-in-project form** (convolution-reverb, tape-echo). These have no
`_DEFAULT_VERSION` variable, so the var-form guard above would abort on them:
```bash
project(ConvolutionReverb VERSION <new-version>)   # convolution-reverb
project(TapeEcho          VERSION <new-version>)   # tape-echo
grep -q "project(<Project> VERSION <new-version>)" <Directory>/CMakeLists.txt \
  || { echo "ERROR: <Project> version bump did not apply"; exit 1; }
```

**PLUGIN_VERSION form** (groovemind only):
```bash
set(PLUGIN_VERSION "<new-version>")
grep -q "set(PLUGIN_VERSION \"<new-version>\")" <Directory>/CMakeLists.txt \
  || { echo "ERROR: PLUGIN_VERSION bump did not apply"; exit 1; }
```
The guard above is necessary but NOT sufficient for this form: it proves the
`set()` was edited, not that anything reads it. Confirm the plugin's
`juce_add_plugin()` actually carries `VERSION ${PLUGIN_VERSION}` before relying
on it. spectrum-analyzer is the counter-example and cannot be released this way
(see Step 1).

**DPF "-2" plugins** (inline project version in `plugins/<dir>/dpf-plugin/CMakeLists.txt`).
Use `<base-version>` — the numeric version with NO prerelease suffix. Any
`-beta`/`-rc`/`-alpha` suffix belongs ONLY in the release tag, never in `project()`:
```
project(TapeMachine2DPF VERSION <base-version>)   # tapemachine-2
project(FourKEQ2DPF     VERSION <base-version>)   # 4k-eq-2
project(TapeEchoDPF     VERSION <base-version>)   # tape-echo-2
project(MultiQ2DPF      VERSION <base-version>)   # multi-q-2
```
Bump only the one being released. `dpf-build.yml`'s guard strips any
`-beta`/`-rc`/`-alpha` suffix from the tag and compares the numeric BASE version to
this `project()` VERSION — they must match exactly or the release fails.

**Sunset Circuits** (version var in the `dpf-plugin/` CMakeLists):
```bash
sed -i.bak 's/set(SUNSETCIRCUITS_DEFAULT_VERSION "[^"]*")/set(SUNSETCIRCUITS_DEFAULT_VERSION "<new-version>")/' \
  plugins/sunset-circuits/dpf-plugin/CMakeLists.txt && rm plugins/sunset-circuits/dpf-plugin/CMakeLists.txt.bak
# Verify the replacement actually landed — a silently unmatched sed must stop
# the release before commit/tag.
grep -q 'set(SUNSETCIRCUITS_DEFAULT_VERSION "<new-version>")' \
  plugins/sunset-circuits/dpf-plugin/CMakeLists.txt \
  || { echo "ERROR: Sunset Circuits version bump did not apply"; exit 1; }
```

**Manual front matter** (issue #80) — only if `manuals/<slug>.md` exists. Uses the portable `sed -i.bak ... && rm` form so this works on both macOS BSD sed and Linux GNU sed:
```bash
PLUGINS_REPO=$(pwd)
MANUAL_MD="$PLUGINS_REPO/manuals/<slug>.md"
TODAY=$(date +%Y-%m-%d)
if [ -f "$MANUAL_MD" ]; then
  sed -i.bak 's/^version: .*/version: <new-version>/' "$MANUAL_MD" && rm "$MANUAL_MD.bak"
  sed -i.bak "s/^last_updated: .*/last_updated: $TODAY/" "$MANUAL_MD" && rm "$MANUAL_MD.bak"
fi
```
The manual front matter `version:` is unquoted (e.g., `version: 1.0.9`); the website's `_plugins/<slug>.md` uses quotes (`version: "1.0.9"`). Match the existing format in each file.

### Step 4: Update Website (Automated)

Update `~/projects/dusk-audio.github.io/_data/plugins.yml`:

For each plugin, use `sed` to update the version line. The file uses YAML format where version appears after the plugin's slug line. Use this approach:

Use the portable `sed -i.bak … && rm` form (same as Step 3). Bare `sed -i ''` is
BSD-only: on Linux GNU sed the `''` is consumed as the SCRIPT and the real script
becomes a filename, so the command exits 2 with `sed: can't read s/...: No such
file or directory` and silently edits nothing.

```bash
WEBSITE_REPO=~/projects/dusk-audio.github.io

# 1. Update _data/plugins.yml (version line inside the target plugin's block)
PLUGINS_YML="$WEBSITE_REPO/_data/plugins.yml"

# Anchor the match and require EXACTLY ONE hit. "slug: tapemachine" is a prefix
# of "slug: tapemachine-2" (likewise 4k-eq/4k-eq-2, multi-q/multi-q-2,
# tape-echo/tape-echo-2), so an unanchored grep returns two line numbers and
# $((SLUG_LINE)) then dies on a two-line value.
SLUG_LINES=$(grep -nE "^[[:space:]]*slug: <slug>[[:space:]]*$" "$PLUGINS_YML" | cut -d: -f1)
SLUG_COUNT=$(printf '%s' "$SLUG_LINES" | grep -c . || true)
if [ "$SLUG_COUNT" -ne 1 ]; then
  echo "ERROR: expected exactly 1 'slug: <slug>' entry in _data/plugins.yml, found $SLUG_COUNT"; exit 1
fi
SLUG_LINE="$SLUG_LINES"

# Bound the edit to this plugin's block: from its slug line to the line before
# the next top-level "- name:" entry (or EOF for the last one). Entries run 6 to
# 12 lines, so a fixed +10 window overshoots the short ones and rewrites the
# NEXT plugin's version.
NEXT_NAME=$(awk -v s="$SLUG_LINE" 'NR > s && /^- name:/ { print NR; exit }' "$PLUGINS_YML")
if [ -n "$NEXT_NAME" ]; then END_LINE=$((NEXT_NAME - 1)); else END_LINE=$(wc -l < "$PLUGINS_YML"); fi

if sed -n "${SLUG_LINE},${END_LINE}p" "$PLUGINS_YML" | grep -qE "^[[:space:]]*version:"; then
  sed -i.bak "${SLUG_LINE},${END_LINE}s/^\([[:space:]]*\)version: .*/\1version: <new-version>/" \
    "$PLUGINS_YML" && rm "$PLUGINS_YML.bak"
else
  # First release: the entry carries no version key yet (see the pre-release
  # block below). Append one at the end of this plugin's block.
  awk -v e="$END_LINE" 'NR == e { print; print "  version: <new-version>"; next } { print }' \
    "$PLUGINS_YML" > "$PLUGINS_YML.tmp" && mv "$PLUGINS_YML.tmp" "$PLUGINS_YML"
  END_LINE=$((END_LINE + 1))
fi

# Verify INSIDE the block only. A whole-file grep passes whenever ANY other
# plugin already carries this version number, hiding a sed that did nothing.
sed -n "${SLUG_LINE},${END_LINE}p" "$PLUGINS_YML" | grep -qE "^[[:space:]]*version: <new-version>[[:space:]]*$" \
  || { echo "ERROR: plugins.yml version bump did not apply"; exit 1; }

# 2. Update _plugins/<slug>.md (front matter version field)
PLUGIN_MD="$WEBSITE_REPO/_plugins/<slug>.md"
if [ -f "$PLUGIN_MD" ]; then
  sed -i.bak 's/^version: ".*"/version: "<new-version>"/' "$PLUGIN_MD" && rm "$PLUGIN_MD.bak"
  grep -q '^version: "<new-version>"' "$PLUGIN_MD" \
    || { echo "ERROR: _plugins/<slug>.md version bump did not apply"; exit 1; }
else
  echo "ERROR: _plugins/<slug>.md missing — the plugin page, download links and changelog live there"; exit 1
fi
```

**IMPORTANT**: Use `sed` for in-place edits. Do NOT use Python `yaml.dump` - it destroys comments and formatting.
**IMPORTANT**: A first-time release needs BOTH website files to exist before the skill runs. Neither is created here; a missing entry aborts the release rather than shipping a page with no download links.
**IMPORTANT**: Both `_data/plugins.yml` AND `_plugins/<slug>.md` must be updated - the plugin pages read from the markdown files.
**IMPORTANT**: Not every `_plugins/<slug>.md` uses LF line endings. Any anchored
match (`^changelog:$`, `^version: ".*"$`) silently matches NOTHING on a CRLF file,
because the line really ends `changelog:\r`. That failure is invisible: the edit
reports success and the release ships without a changelog entry. Always verify each
edit landed (the `grep -q` guards above do this) and match the file's existing
endings rather than converting it. `grep -c $'\r$' <file>` tells you which it is.

If the plugin has a pre-release status (`status: in-dev` **or** `status: coming-soon`) and is
being released for the first time, also update (in `_data/plugins.yml`, and in `_plugins/<slug>.md`
if it carries a `status:` field):
- `status: in-dev` / `status: coming-soon` → `status: released`
- `featured: false` → `featured: true`
- Add `version: <new-version>` if missing

#### Step 4b: Append a new entry to the `_plugins/<slug>.md` changelog array (Automated, issue #80)

Step 4 only updates the top-level `version:` field. The `changelog:` array also needs a new entry so the plugin page lists the release. Insert immediately after the `changelog:` line, in the format the existing entries use:

```bash
# Use awk (portable across BSD and GNU sed) to insert after the `changelog:` key
WEBSITE_REPO=~/projects/dusk-audio.github.io
PLUGIN_MD="$WEBSITE_REPO/_plugins/<slug>.md"
TODAY=$(date +%Y-%m-%d)
NEW_VERSION="<new-version>"

# Pull each changelog line gathered in Step 2 into one bullet entry per line.
# The CHANGELOG_BULLETS variable should be a newline-separated list, e.g.
#   "First change"
#   "Second change"
#
# If only one summary string is available, use it as a single bullet.

awk -v ver="$NEW_VERSION" -v date="$TODAY" -v bullets="$CHANGELOG_BULLETS" '
  /^changelog:$/ && !inserted {
    print
    print "  - version: \"" ver "\""
    print "    date: \"" date "\""
    print "    changes:"
    n = split(bullets, lines, "\n")
    for (i = 1; i <= n; i++) {
      if (lines[i] != "") print "      - \"" lines[i] "\""
    }
    inserted = 1
    next
  }
  { print }
' "$PLUGIN_MD" > "$PLUGIN_MD.tmp" && mv "$PLUGIN_MD.tmp" "$PLUGIN_MD"
```

The new entry appears at the TOP of the `changelog:` array (newest first, matching existing convention).

#### Step 4c: Regenerate manual PDFs (Automated, issue #80)

Skip this step if `manuals/<slug>.md` does not exist (plugin has no manual yet).

```bash
PLUGINS_REPO=$(pwd)
if [ -f "$PLUGINS_REPO/manuals/<slug>.md" ]; then
  cd "$PLUGINS_REPO/manuals"
  python3 build_manuals.py --slug <slug>
  python3 build_manuals.py --combined
  cd "$PLUGINS_REPO"
fi
```

For batch releases (multiple plugins in one invocation), run the per-slug command for each plugin THEN run `--combined` once at the end (combined regeneration is idempotent and inexpensive, but no need to run it N times).

If pandoc or xelatex is not installed locally, this step fails. The skill should report the missing tool and exit cleanly without leaving the repos in a half-staged state. The release CI workflow continues to fetch the previously-published PDF if no new one was generated.

### Step 5: Commit Everything

**Plugins repo** - Stage and commit all changed CMakeLists.txt files plus any bumped manual front matter:
```bash
# Stage ONLY each selected plugin's own CMakeLists.txt — no `plugins/*` wildcard,
# no error suppression — so unrelated in-flight version bumps are never swept in and
# a missing/failed add aborts the release loudly. PLUGIN_DIR = the full Directory
# value from the slug table (JUCE e.g. plugins/4k-eq; DPF "-2" plugins e.g.
# plugins/TapeMachine/dpf-plugin). Repeat this pair per selected plugin:
PLUGIN_DIR="<Directory from the slug table>"
git add -- "$PLUGIN_DIR/CMakeLists.txt"
# Issue #80: include any manual front-matter bumps from Step 3 (only if present)
git add manuals/*.md 2>/dev/null || true
git commit -m "<summary of version bumps>"
```

For single plugin: `"4K EQ v1.0.8: <one-line changelog summary>"`
For batch: `"Bump versions: 4K EQ v1.0.8, Multi-Comp v1.2.3, ..."`

**Do NOT add Co-Authored-By trailers** — they pollute changelogs and release notes.

**Website repo** - stage version + changelog edits AND any regenerated PDFs from Step 4c:
```bash
cd ~/projects/dusk-audio.github.io
git add _data/plugins.yml _plugins/*.md
# Issue #80: include regenerated manual PDFs (per-plugin + combined)
git add assets/manuals/*.pdf 2>/dev/null || true
git commit -m "Update <plugin(s)> to v<version>"
```

### Step 6: Create Tags

For EACH plugin, create an annotated tag with changelog:

```bash
# Write changelog to temp file (preserves ## headers)
cat > /tmp/tag_message.txt << 'TAGEOF'
<Plugin Name> v<version>

<changelog entries>
TAGEOF

git tag -a <slug>-v<version> --cleanup=verbatim -F /tmp/tag_message.txt
```

The `<slug>-v<version>` form produces the tag each plugin's CI release workflow
listens for:
- **JUCE plugins** (4k-eq, multi-comp, tapemachine, tape-echo, multi-q, convolution-reverb, …)
  → matched by `.github/workflows/build.yml` (`<slug>-v*` triggers).
- **DPF "-2" plugins** (tapemachine-2, 4k-eq-2, tape-echo-2, multi-q-2)
  → matched by `.github/workflows/dpf-build.yml`, whose registry maps each `<slug>-v*`
  tag to the right `plugins/<dir>/dpf-plugin` build. e.g. `tapemachine-2-v2.0.1`
  triggers a TapeMachine 2 build + release.
- **Sunset Circuits** → matched by `.github/workflows/dpf-release.yml`, which is
  dedicated to the one plugin (`sunset-circuits-v*`); it is not in the
  `dpf-build.yml` registry.

### Step 7: Push Everything

**CRITICAL: Push tags ONE AT A TIME.** GitHub Actions silently drops ALL push events when more than 3 tags are pushed in a single `git push` command. This causes CI builds to never trigger, resulting in broken releases.

```bash
# Push plugins repo commits
git push origin <current-branch>

# Push tags ONE AT A TIME with a delay between each
# (GitHub silently drops all events when >3 tags are pushed at once)
git push origin <tag1>
sleep 2
git push origin <tag2>
sleep 2
# ... repeat for each additional tag

# Push website repo
cd ~/projects/dusk-audio.github.io
git pull --rebase origin main  # Handle any CI-pushed changes
git push origin main
```

### Step 8: Report Results

Print a summary table:

```
| Plugin | Old Version | New Version | Tag |
|--------|------------|-------------|-----|
| 4K EQ  | 1.0.7      | 1.0.8       | 4k-eq-v1.0.8 |
| ...    | ...        | ...         | ... |

Website updated and pushed.
Tags pushed - GitHub Actions builds will start automatically.
Monitor: gh run list --limit 5
```

## Error Handling

- **Website push conflict**: Run `git pull --rebase` then retry push
- **Tag already exists**: Warn user and skip (don't overwrite existing tags)
- **No changes to commit**: Skip the commit step, still create tags if versions changed
- **Build failures after push**: Use `gh run view <id> --log-failed` to diagnose
- **Partial tag push failure**: If a tag push fails midway through a batch, report which tags were pushed successfully and which failed. Retry the failed pushes individually. Tags are idempotent — re-pushing an already-pushed tag is a no-op, so it's safe to retry all remaining tags.
