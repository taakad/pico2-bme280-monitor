#!/usr/bin/env bash
# Build a distributable bundle of the whole system: source, prebuilt firmware
# UF2, installers (macOS + Windows) and docs. Output lands in dist/:
#
#   dist/pico2-bme280-monitor-<version>.zip   # works on Mac and Windows
#   dist/pico2-bme280-monitor-<version>.dmg   # macOS disk image (if hdiutil)
#
# The bundle contains no node_modules/dist/data: install.sh / install.ps1
# fetch Node, PostgreSQL and TimescaleDB on the target machine.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

version="${VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"
name="pico2-bme280-monitor-$version"
stage="dist/$name"
rm -rf "$stage" "dist/$name.zip" "dist/$name.dmg"
mkdir -p "$stage"

# Track the same files git does, minus build outputs and dev cruft.
git ls-files -z | while IFS= read -r -d '' f; do
    mkdir -p "$stage/$(dirname "$f")"
    cp "$f" "$stage/$f"
done

# Ship the firmware binary so users never need the Arm toolchain.
if [ -f firmware/build/bme280_monitor.uf2 ]; then
    mkdir -p "$stage/firmware/uf2"
    cp firmware/build/bme280_monitor.uf2 "$stage/firmware/uf2/bme280_monitor.uf2"
    echo "including prebuilt firmware UF2"
else
    echo "note: no firmware/build/bme280_monitor.uf2 - run scripts/build.sh to include it"
fi

chmod +x "$stage/scripts/"*.sh 2>/dev/null || true

(cd dist && zip -qr "$name.zip" "$name" -x '*/.DS_Store')
echo "wrote dist/$name.zip ($(du -h "dist/$name.zip" | cut -f1))"

if command -v hdiutil >/dev/null 2>&1; then
    dmg="dist/$name.dmg"
    rm -f "$dmg"
    # Add a shortcut folder for macOS users.
    ln -sf /Applications "$stage/Applications" 2>/dev/null || true
    hdiutil create -quiet -volname "Pico2 BME280 Monitor" -srcfolder "$stage" -ov -format UDZO "$dmg"
    rm -f "$stage/Applications"
    echo "wrote $dmg ($(du -h "$dmg" | cut -f1))"
fi

rm -rf "$stage"
echo "bundle ready in dist/"
