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
- **Current version**: Check `CMakeLists.txt` for `project(... VERSION x.y.z)`
- **Website files** (in WEBSITE_REPO):
  - `_data/plugins.yml`
  - `_plugins/<plugin-name>.md`

If no version is provided, auto-increment:
- Bug fix: increment patch (1.0.0 → 1.0.1)
- New feature: increment minor (1.0.0 → 1.1.0)
- Breaking change: increment major (1.0.0 → 2.0.0)

Ask the user what type of release this is if not clear.

### 2. Ask for Changelog

Ask the user to provide changelog entries for this release. Format:
```
What changes should be included in the v{version} changelog?
```

### 3. Update Version Files

Update these files with the new version:

**Plugin CMakeLists.txt** (`plugins/<plugin>/CMakeLists.txt`):
```cmake
project(<PluginName> VERSION x.y.z)
```

**Plugin AppConfig.h** (`plugins/<plugin>/AppConfig.h`):
```cpp
#define JucePlugin_VersionCode  0xMMmmpp  // MM=major, mm=minor, pp=patch in hex
#define JucePlugin_VersionString "x.y.z"
```

Version code encoding: For version 1.2.3 → `0x10203` (major×0x10000 + minor×0x100 + patch)

### 4. Update Website

**plugins.yml** (`WEBSITE_REPO/_data/plugins.yml`):
- Update the `version:` field for the plugin

**Plugin page** (`WEBSITE_REPO/_plugins/<plugin>.md`):
- Update `version:` in front matter
- Add new changelog entry at the top of the `changelog:` list with today's date

### 5. Local Build Validation (Recommended)

Before committing, optionally run a local build to catch errors early:

```bash
cd $PLUGINS_REPO/build
cmake --build . --target <PluginTarget>_All -j8
```

Plugin targets:
- 4K EQ: `FourKEQ_All`
- Multi-Comp: `MultiComp_All`
- TapeMachine: `TapeMachine_All`
- Multi-Q: `MultiQ_All`
- SilkVerb: `SilkVerb_All`
- Neural Amp: `NeuralAmp_All`

If local build fails, fix errors before proceeding.

Optionally run pluginval for validation:
```bash
./tests/run_plugin_tests.sh --plugin "<Plugin Name>" --skip-audio
```

### 6. Commit Changes

**Plugins repo** (current directory):
```bash
git add plugins/<plugin>/CMakeLists.txt plugins/<plugin>/AppConfig.h
git commit -m "<Plugin> v{version}: <brief description>

<changelog entries>

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
**Website repo** (`WEBSITE_REPO`):
```bash
git add _data/plugins.yml _plugins/<plugin>.md
git commit -m "Update <Plugin> to v{version}

<brief description>

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

### 7. Create Tag and Push

```bash
# Create annotated tag
git tag -a <plugin>-v{version} -m "<Plugin> v{version} - <brief description>"

# Push plugin repo
git push origin main

# Push only the specific release tag (avoids pushing unrelated tags)
git push origin <plugin>-v{version}

# Push website repo
cd $WEBSITE_REPO
git push origin main
```

### 8. Verify Build Started

Check that GitHub Actions build started:
```bash
gh run list --limit 5
```

Identify the run ID for the tag build (look for the `<plugin>-v{version}` tag).

### 9. Monitor Build and Handle Errors

**Wait for build completion** using the run ID from step 8:
```bash
gh run watch <run-id>
```

Or check status periodically:
```bash
gh run view <run-id>
```

**If build succeeds:**
1. Confirm all platforms built (Linux, Windows, macOS)
2. Report success with release URL: `https://github.com/luna-co-software/plugins/releases/tag/<plugin>-v{version}`
3. Remind user to verify the GitHub Release page has all artifacts

**If build fails:**
1. Get the failure details:
   ```bash
   gh run view <run-id> --log-failed
   ```

2. Analyze the error output to identify the issue:
   - **Compilation errors**: Fix the code, commit with message "Fix <plugin> build: <description>"
   - **Missing dependencies**: Update CMakeLists.txt or workflow
   - **Platform-specific issues**: Check platform-specific code paths
   - **Test failures**: Fix failing tests

3. After fixing, push the fix:
   ```bash
   git add <fixed-files>
   git commit -m "Fix <plugin> build: <description>

   Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
   git push origin main
   ```

4. **Important**: Delete the failed tag and recreate it on the fixed commit:
   ```bash
   # Delete remote tag
   git push origin --delete <plugin>-v{version}
   # Delete local tag
   git tag -d <plugin>-v{version}
   # Recreate tag on current commit
   git tag -a <plugin>-v{version} -m "<Plugin> v{version} - <brief description>"
   # Push new tag
   git push origin <plugin>-v{version}
   ```

5. Monitor the new build and repeat if necessary

### 10. Post-Release Verification

Once the build succeeds:

1. **Check GitHub Release page**: `https://github.com/luna-co-software/plugins/releases/tag/<plugin>-v{version}`
   - Verify all platform artifacts are present:
     - `<plugin>-v{version}-linux.zip`
     - `<plugin>-v{version}-windows.zip`
     - `<plugin>-v{version}-macos.zip`

2. **Check website**: `https://luna-co-software.github.io/lunacoaudio.github.io/plugins/<plugin>/`
   - Verify version number is updated
   - Verify download links work

3. Report final status to user with all relevant URLs

## Common Build Errors and Fixes

| Error | Likely Cause | Fix |
|-------|--------------|-----|
| `undefined reference` | Missing source file in CMakeLists.txt | Add file to `target_sources()` |
| `no matching function` | API change or wrong parameters | Check function signature, update call |
| `file not found` | Missing include or wrong path | Fix include path or add file |
| `LNK2019` (Windows) | Missing library or symbol | Add to `target_link_libraries()` |
| `dyld: Library not loaded` (macOS) | Missing framework | Add to `JUCE_NEEDS_*` or link libraries |
| Plugin validation failed | Parameter or state issue | Run pluginval locally to debug |

## Plugin Slug Mapping

| Plugin Name | Slug | Directory |
|-------------|------|-----------|
| 4K EQ | 4k-eq | plugins/4k-eq |
| Multi-Comp | multi-comp | plugins/multi-comp |
| TapeMachine | tapemachine | plugins/TapeMachine |
| Multi-Q | multi-q | plugins/multi-q |
| SilkVerb | silkverb | plugins/SilkVerb |
| Convolution Reverb | convolution-reverb | plugins/convolution-reverb |
| Neural Amp | neural-amp | plugins/neural-amp |
| GrooveMind | groovemind | plugins/groovemind |
