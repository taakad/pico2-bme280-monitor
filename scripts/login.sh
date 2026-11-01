#!/usr/bin/env bash
# GitHub 認証を対話的に行うスクリプト。
#
#   ./scripts/login.sh              # ブラウザで認可するデバイスコード方式（推奨）
#   ./scripts/login.sh --token      # PAT をターミナルに貼り付ける方式
#
# 認証できたら ./scripts/publish.sh でリポジトリを作成・push します。
set -euo pipefail

GH="${GH:-$HOME/toolchain/gh/gh_2.103.3_macOS_arm64/bin/gh}"
if [ ! -x "$GH" ]; then
  echo "gh CLI が見つかりません: $GH"
  echo "先に ./scripts/setup-toolchain.sh を実行してください。"
  exit 1
fi

if [ "${1:-}" = "--token" ]; then
  echo "Personal Access Token を貼り付けて Enter（表示されません）:"
  echo "  作成: https://github.com/settings/tokens （scope: repo）"
  read -rs TOKEN
  printf '%s' "$TOKEN" | "$GH" auth login --hostname github.com --git-protocol https --with-token
  unset TOKEN
else
  # --web にすると gh がカーソル位置問い合わせで止まる環境があるため
  # 明示的にデバイスコード方式を使う。ブラウザが自動的に開く。
  "$GH" auth login --hostname github.com --git-protocol https \
    --scopes repo,workflow --web
fi

echo
echo "=== 認証状態 ==="
"$GH" auth status
echo
echo "=== アカウント ==="
"$GH" api user --jq '"login=\(.login)  name=\(.name)"'
echo
echo "次: ./scripts/publish.sh"
