# Release Plugin

Release a Luna Co. Audio plugin with version updates, changelog, and GitHub release.

## Configuration

Adjust these paths to match your local environment:
- **PLUGINS_REPO**: Path to the plugins repository (default: `~/projects/Luna/plugins`)
- **WEBSITE_REPO**: Path to the website repository (default: `~/projects/lunacoaudio.github.io`)

## Usage

```
/release-plugin <plugin-name> [version]
```

**Arguments:**
- `plugin-name`: Plugin slug (e.g., `multi-comp`, `4k-eq`, `tapemachine`)
- `version` (optional): New version number (e.g., `1.2.0`). If not provided, auto-increment patch version.

**Examples:**
- `/release-plugin multi-comp 1.2.0` - Release Multi-Comp v1.2.0
- `/release-plugin 4k-eq` - Release 4K EQ with auto-incremented version

## Instructions

When this skill is invoked, follow these steps:

### 1. Gather Information

First, determine the plugin details:
- **Plugin directory**: `plugins/<plugin-name>/`
- **Current version**: Check `CMakeLists.txt` for `DEFAULT_VERSION` variable
- **Previous release tag**: Find with `git tag --list "<plugin>-v*" | tail -1`
- **Website files** (in WEBSITE_REPO):
  - `_data/plugins.yml`

If no version is provided, auto-increment:
- Bug fix: increment patch (1.0.0 → 1.0.1)
- New feature: increment minor (1.0.0 → 1.1.0)
- Breaking change: increment major (1.0.0 → 2.0.0)

Ask the user what type of release this is if not clear.

### 2. Gather Changelog Information (MANDATORY - DO NOT SKIP)

**This step is CRITICAL. Users download releases based on what changed. Vague changelogs are unacceptable.**

**Step 2a: Show the previous release notes for context:**
```bash
git show <plugin>-v{previous-version} --quiet
```

This helps the user remember what was in the last release and what's changed since.

**Step 2b: Ask the user for SPECIFIC changelog entries:**

Use AskUserQuestion or direct prompt to collect:
```
What changes are in v{version}? Please provide SPECIFIC details for each category that applies:

## What's New (new features added)
-

## Improvements (enhancements to existing features)
-

## Bug Fixes (issues resolved)
-

## Technical Changes (build/infrastructure)
-

IMPORTANT: Generic entries like "bug fixes" or "various improvements" are NOT acceptable.
Each entry must describe WHAT specifically changed so users know what they're getting.

Example of GOOD entries:
- "Window size now persists between sessions"
- "Fixed crash when loading presets with missing files"
- "Added 2x oversampling option for higher quality"

Example of BAD entries:
- "Bug fixes"
- "Improvements"
- "Updated code"
```

**Step 2c: Validate the changelog before proceeding:**

Check that the user's response includes:
- [ ] At least ONE section with specific entries
- [ ] NO entries that are just "bug fixes" or "improvements" without detail
- [ ] Each entry describes WHAT changed, not just THAT something changed

**If validation fails:** Ask the user for more specific details. Do NOT proceed until you have meaningful changelog entries.

### 3. Update Version Files

Update these files with the new version:

**Plugin CMakeLists.txt** (`plugins/<plugin>/CMakeLists.txt`):
```cmake
set(<PLUGIN>_DEFAULT_VERSION "x.y.z")
```

**Plugin header file** (if exists, e.g., `FourKEQ.h`):
```cpp
static constexpr const char* PLUGIN_VERSION = "x.y.z";  // Fallback
```

### 4. Update Website

**plugins.yml** (`WEBSITE_REPO/_data/plugins.yml`):
- Update the `version:` field for the plugin

### 5. Local Build Validation (Recommended)

Before committing, optionally run a local build to catch errors early:

```bash
./docker/build_release.sh <plugin-shortcut>
```

Plugin shortcuts: `4keq`, `compressor`, `tape`, `tapeecho`, `silkverb`, `convolution`, `multiq`, `nam`

If local build fails, fix errors before proceeding.

Optionally run pluginval for validation:
```bash
./tests/run_plugin_tests.sh --plugin "<Plugin Name>" --skip-audio
```

### 6. Commit Changes

**Plugins repo** (current directory):
```bash
git add plugins/<plugin>/CMakeLists.txt [other-changed-files]
git commit -m "<Plugin> v{version}: <one-line summary>

## What's New
- <entry from changelog>

## Improvements
- <entry from changelog>

## Bug Fixes
- <entry from changelog>

## Technical Changes
- <entry from changelog>

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

**Website repo** (`WEBSITE_REPO`):
```bash
git add _data/plugins.yml
git commit -m "Update <Plugin> to v{version}

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

### 7. Create Tag with FULL Changelog

**CRITICAL: The tag message becomes the GitHub Release description. Include the FULL changelog.**

```bash
git tag -a <plugin>-v{version} -m "<Plugin Name> v{version}

## What's New
- <specific feature 1>
- <specific feature 2>

## Improvements
- <specific improvement 1>
- <specific improvement 2>

## Bug Fixes
- <specific fix 1>

## Technical Changes
- <specific change 1>
"
```

**Use a HEREDOC for proper formatting:**
```bash
git tag -a <plugin>-v{version} -m "$(cat <<'EOF'
<Plugin Name> v{version}

## What's New
- <entries>

## Improvements
- <entries>

## Bug Fixes
- <entries>

## Technical Changes
- <entries>
EOF
)"
```

### 8. Push Everything

```bash
# Push plugin repo commit
git push origin main

# Push only the specific release tag
git push origin <plugin>-v{version}

# Push website repo
cd $WEBSITE_REPO
git push origin main
```

### 9. Verify Build Started

Check that GitHub Actions build started:
```bash
gh run list --limit 5
```

Identify the run ID for the tag build (look for the `<plugin>-v{version}` tag).

### 10. Monitor Build and Handle Errors

**Wait for build completion:**
```bash
gh run watch <run-id>
```

**If build succeeds:**
1. Confirm all platforms built (Linux, Windows, macOS)
2. Report success with release URL: `https://github.com/luna-co-software/plugins/releases/tag/<plugin>-v{version}`
3. Remind user to verify the GitHub Release page shows the full changelog

**If build fails:**
1. Get the failure details: `gh run view <run-id> --log-failed`
2. Fix the issue
3. Delete and recreate the tag:
   ```bash
   git push origin --delete <plugin>-v{version}
   git tag -d <plugin>-v{version}
   git tag -a <plugin>-v{version} -m "..." # Use same changelog
   git push origin <plugin>-v{version}
   ```

### 11. Post-Release Verification

1. **Check GitHub Release page** - Verify changelog is displayed properly
2. **Check website** - Verify version number and download links
3. Report final status to user

## Common Build Errors and Fixes

| Error | Likely Cause | Fix |
|-------|--------------|-----|
| `undefined reference` | Missing source file | Add file to `target_sources()` |
| `no matching function` | API change | Check function signature |
| `file not found` | Missing include | Fix include path |
| `LNK2019` (Windows) | Missing library | Add to `target_link_libraries()` |
| Plugin validation failed | Parameter issue | Run pluginval locally |

## Plugin Slug Mapping

| Plugin Name | Slug | Directory | Build Shortcut |
|-------------|------|-----------|----------------|
| 4K EQ | 4k-eq | plugins/4k-eq | 4keq |
| Multi-Comp | multi-comp | plugins/multi-comp | compressor |
| TapeMachine | tapemachine | plugins/TapeMachine | tape |
| Tape Echo | tape-echo | plugins/tape-echo | tapeecho |
| Multi-Q | multi-q | plugins/multi-q | multiq |
| SilkVerb | silkverb | plugins/SilkVerb | silkverb |
| Convolution Reverb | convolution-reverb | plugins/convolution-reverb | convolution |
| Neural Amp | neural-amp | plugins/neural-amp | nam |
| GrooveMind | groovemind | plugins/groovemind | groovemind |
