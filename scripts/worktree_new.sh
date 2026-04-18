#!/usr/bin/env bash
# worktree_new.sh — Create a new git worktree + branch for parallel agent work.
#
# Usage:
#   bash scripts/worktree_new.sh <issue-number> <short-description>
#
# Example:
#   bash scripts/worktree_new.sh 115 add-i2c-driver
#
# The new worktree is created at:
#   <repo-parent>/stm32-bare-metal-worktrees/<issue-number>-<short-description>/
#
# The last line of output is the worktree path (parseable by callers).

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <issue-number> <short-description>" >&2
    exit 1
fi

ISSUE="$1"
DESC="$2"
BRANCH="${ISSUE}-${DESC}"

# Resolve the repo root (safe to call from any directory)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Worktrees live alongside the repo, not inside it (git restriction)
WORKTREES_BASE="$(dirname "${REPO_ROOT}")/stm32-bare-metal-worktrees"
WORKTREE_PATH="${WORKTREES_BASE}/${BRANCH}"

# Guard: branch must not already exist
if git -C "${REPO_ROOT}" show-ref --verify --quiet "refs/heads/${BRANCH}"; then
    echo "Error: branch '${BRANCH}' already exists." >&2
    echo "  To resume: git -C ${REPO_ROOT} worktree add ${WORKTREE_PATH} ${BRANCH}" >&2
    exit 1
fi

# Guard: worktree path must not already exist
if [ -e "${WORKTREE_PATH}" ]; then
    echo "Error: path '${WORKTREE_PATH}' already exists." >&2
    echo "  Check: git -C ${REPO_ROOT} worktree list" >&2
    exit 1
fi

# Create base directory if needed
mkdir -p "${WORKTREES_BASE}"

# Fetch latest main so the new branch is not stale
echo "Fetching origin/main..."
git -C "${REPO_ROOT}" fetch origin main --quiet

# Create the worktree with a new branch based on origin/main
git -C "${REPO_ROOT}" worktree add -b "${BRANCH}" "${WORKTREE_PATH}" origin/main

echo ""
echo "Worktree ready: ${WORKTREE_PATH}"
echo "Branch:         ${BRANCH}"
echo "Base:           origin/main ($(git -C "${REPO_ROOT}" rev-parse --short origin/main))"
echo ""
echo "Next steps:"
echo "  1. EnterWorktree ${WORKTREE_PATH}"
echo "  2. Implement and commit"
echo "  3. make test && make all"
echo "  4. git push -u origin ${BRANCH}"
echo "  5. gh pr create ..."
