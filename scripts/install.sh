#!/usr/bin/env bash
# One-shot installer for macOS: Node.js, PostgreSQL 17, TimescaleDB, project
# dependencies, database schema — everything the collector needs, installed
# under ~/toolchain without admin rights.
#
#   scripts/install.sh            # install everything
#   scripts/install.sh --service  # also register a launchd agent that starts
#                                 # the collector at login (Pico 接続で即測定)
#
# Re-running is safe: every step is skipped when already satisfied.
set -euo pipefail

NODE_VERSION="24.22.2"          # LTS line; Angular CLI needs >= 24.15
PGAPP_VERSION="2.9.6"           # Postgres.app release carrying PostgreSQL 17
TSDB_VERSION="2.31.0"           # TimescaleDB release

TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/toolchain}"
PGROOT="$TOOLCHAIN_DIR/pgsql"
PGDATA="$PGROOT/data"
PGPORT="${PGPORT:-5433}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"

service=no
for arg in "$@"; do
    case "$arg" in
        --service) service=yes ;;
        *) echo "unknown option: $arg" >&2; exit 1 ;;
    esac
done

log() { printf '\n== %s ==\n' "$*"; }

fetch() { # fetch <url> <dest> — retries, follows redirects
    local url="$1" dest="$2" try
    for try in 1 2 3; do
        if curl -fL --retry 3 --connect-timeout 20 -o "$dest.part" "$url"; then
            mv "$dest.part" "$dest"
            return 0
        fi
        rm -f "$dest.part"
        echo "download failed (attempt $try): $url" >&2
        sleep $((try * 5))
    done
    echo "giving up on: $url" >&2
    return 1
}

# ---------------------------------------------------------------- Node.js ---
log "Node.js"
need_node=yes
node_bin_dir=""
if command -v node >/dev/null 2>&1; then
    ver="$(node -v)"
    # Accept >= 24.15 (what the Angular CLI requires on the 24 line).
    major="${ver#v}"; major="${major%%.*}"
    rest="${ver#v}"; minor="${rest#*.}"; minor="${minor%%.*}"
    if [ "$major" -gt 24 ] || { [ "$major" -eq 24 ] && [ "$minor" -ge 15 ]; }; then
        need_node=no
        node_bin_dir="$(dirname "$(command -v node)")"
        echo "found $ver on PATH"
    else
        echo "found $ver but the Angular CLI needs >= v24.15"
    fi
fi
if [ "$need_node" = yes ]; then
    case "$(uname -m)" in
        arm64) node_arch="arm64" ;;
        x86_64) node_arch="x64" ;;
        *) echo "unsupported arch: $(uname -m)" >&2; exit 1 ;;
    esac
    node_dir="$TOOLCHAIN_DIR/node/node-v$NODE_VERSION-darwin-$node_arch"
    if [ ! -x "$node_dir/bin/node" ]; then
        mkdir -p "$TOOLCHAIN_DIR/node"
        tarball="node-v$NODE_VERSION-darwin-$node_arch.tar.gz"
        echo "downloading $tarball ..."
        fetch "https://nodejs.org/dist/v$NODE_VERSION/$tarball" "/tmp/$tarball"
        tar -xzf "/tmp/$tarball" -C "$TOOLCHAIN_DIR/node"
        rm -f "/tmp/$tarball"
    fi
    export PATH="$node_dir/bin:$PATH"
    node_bin_dir="$node_dir/bin"
    echo "using $(node -v) from $node_bin_dir"
fi

# ------------------------------------------------------------- PostgreSQL ---
log "PostgreSQL 17"
if [ -x "$PGROOT/17/bin/pg_ctl" ]; then
    echo "already installed at $PGROOT/17"
else
    dmg="$TOOLCHAIN_DIR/postgres-app-$PGAPP_VERSION-17.dmg"
    if [ ! -f "$dmg" ]; then
        echo "downloading Postgres.app (PostgreSQL $PGAPP_VERSION bundle) ..."
        fetch "https://github.com/PostgresApp/PostgresApp/releases/download/v$PGAPP_VERSION/Postgres-2.9.6-17.dmg" "$dmg"
    fi
    echo "extracting server binaries to $PGROOT/17 ..."
    mkdir -p "$PGROOT"
    mount="$(hdiutil attach -nobrowse -readonly "$dmg" | awk -F'\t' '/\/Volumes\// {print $NF}' | tail -1)"
    trap 'hdiutil detach "$mount" -quiet || true' EXIT
    cp -R "$mount/Postgres.app/Contents/Versions/17" "$PGROOT/17"
    hdiutil detach "$mount" -quiet
    trap - EXIT
    echo "installed $(("$PGROOT"/17/bin/postgres --version))"
fi

# ------------------------------------------------------------- TimescaleDB --
log "TimescaleDB $TSDB_VERSION"
tsdb_lib="$PGROOT/17/lib/postgresql/timescaledb.dylib"
if [ -e "$tsdb_lib" ]; then
    echo "already installed"
elif ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found - skipping TimescaleDB."
    echo "The collector works with plain PostgreSQL (schema.sql adapts)."
    echo "To add TimescaleDB later: brew install cmake, then re-run this script."
else
    src="$TOOLCHAIN_DIR/tsdb/timescaledb"
    if [ ! -d "$src" ]; then
        mkdir -p "$TOOLCHAIN_DIR/tsdb"
        echo "cloning timescaledb $TSDB_VERSION ..."
        git clone --depth 1 --branch "$TSDB_VERSION" \
            https://github.com/timescale/timescaledb.git "$src"
    fi
    echo "building (USE_OPENSSL=0, no regressions; a few minutes) ..."
    mkdir -p "$src/build"
    (cd "$src/build" &&
        cmake -DUSE_OPENSSL=0 -DREGRESS_CHECKS=OFF \
              -DPG_PATH="$PGROOT/17/bin" .. &&
        cmake --build . -j"$(sysctl -n hw.ncpu)" &&
        make install)
    echo "installed $(basename "$tsdb_lib")"
fi

# ------------------------------------------- cluster, database, schema ------
log "Database cluster + schema"
export PGHOST=/tmp PGPORT PGDATABASE=pico_monitor PGUSER=pico
"$repo_root/scripts/db.sh"

# ------------------------------------------------- project dependencies -----
log "Collector + UI dependencies"
(cd "$repo_root/server" && npm install --no-audit --no-fund)
(cd "$repo_root/ui" && npm install --no-audit --no-fund && npm run build)

# ------------------------------------------------------- firmware (opt-in) --
log "Firmware UF2"
if [ -f "$repo_root/firmware/build/bme280_monitor.uf2" ]; then
    echo "already built: firmware/build/bme280_monitor.uf2"
elif [ -d "$TOOLCHAIN_DIR/pico-sdk" ]; then
    "$repo_root/scripts/build.sh" >/dev/null 2>&1 &&
        echo "built: firmware/build/bme280_monitor.uf2" ||
        echo "firmware build failed - run scripts/build.sh to see why"
else
    echo "pico-sdk not under $TOOLCHAIN_DIR - skipping firmware build."
    echo "Run scripts/setup-toolchain.sh to build UF2s locally."
fi

# ------------------------------------------------------------- service ------
if [ "$service" = yes ]; then
    log "launchd service"
    plist="$HOME/Library/LaunchAgents/com.pico2.bme280.collector.plist"
    mkdir -p "$HOME/Library/LaunchAgents"
    cat > "$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>com.pico2.bme280.collector</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/bash</string>
        <string>$repo_root/scripts/collector.sh</string>
    </array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>StandardOutPath</key><string>$HOME/Library/Logs/pico2-collector.log</string>
    <key>StandardErrorPath</key><string>$HOME/Library/Logs/pico2-collector.log</string>
    <key>EnvironmentVariables</key>
    <dict>
        <key>PATH</key><string>$node_bin_dir:/usr/bin:/bin:/usr/sbin:/sbin</string>
    </dict>
</dict>
</plist>
EOF
    launchctl bootout "gui/$(id -u)/com.pico2.bme280.collector" 2>/dev/null || true
    launchctl bootstrap "gui/$(id -u)" "$plist"
    echo "service registered: starts at login, restarts if it exits"
    echo "log: ~/Library/Logs/pico2-collector.log"
fi

log "done"
cat <<EOF
Start the collector with:
    scripts/collector.sh
then open:
    http://127.0.0.1:8787
Connect the Pico 2 (BME280 wired) and measurement begins automatically.
EOF
