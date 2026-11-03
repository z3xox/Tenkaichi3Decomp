#!/bin/bash
# Makes two Mac apps for this source checkout, to start it from Finder, Launchpad or the Dock:
#   Tenkaichi3Decomp.app         checks GitHub for a newer version (a dialog offers it), then starts the game
#   Tenkaichi3Decomp Setup.app   the same check, then the setup window (build again, add languages)
# Both run port/macos/launcher.sh in this folder, so the checkout must stay where it is (run this again after moving it).
#   port/macos/make_apps.sh [--iso <disc image>] [--icon <picture>] [--to <folder>]
#     --iso    the disc image an update is built from, kept in bt3_iso.txt (default: the one given before, or the
#              only .iso in or next to this folder)
#     --icon   any picture macOS can open (png, jpg, webp...), made into the apps' icon; without it the default icon
#     --to     where the apps go (default: ~/Applications)
# The apps are made here, not downloaded, so Gatekeeper does not stop them.
cd "$(dirname "$0")/../.." || exit 1
ROOT="$PWD"
TO="$HOME/Applications"
ICON=""
while [ $# -gt 0 ]; do
    case "$1" in
        --iso) ISO="$2"; shift 2 ;;
        --icon) ICON="$2"; shift 2 ;;
        --to) TO="$2"; shift 2 ;;
        *) echo "usage: port/macos/make_apps.sh [--iso <disc image>] [--icon <picture>] [--to <folder>]" >&2; exit 1 ;;
    esac
done
[ "$(uname -s)" = Darwin ] || { echo "make_apps.sh is for macOS" >&2; exit 1; }

if [ -z "$ISO" ]; then
    ISO="$(head -n1 bt3_iso.txt 2>/dev/null)"
    if [ ! -f "$ISO" ]; then
        ISO=""
        FOUND=$(ls "$ROOT"/*.iso "$ROOT"/../*.iso 2>/dev/null)
        [ "$(echo "$FOUND" | grep -c .)" = 1 ] && ISO="$FOUND"
    fi
fi
if [ -n "$ISO" ]; then
    [ -f "$ISO" ] || { echo "no disc image at $ISO" >&2; exit 1; }
    (cd "$(dirname "$ISO")" && echo "$PWD/$(basename "$ISO")") > bt3_iso.txt
    echo "disc image for updates: $(cat bt3_iso.txt)"
else
    echo "no disc image given (--iso): an update will ask for it in the setup window"
fi

port/setup/build.sh > /dev/null || { echo "the setup window did not build: port/setup/build.sh" >&2; exit 1; }

if [ -n "$ICON" ]; then
    [ -f "$ICON" ] || { echo "no picture at $ICON" >&2; exit 1; }
    T=$(mktemp -d)
    mkdir "$T/app.iconset"
    sips -s format png -z 1024 1024 "$ICON" --out "$T/icon.png" > /dev/null || { echo "macOS cannot open $ICON" >&2; exit 1; }
    for s in 16 32 128 256 512; do
        sips -z $s $s "$T/icon.png" --out "$T/app.iconset/icon_${s}x${s}.png" > /dev/null
        sips -z $((s * 2)) $((s * 2)) "$T/icon.png" --out "$T/app.iconset/icon_${s}x${s}@2x.png" > /dev/null
    done
    iconutil -c icns "$T/app.iconset" -o "$T/AppIcon.icns" || exit 1
fi

make_app() { # make_app <name> <executable> <bundle id> <launcher argument>
    local app="$TO/$1.app"
    rm -rf "$app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
    printf '#!/bin/sh\nexec "%s/port/macos/launcher.sh" %s\n' "$ROOT" "$4" > "$app/Contents/MacOS/$2"
    chmod +x "$app/Contents/MacOS/$2"
    [ -n "$ICON" ] && cp "$T/AppIcon.icns" "$app/Contents/Resources/AppIcon.icns"
    cat > "$app/Contents/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>$1</string>
	<key>CFBundleDisplayName</key><string>$1</string>
	<key>CFBundleExecutable</key><string>$2</string>
	<key>CFBundleIdentifier</key><string>$3</string>
	<key>CFBundleIconFile</key><string>AppIcon</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>1.0</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
    touch "$app"
    echo "made $app"
}

mkdir -p "$TO" || exit 1
make_app "Tenkaichi3Decomp" Tenkaichi3Decomp io.github.tenkaichi3decomp.play ""
make_app "Tenkaichi3Decomp Setup" Tenkaichi3DecompSetup io.github.tenkaichi3decomp.setup setup
[ -n "$ICON" ] && rm -rf "$T"
exit 0
