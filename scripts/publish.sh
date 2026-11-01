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

# gh が無い・未認証でも、GITHUB_TOKEN を渡せばそのまま公開できる。
TOKEN="${GITHUB_TOKEN:-${GH_TOKEN:-}}"
USE_TOKEN=0
if [ -n "$TOKEN" ]; then
    USE_TOKEN=1
elif [ -x "$GH" ] && "$GH" auth status >/dev/null 2>&1; then
    :
else
    echo "gh is not authenticated - run: ./scripts/login.sh" >&2
    echo "or set GITHUB_TOKEN (scope: repo) and re-run." >&2
    exit 1
fi

api() {  # api <method> <path> [body]
    if [ "$USE_TOKEN" = 1 ]; then
        local method="$1" path="$2" body="${3:-}"
        if [ -n "$body" ]; then
            curl -sf -X "$method" -H "Authorization: Bearer $TOKEN" \
                -H 'Content-Type: application/json' -d "$body" "https://api.github.com$path"
        else
            curl -sf -X "$method" -H "Authorization: Bearer $TOKEN" "https://api.github.com$path"
        fi
    else
        "$GH" api "$@"
    fi
}

if [ "$USE_TOKEN" = 1 ]; then
    ACCOUNT="$(api GET /user | python3 -c 'import json,sys; print(json.load(sys.stdin).get("login",""))' 2>/dev/null || true)"
    [ -n "$ACCOUNT" ] || { echo "GITHUB_TOKEN が無効です（API が /user を返さなかった）" >&2; exit 1; }
else
    ACCOUNT="$("$GH" api user --jq .login)"
fi
[ -n "$ACCOUNT" ] || { echo "could not resolve the GitHub account" >&2; exit 1; }

cd "$ROOT"

DESC="Raspberry Pi Pico 2 + BME280 temperature/pressure/humidity monitor in C++ (pico-sdk), with host tests, flashing scripts and a roadmap for an Angular real-time UI"

if [ "$USE_TOKEN" = 1 ]; then
    if curl -sf -o /dev/null -H "Authorization: Bearer $TOKEN" "https://api.github.com/repos/$ACCOUNT/$REPO_NAME"; then
        echo "repository $ACCOUNT/$REPO_NAME already exists, reusing it"
    else
        if [ "$VISIBILITY" = private ]; then PRIVATE=true; else PRIVATE=false; fi
        api POST /user/repos "{\"name\":\"$REPO_NAME\",\"private\":$PRIVATE,\"description\":\"$DESC\"}" >/dev/null
    fi
elif "$GH" repo view "$ACCOUNT/$REPO_NAME" >/dev/null 2>&1; then
    echo "repository $ACCOUNT/$REPO_NAME already exists, reusing it"
else
    "$GH" repo create "$REPO_NAME" "--$VISIBILITY" --description "$DESC"
fi

git remote add origin "https://github.com/$ACCOUNT/$REPO_NAME.git" 2>/dev/null ||
    git remote set-url origin "https://github.com/$ACCOUNT/$REPO_NAME.git"

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [ "$USE_TOKEN" = 1 ]; then
    # トークンは .git/config に残さないよう、push 時にだけ credential helper として渡す
    git -c "credential.helper=" -c "credential.helper=!f() { echo password=$TOKEN; }; f" \
        push --force-with-lease -u origin "$BRANCH"
else
    git push --force-with-lease -u origin "$BRANCH"
fi

# push できたかリモート側の実コミットで確認する
LOCAL="$(git rev-parse HEAD)"
if [ "$USE_TOKEN" = 1 ]; then
    REMOTE="$(api GET "repos/$ACCOUNT/$REPO_NAME/commits" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["sha"])' 2>/dev/null || true)"
else
    REMOTE="$("$GH" api "repos/$ACCOUNT/$REPO_NAME/commits" --jq '.[0].sha' 2>/dev/null || true)"
fi
if [ -n "$REMOTE" ] && [ "$REMOTE" != "$LOCAL" ]; then
    echo "warning: remote HEAD $REMOTE != local $LOCAL" >&2
    exit 1
fi

echo
echo "pushed to https://github.com/$ACCOUNT/$REPO_NAME ($VISIBILITY)"
