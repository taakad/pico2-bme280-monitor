#!/usr/bin/env bash
# Start (or reuse) the local PostgreSQL instance used by the collector.
#
# This assumes the admin-free install: Postgres.app binaries copied under
# ~/toolchain/pgsql/<version> with the data directory at ~/toolchain/pgsql/data,
# listening on port 5433 through the /tmp socket. Nothing is written to system
# locations.
set -euo pipefail

PGROOT="${PGROOT:-$HOME/toolchain/pgsql}"
PGPORT="${PGPORT:-5433}"
PGDATA="$PGROOT/data"

pgbin=""
for candidate in "$PGROOT"/17 "$PGROOT"/16 "$PGROOT"/15; do
    if [ -x "$candidate/bin/pg_ctl" ]; then
        pgbin="$candidate/bin"
        break
    fi
done
if [ -z "$pgbin" ]; then
    echo "no PostgreSQL found under $PGROOT (expected $PGROOT/17/bin/pg_ctl)" >&2
    echo "see docs/roadmap.md or extract Postgres.app: cp -R Postgres.app/Contents/Versions/17 $PGROOT/17" >&2
    exit 1
fi

if "$pgbin/pg_isready" -h /tmp -p "$PGPORT" -q; then
    echo "already running on port $PGPORT"
else
    mkdir -p "$PGDATA"
    if [ ! -f "$PGDATA/PG_VERSION" ]; then
        echo "initialising cluster at $PGDATA (superuser: pico, trust auth)"
        "$pgbin/initdb" -D "$PGDATA" -U pico --auth=trust >/dev/null
    fi
    "$pgbin/pg_ctl" -D "$PGDATA" -l "$PGROOT/server.log" \
        -o "-p $PGPORT -k /tmp" -w start
fi

# Make sure the app database and schema exist (both idempotent).
if ! "$pgbin/psql" -h /tmp -p "$PGPORT" -U pico -lqt | cut -d '|' -f 1 | grep -qw pico_monitor; then
    "$pgbin/createdb" -h /tmp -p "$PGPORT" -U pico pico_monitor
fi
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
"$pgbin/psql" -h /tmp -p "$PGPORT" -U pico -d pico_monitor -q -f "$repo_root/db/schema.sql"
echo "schema applied; psql -h /tmp -p $PGPORT -U pico -d pico_monitor"
