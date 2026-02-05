# Release Plugin

Release one or more Luna Co. Audio plugins with automated version bumps, website updates, tagging, and push.

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
| SilkVerb | silkverb | plugins/SilkVerb | SILKVERB | silkverb |
| Convolution Reverb | convolution-reverb | plugins/convolution-reverb | CONVOLUTION | convolution |
| Neural Amp | neural-amp | plugins/neural-amp | NEURALAMP | nam |
| GrooveMind | groovemind | plugins/groovemind | GROOVEMIND | groovemind |

## Paths

- **Plugins repo**: Current working directory (the repo where this skill is invoked)
- **Website repo**: `~/projects/lunacoaudio.github.io`

## Instructions

When this skill is invoked, execute ALL steps automatically. Do NOT stop to ask questions unless there is an ambiguity that cannot be resolved. Speed is critical.

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

**Standard plugins** (4k-eq, multi-comp, tapemachine, silkverb, convolution-reverb, neural-amp, groovemind):
```
set(<VAR>_DEFAULT_VERSION "<new-version>")
```

**Multi-Q** (uses inline project version):
```
project(MultiQ VERSION <new-version>)
```

### Step 4: Update Website (Automated)

Update `~/projects/lunacoaudio.github.io/_data/plugins.yml`:

For each plugin, use `sed` to update the version line. The file uses YAML format where version appears after the plugin's slug line. Use this approach:

```bash
# Find the line number of the slug, then update the next "version:" line
WEBSITE_REPO=~/projects/lunacoaudio.github.io
SLUG_LINE=$(grep -n "slug: <slug>" "$WEBSITE_REPO/_data/plugins.yml" | cut -d: -f1)
if [ -n "$SLUG_LINE" ]; then
  # Find the version line within the next 10 lines after slug
  sed -i '' "$((SLUG_LINE)),$(( SLUG_LINE + 10 ))s/version: .*/version: <new-version>/" "$WEBSITE_REPO/_data/plugins.yml"
fi
```

**IMPORTANT**: Use `sed` for in-place edits. Do NOT use Python `yaml.dump` - it destroys comments and formatting.

If the plugin has `status: in-dev` and is being released for the first time, also update:
- `status: in-dev` → `status: released`
- `featured: false` → `featured: true`
- Add `version: <new-version>` if missing

### Step 5: Commit Everything

**Plugins repo** - Stage and commit all changed CMakeLists.txt files:
```bash
git add plugins/*/CMakeLists.txt
git commit -m "<summary of version bumps>

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

For single plugin: `"4K EQ v1.0.8: <one-line changelog summary>"`
For batch: `"Bump versions: 4K EQ v1.0.8, Multi-Comp v1.2.3, ..."`

**Website repo**:
```bash
cd ~/projects/lunacoaudio.github.io
git add _data/plugins.yml
git commit -m "Update <plugin(s)> to v<version>

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
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

```bash
# Push plugins repo commits
git push origin <current-branch>

# Push all new tags
git push origin <tag1> <tag2> ...

# Push website repo
cd ~/projects/lunacoaudio.github.io
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
