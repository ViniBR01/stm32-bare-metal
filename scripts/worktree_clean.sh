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

# Verify the branch has been merged before cleaning up.
# Two checks are needed because squash-merge creates a new commit with no
# shared history with the branch tip, so `git branch --merged` misses it.
#
#   1. Fast-forward / regular merge: branch tip is an ancestor of origin/main
#   2. Squash merge: a merged PR for this branch exists on GitHub
is_merged=0

git -C "${REPO_ROOT}" fetch origin main --quiet 2>/dev/null || true

if git -C "${REPO_ROOT}" branch --merged origin/main \
        | grep -qE "^[[:space:]]*${BRANCH}$"; then
    is_merged=1
fi

if [ "${is_merged}" -eq 0 ]; then
    # Fall back to GitHub PR state (handles squash merges)
    REPO_SLUG="$(git -C "${REPO_ROOT}" remote get-url origin \
                 | sed 's|.*github\.com[:/]\(.*\)\.git|\1|;s|.*github\.com[:/]\(.*\)|\1|')"
    if gh pr list --repo "${REPO_SLUG}" \
            --head "${BRANCH}" --state merged --json number \
            --jq '.[0].number' 2>/dev/null | grep -q '[0-9]'; then
        is_merged=1
        echo "Branch '${BRANCH}' was squash-merged (confirmed via GitHub PR)."
    fi
fi

if [ "${is_merged}" -eq 0 ]; then
    echo "Warning: branch '${BRANCH}' does not appear merged into origin/main." >&2
    read -r -p "  Proceed anyway? [y/N] " confirm
    if [[ "${confirm}" != "y" && "${confirm}" != "Y" ]]; then
        echo "Aborted." >&2
        exit 1
    fi
fi

# Remove the worktree
if [ -d "${WORKTREE_PATH}" ]; then
    git -C "${REPO_ROOT}" worktree remove --force "${WORKTREE_PATH}"
    echo "Removed worktree: ${WORKTREE_PATH}"
else
    echo "Worktree path not found (already removed?): ${WORKTREE_PATH}"
fi

# Prune stale worktree references
git -C "${REPO_ROOT}" worktree prune

# Delete the local branch
if git -C "${REPO_ROOT}" show-ref --verify --quiet "refs/heads/${BRANCH}"; then
    git -C "${REPO_ROOT}" branch -D "${BRANCH}"
    echo "Deleted local branch: ${BRANCH}"
fi

echo "Cleanup complete for: ${BRANCH}"
