#!/bin/bash
# Hook: Run regression check after git add commands
# Triggered by PostToolUse on Bash — filters for git add only

INPUT=$(cat)
CMD=$(echo "$INPUT" | jq -r '.tool_input.command // empty' 2>/dev/null)

case "$CMD" in
    'git add'*)
        exec "$CLAUDE_PROJECT_DIR/tests/regression_check.sh"
        ;;
    *)
        exit 0
        ;;
esac
