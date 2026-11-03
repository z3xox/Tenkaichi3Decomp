#!/bin/bash
# What the Mac apps made by port/macos/make_apps.sh run: asks GitHub for a newer version of this checkout, offers
# it in a dialog, then starts the game (or the setup window with "setup").
#   port/macos/launcher.sh          check for an update, then play
#   port/macos/launcher.sh setup    check for an update, then open the setup window
# An update is pulled with git (fast-forward only) and rebuilt by the setup window, which shows the progress and ends
# with Play. The disc image for the rebuild is the one named in bt3_iso.txt (written by make_apps.sh). No internet,
# or GitHub slower than 10 seconds: the game starts as it is. Saves, settings, mods and game data are not in git and
# are not touched. BT3_NO_UPDATE_CHECK=1 skips the check.
# Started from Finder a program gets only /usr/bin:/bin:/usr/sbin:/sbin: Homebrew's folders first, as in a Terminal.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
cd "$(dirname "$0")/../.." || exit 1
LOG="$PWD/update.log"
ISO="$(head -n1 bt3_iso.txt 2>/dev/null)"

dialog() { # dialog <text> <button>... : prints the button pressed; the last one is the default
    local buttons=""
    for b in "${@:2}"; do buttons="$buttons\"$b\", "; done
    osascript -e "button returned of (display dialog \"$1\" with title \"Tenkaichi3Decomp\" buttons {${buttons%, }} default button \"${@: -1}\")" 2>/dev/null
}

BEHIND=0
if [ -z "$BT3_NO_UPDATE_CHECK" ] && git rev-parse --abbrev-ref '@{u}' > /dev/null 2>&1; then
    git fetch --quiet >> "$LOG" 2>&1 &
    FETCH=$!
    for _ in $(seq 1 20); do kill -0 $FETCH 2>/dev/null || break; sleep 0.5; done
    kill $FETCH 2>/dev/null
    BEHIND=$(git rev-list --count 'HEAD..@{u}' 2>/dev/null || echo 0)
fi

if [ "$BEHIND" -gt 0 ]; then
    CHANGES=$(git log --format="• %s" -n 6 'HEAD..@{u}' | sed 's/["\\]//g')
    ANSWER=$(dialog "A new version is available ($BEHIND changes):\n\n$CHANGES\n\nUpdating builds the game again, which takes a few minutes. Saves, settings and mods are kept." "Later" "Update now")
    if [ "$ANSWER" = "Update now" ]; then
        echo "=== update $(date)" >> "$LOG"
        if ! git pull --ff-only >> "$LOG" 2>&1; then
            dialog "The update could not be applied (details in update.log). The game starts as it was." "OK" > /dev/null
        else
            port/setup/build.sh >> "$LOG" 2>&1
            if [ -f "$ISO" ]; then
                BT3_SETUP_ISO="$ISO" exec ./Tenkaichi3Decomp-setup
            fi
            dialog "The disc image is needed to build the update. Choose it in the setup window and press Install." "OK" > /dev/null
            exec ./Tenkaichi3Decomp-setup
        fi
    fi
fi

if [ "$1" = setup ]; then
    [ -f "$ISO" ] && exec ./Tenkaichi3Decomp-setup "$ISO"
    exec ./Tenkaichi3Decomp-setup
fi
if [ ! -x play.sh ]; then
    dialog "The game is not built yet. The setup window will open: choose your disc image and press Install." "OK" > /dev/null
    exec ./Tenkaichi3Decomp-setup
fi
exec ./play.sh > game.log 2>&1
