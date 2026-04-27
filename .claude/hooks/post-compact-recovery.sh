#!/bin/bash
# Hook: Re-inject critical context after context compaction
# Triggered by SessionStart with "compact" matcher

if [ -z "$CLAUDE_PROJECT_DIR" ]; then
    echo "Warning: CLAUDE_PROJECT_DIR not set, skipping recovery"
    exit 0
fi
cd "$CLAUDE_PROJECT_DIR" || exit 0

echo "=== POST-COMPACTION CONTEXT RECOVERY ==="
echo ""

# Current branch and git state
echo "## Git State"
echo "Branch: $(git branch --show-current)"
echo ""
git status --short
echo ""

# Recent commits (what we've been working on)
echo "## Recent Commits (last 5)"
git log --oneline -5
echo ""

# Staged vs unstaged changes summary
STAGED=$(git diff --cached --stat)
UNSTAGED=$(git diff --stat)
if [ -n "$STAGED" ]; then
    echo "## Staged Changes"
    echo "$STAGED"
    echo ""
fi
if [ -n "$UNSTAGED" ]; then
    echo "## Unstaged Changes"
    echo "$UNSTAGED"
    echo ""
fi

# Key project rules that get lost in compaction
cat <<'RULES'
## Critical Project Rules (from CLAUDE.md)
- Build verification: `cmake --build build --config Release --target <Plugin>_AU -j8`
- Plugin tests: `./tests/run_plugin_tests.sh --plugin "<Name>" --skip-audio`
- Never delete `*_VV_snare.wav` files (only delete `*_DV_snare.wav`)
- No Co-Authored-By trailers on commits
- User manages git themselves — don't auto-commit
- Build AU targets for local testing (Logic Pro uses AU, not VST3)
- Check `plugins/shared/` before writing new code
- Re-read files before editing after compaction — don't trust previous memory
RULES

echo ""
echo "=== END RECOVERY ==="
