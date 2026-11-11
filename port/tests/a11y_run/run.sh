#!/bin/sh
# Runs the Linux release with no window in the release container (port/release/Dockerfile), against a game data folder
# of a disc that is installed already, for the accessibility's checks (port/tools/a11y_check.py):
#
#   docker run --rm -v "$PWD":/src -v <installed game>/gamedata:/gamedata:ro bt3-port-release:jammy \
#       sh /src/port/tests/a11y_run/run.sh <folder> <seconds>
#
# The release is port/build/Tenkaichi3Decomp-linux-x64.zip (or $BT3_A11Y_ZIP); <folder> (from the repository's top) is
# where the run's files go: a11y_log.txt is its log (BT3_A11Y_LOG, in Spanish), out.txt the program's output. If the
# folder has settings.txt it is the run's settings; env.txt has lines VAR=value for the run, for instance
#   BT3_A11Y_READER=nvda            as if NVDA spoke (the blind player profile is applied the first time)
#   BT3_A11Y_ASK=150                where the rival is and the gauges, in turn, every 150 blanks
#   BT3_SOUND=1 + SDL_AUDIO_DRIVER=dummy   the sounds are made (and noted) with nowhere to go
#   BT3_SOUND=1 + SDL_AUDIO_DRIVER=disk + SDL_AUDIO_DISK_OUTPUT_FILE=<folder>/audio.raw + BT3_SND_DUMP=<folder>/music.raw
#       + BT3_SND_VERBOSE=1 + BT3_PACED=1    the sound recorded at the game's pace (a11y_check.py music)
#   BT3_PAD_PLAY=<pad file>         input played back (padtool.py makes one from a script like duel.txt)
# Without input the game shows its attract mode: CPU fights with the gauges, ended by the time, every minute or so.
set -e
R=/src/$1
ZIP=${BT3_A11Y_ZIP:-/src/port/build/Tenkaichi3Decomp-linux-x64.zip}
rm -rf /tmp/g /tmp/saves && mkdir -p /tmp/saves
unzip -q "$ZIP" -d /tmp/g
if [ -f "$R/settings.txt" ]; then cp "$R/settings.txt" /tmp/s.txt; else : > /tmp/s.txt; fi
cd /tmp/g/Tenkaichi3Decomp
export BT3_DATA=/gamedata BT3_SAVES=/tmp/saves BT3_SETTINGS=/tmp/s.txt BT3_GS=none BT3_A11Y_LOG="$R/a11y_log.txt" BT3_A11Y_LANG=es
if [ -f "$R/env.txt" ]; then
    while IFS= read -r line; do [ -n "$line" ] && export "$line"; done < "$R/env.txt"
fi
timeout -s KILL "$2" ./Tenkaichi3Decomp > "$R/out.txt" 2>&1 || true  # (killed: the game answers a TERM with its exit question)
cp /tmp/s.txt "$R/settings_after.txt"
echo "run $1: done"
