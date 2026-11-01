#!/usr/bin/env bash
# Creates the private GitHub repository and pushes this project to it.
#
# Requires gh to be authenticated:  ./scripts/login.sh
# Override the repo name with:      REPO_NAME=whatever scripts/publish.sh
set -euo pipefail

# macOS: Xcode のライセンス未同意だとシステム git が動かないため、
# Command Line Tools を明示的に指定する。
if [ -d /Library/Developer/CommandLineTools ]; then
    export DEVELOPER_DIR="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# gh を探す: 明示指定 → PATH → ~/toolchain 配下
GH="${GH:-}"
if [ -z "$GH" ]; then
    GH="$(command -v gh || true)"
fi
if [ -z "$GH" ]; then
    GH="$(find "$HOME/toolchain/gh" -type f -name gh 2>/dev/null | head -1 || true)"
fi
REPO_NAME="${REPO_NAME:-pico2-bme280-monitor}"
VISIBILITY="${VISIBILITY:-private}"

command -v git >/dev/null 2>&1 || { echo "git not found" >&2; exit 1; }
[ -x "$GH" ] || { echo "gh not found - install the GitHub CLI" >&2; exit 1; }
"$GH" auth status >/dev/null 2>&1 || { echo "gh is not authenticated - run: ./scripts/login.sh" >&2; exit 1; }
ACCOUNT="$("$GH" api user --jq .login)"

cd "$ROOT"

if [ -n "$("$GH" repo view "$ACCOUNT/$REPO_NAME" 2>/dev/null)" ]; then
    echo "repository $ACCOUNT/$REPO_NAME already exists, reusing it"
else
    "$GH" repo create "$REPO_NAME" "--$VISIBILITY" --description \
        "Raspberry Pi Pico 2 + BME280 temperature/pressure/humidity monitor in C++ (pico-sdk), with host tests, flashing scripts and a roadmap for an Angular real-time UI"
fi

git remote add origin "https://github.com/$ACCOUNT/$REPO_NAME.git" 2>/dev/null ||
    git remote set-url origin "https://github.com/$ACCOUNT/$REPO_NAME.git"

git push --force-with-lease -u origin "$(git rev-parse --abbrev-ref HEAD)"

# push できたかリモート側の実コミットで確認する
LOCAL="$(git rev-parse HEAD)"
REMOTE="$("$GH" api "repos/$ACCOUNT/$REPO_NAME/commits" --jq '.[0].sha' 2>/dev/null || true)"
if [ -n "$REMOTE" ] && [ "$REMOTE" != "$LOCAL" ]; then
    echo "warning: remote HEAD $REMOTE != local $LOCAL" >&2
    exit 1
fi

echo
echo "pushed to https://github.com/$ACCOUNT/$REPO_NAME ($VISIBILITY)"
