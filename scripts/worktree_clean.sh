#!/usr/bin/env bash
# worktree_clean.sh — Remove a worktree and its branch after the PR is merged.
#
# Usage:
#   bash scripts/worktree_clean.sh <branch-name>
#
# Example:
#   bash scripts/worktree_clean.sh 115-add-i2c-driver
#
# ONLY run this after the PR has been merged into main.
# The local branch is deleted — this cannot be undone.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <branch-name>" >&2
    exit 1
fi

BRANCH="$1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKTREES_BASE="$(dirname "${REPO_ROOT}")/stm32-bare-metal-worktrees"
WORKTREE_PATH="${WORKTREES_BASE}/${BRANCH}"

# Warn if branch isn't merged into origin/main
if git -C "${REPO_ROOT}" fetch origin main --quiet 2>/dev/null; then
    if ! git -C "${REPO_ROOT}" branch --merged origin/main | grep -qE "^[[:space:]]*${BRANCH}$"; then
        echo "Warning: branch '${BRANCH}' does not appear merged into origin/main." >&2
        read -r -p "  Proceed anyway? [y/N] " confirm
        if [[ "${confirm}" != "y" && "${confirm}" != "Y" ]]; then
            echo "Aborted." >&2
            exit 1
        fi
    fi
fi

# Remove the worktree
if [ -d "${WORKTREE_PATH}" ]; then
    git -C "${REPO_ROOT}" worktree remove "${WORKTREE_PATH}"
    echo "Removed worktree: ${WORKTREE_PATH}"
else
    echo "Worktree path not found (already removed?): ${WORKTREE_PATH}"
fi

# Prune stale worktree references
git -C "${REPO_ROOT}" worktree prune

# Delete the local branch
if git -C "${REPO_ROOT}" show-ref --verify --quiet "refs/heads/${BRANCH}"; then
    git -C "${REPO_ROOT}" branch -d "${BRANCH}"
    echo "Deleted local branch: ${BRANCH}"
fi

echo "Cleanup complete for: ${BRANCH}"
