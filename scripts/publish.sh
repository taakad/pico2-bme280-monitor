#!/usr/bin/env bash
# Creates the private GitHub repository and pushes this project to it.
#
# Requires gh to be authenticated:  gh auth login
# Override the repo name with:      REPO_NAME=whatever scripts/publish.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GH="${GH:-$HOME/toolchain/gh/gh_2.103.3_macOS_arm64/bin/gh}"
REPO_NAME="${REPO_NAME:-pico2-bme280-monitor}"
VISIBILITY="${VISIBILITY:-private}"
ACCOUNT="$("$GH" api user --jq .login)"

command -v git >/dev/null 2>&1 || { echo "git not found" >&2; exit 1; }
[ -x "$GH" ] || { echo "gh not found at $GH - install the GitHub CLI" >&2; exit 1; }
"$GH" auth status >/dev/null 2>&1 || { echo "gh is not authenticated - run: gh auth login" >&2; exit 1; }

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
"$GH" repo view "$ACCOUNT/$REPO_NAME" --web

echo
echo "pushed to https://github.com/$ACCOUNT/$REPO_NAME ($VISIBILITY)"
