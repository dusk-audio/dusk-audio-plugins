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
| Tape Echo | tape-echo | plugins/tape-echo | TAPEECHO | tapeecho |
| Multi-Q | multi-q | plugins/multi-q | (inline) | multiq |
| Convolution Reverb | convolution-reverb | plugins/convolution-reverb | CONVOLUTION | convolution |
| GrooveMind | groovemind | plugins/groovemind | GROOVEMIND | groovemind |

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

1. **Find current version** from CMakeLists.txt:
   - Most plugins: `set(<VAR>_DEFAULT_VERSION "X.Y.Z")`
   - Multi-Q uses: `project(MultiQ VERSION X.Y.Z)`
2. **Determine new version**:
   - If explicit version provided: use it
   - If omitted: auto-increment patch (1.0.2 → 1.0.3)
3. **Validate**: Check the tag `<slug>-v<new-version>` doesn't already exist

### Step 2: Gather Changelog

**For patch bumps (auto-increment)**: Auto-generate changelog from git log since the last tag:
```bash
git log <slug>-v<old-version>..HEAD --oneline -- plugins/<directory>/
```
Summarize the commits into a concise changelog. If no plugin-specific commits exist, check for shared code changes:
```bash
git log <slug>-v<old-version>..HEAD --oneline -- plugins/shared/
```

**For minor/major bumps**: Ask the user for changelog entries using AskUserQuestion. Require SPECIFIC entries (not generic "bug fixes").

### Step 3: Update All Version Files (Automated)

For EACH plugin, update the CMakeLists.txt:

**Standard plugins** (4k-eq, multi-comp, tapemachine, convolution-reverb, groovemind):
```
set(<VAR>_DEFAULT_VERSION "<new-version>")
```

**Multi-Q** (uses inline project version):
```
project(MultiQ VERSION <new-version>)
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

```bash
WEBSITE_REPO=~/projects/dusk-audio.github.io

# 1. Update _data/plugins.yml (version line after slug)
SLUG_LINE=$(grep -n "slug: <slug>" "$WEBSITE_REPO/_data/plugins.yml" | cut -d: -f1)
if [ -n "$SLUG_LINE" ]; then
  sed -i '' "$((SLUG_LINE)),$(( SLUG_LINE + 10 ))s/version: .*/version: <new-version>/" "$WEBSITE_REPO/_data/plugins.yml"
fi

# 2. Update _plugins/<slug>.md (front matter version field)
PLUGIN_MD="$WEBSITE_REPO/_plugins/<slug>.md"
if [ -f "$PLUGIN_MD" ]; then
  sed -i '' 's/^version: ".*"/version: "<new-version>"/' "$PLUGIN_MD"
fi
```

**IMPORTANT**: Use `sed` for in-place edits. Do NOT use Python `yaml.dump` - it destroys comments and formatting.
**IMPORTANT**: Both `_data/plugins.yml` AND `_plugins/<slug>.md` must be updated - the plugin pages read from the markdown files.

If the plugin has `status: in-dev` and is being released for the first time, also update:
- `status: in-dev` → `status: released`
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
git add plugins/*/CMakeLists.txt
# Issue #80: include any manual front-matter bumps from Step 3
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
