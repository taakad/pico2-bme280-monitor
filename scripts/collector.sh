#!/usr/bin/env bash
# Run the collector service: serial -> PostgreSQL -> REST/WebSocket -> built UI.
#
# Starts the database first (scripts/db.sh), builds the Angular app if the
# bundle is missing, then runs the collector in the foreground. Ctrl-C stops it.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$repo_root/scripts/env.sh" ] && . "$repo_root/scripts/env.sh"
export PGHOST="${PGHOST:-/tmp}"
export PGPORT="${PGPORT:-5433}"
export PGDATABASE="${PGDATABASE:-pico_monitor}"
export PGUSER="${PGUSER:-pico}"

"$repo_root/scripts/db.sh"

if [ ! -f "$repo_root/ui/dist/ui/browser/index.html" ]; then
    echo "building the UI (first run)..."
    (cd "$repo_root/ui" && npm install --no-audit --no-fund && npm run build)
fi

echo "collector on http://${HOST:-127.0.0.1}:${PORT:-8787}"
(cd "$repo_root/server" && npm install --no-audit --no-fund >/dev/null && exec node src/index.js)
