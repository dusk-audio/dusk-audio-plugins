# Regression Check Skill

Verify that previously fixed issues haven't regressed before committing.

## Invocation
- `/regression-check` - Run all regression checks
- `/regression-check --fix <id>` - Check a specific fix by ID
- `/regression-check --add` - Add a new fix to the registry after confirming it works

## Instructions

When this skill is invoked, perform these steps:

### Step 1: Load the Fixes Registry
Read the fixes registry from `.claude/fixes-registry.json` in the project root.

**Error Handling:**
- If the registry file doesn't exist, report this as an error:
  ```
  ERROR: Fixes registry not found at .claude/fixes-registry.json

  This file tracks verified fixes to prevent regressions.
  Run `/regression-check --add` to create the first entry and initialize the registry.
  ```
- If the registry file exists but is empty or malformed, report the parsing error and suggest checking the JSON syntax.

### Step 2: Run Verifications
For each fix in the registry, run all its verification checks using the Grep tool:

**grep verification:**
Search for the pattern in the specified file. If the pattern is NOT found, the fix has regressed.

**file_exists verification:**
Check if the file exists using the Glob tool.

**multiline grep:**
Use the Grep tool with `multiline: true` for patterns that span lines.

### Step 3: Report Results

Format the output as a clear table:

```
=== Regression Check Results ===

[PASS] cmd-drag-fine-control - Cmd/Ctrl+drag fine control
       - LunaSlider class exists in shared library
       - Uses setMouseDragSensitivity for fine control
       - Checks for Cmd/Ctrl modifier keys
       - 4K EQ uses LunaSlider
       - TapeMachine uses LunaSlider

[PASS] timer-lifecycle-crash-fix - Timer lifecycle crash fix
       - 4K EQ destructor calls stopTimer()

[FAIL] some-broken-fix - REGRESSION DETECTED
       - Expected: "some pattern" in file.cpp
       - Found: Pattern not present
       - This fix was for ticket #123
       - To restore: [specific instructions]

=== Summary ===
Passed: 7/8
Failed: 1/8

RECOMMENDATION: Do not commit until regressions are fixed.
```

### Step 4: Block Commits on Failure

If ANY fix has regressed:
1. Show which fix(es) failed with details
2. Show the expected code vs actual
3. Recommend NOT committing until fixed
4. Offer to help restore the fix

If all fixes pass:
1. Show success summary
2. Confirm it's safe to commit

### Adding a New Fix

When `/regression-check --add` is invoked or when a fix is confirmed working:

1. Ask for:
   - Fix ID (short kebab-case identifier)
   - Ticket number(s)
   - Description
   - Files involved
   - Code patterns to verify (grep patterns that prove the fix is in place)

2. Add the entry to `.claude/fixes-registry.json`

3. Run the new verifications to confirm they pass

4. Update the `last_updated` date in the registry

## Registry Location
`.claude/fixes-registry.json`

## Example Registry Entry

```json
{
  "id": "cmd-drag-fine-control",
  "ticket": "#5",
  "description": "Cmd/Ctrl+drag fine control for knobs",
  "date_fixed": "2026-01-28",
  "files": ["plugins/shared/LunaLookAndFeel.h"],
  "verifications": [
    {
      "type": "grep",
      "file": "plugins/shared/LunaLookAndFeel.h",
      "pattern": "class LunaSlider",
      "description": "LunaSlider class must exist"
    }
  ]
}
```

## When to Use This Skill

1. **Before every commit** - Run `/regression-check` to verify no fixes have broken
2. **After confirming a fix works** - Run `/regression-check --add` to register the fix
3. **When investigating a bug** - Check if it's a known regression from the registry
