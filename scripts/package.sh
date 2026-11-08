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

# --- macOS: form-based Setup.app inside the DMG ------------------------------
# The DMG no longer asks users to drag files around: it carries a single
# Setup.app whose Resources contain the whole bundle as a tar.gz. The app
# shows a settings form (DB host/port/user/password, HTTP host/port) and runs
# scripts/install.sh with the entered values.
if command -v hdiutil >/dev/null 2>&1 && command -v swiftc >/dev/null 2>&1; then
    dmg="dist/$name.dmg"
    rm -f "$dmg"
    appdir="dist/Setup.app"
    rm -rf "$appdir"
    mkdir -p "$appdir/Contents/MacOS" "$appdir/Contents/Resources"
    echo "compiling installer app (swiftc) ..."
    swiftc -O -o "$appdir/Contents/MacOS/Setup" installer/macos/Setup.swift \
        -framework AppKit
    cat > "$appdir/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Pico2 BME280 Monitor Setup</string>
    <key>CFBundleIdentifier</key><string>dev.taakad.pico2-bme280-monitor.setup</string>
    <key>CFBundleExecutable</key><string>Setup</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${version#v}</string>
    <key>LSMinimumSystemVersion</key><string>12.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
    # Embed the bundle (same file set as the zip) as a tar.gz resource.
    (cd dist && COPYFILE_DISABLE=1 tar -czf "Setup.app/Contents/Resources/bundle.tar.gz" "$name")
    # Ad-hoc sign so Gatekeeper on the same machine accepts the freshly built app.
    codesign --force --deep -s - "$appdir" 2>/dev/null || true
    hdiutil create -quiet -volname "Pico2 BME280 Monitor" -srcfolder "$appdir" -ov -format UDZO "$dmg"
    rm -rf "$appdir"
    echo "wrote $dmg ($(du -h "$dmg" | cut -f1)) with form-based Setup.app"
elif command -v hdiutil >/dev/null 2>&1; then
    # Fallback for machines without swiftc: classic folder-drag DMG.
    dmg="dist/$name.dmg"
    rm -f "$dmg"
    ln -sf /Applications "$stage/Applications" 2>/dev/null || true
    hdiutil create -quiet -volname "Pico2 BME280 Monitor" -srcfolder "$stage" -ov -format UDZO "$dmg"
    rm -f "$stage/Applications"
    echo "wrote $dmg ($(du -h "$dmg" | cut -f1)) [folder-drag fallback: swiftc missing]"
fi

rm -rf "$stage"
echo "bundle ready in dist/"
