#!/bin/sh
# Builds the setup window (port/setup/setup.cpp, native.cpp) and the game's screen reader code it speaks through.
#   port/setup/build.sh        -> ./Tenkaichi3Decomp-setup            (needs g++ and SDL3; Dear ImGui is in the repository)
#   port/setup/build.sh win    -> port/build/Tenkaichi3Decomp-setup.exe
#        on Linux: cross-compiled with mingw-w64 (x86_64-w64-mingw32-g++) and SDL3's mingw development package
#                  unpacked under port/build/win (or its x86_64-w64-mingw32 folder named by BT3_SDL3);
#        on Windows (MSYS2 shell): with that shell's g++ and SDL3.
#   on macOS: the same command -> ./Tenkaichi3Decomp-setup, with Apple's clang++ and Homebrew's SDL3 (brew install
#        sdl3; BT3_SDL3 names another SDL3 folder, as install.py); the window for install.py in a source checkout.
cd "$(dirname "$0")/../.." || exit 1
I=port/third_party/imgui
if [ "$1" = win ]; then
    O=port/build/setup_win; OUT=port/build/Tenkaichi3Decomp-setup.exe; LIBS="-lSDL3 -static -static-libgcc -static-libstdc++ -mwindows"
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) CXX=g++; SDL="" ;;
        *) CXX=x86_64-w64-mingw32-g++
           S=${BT3_SDL3:-$(ls -d port/build/win/SDL3-*/x86_64-w64-mingw32 2>/dev/null | tail -1)}
           [ -d "$S" ] || { echo "SDL3 for mingw not found: unpack SDL3-devel-*-mingw.tar.gz under port/build/win"; exit 1; }
           SDL="-I$S/include -L$S/lib" ;;
    esac
elif [ "$(uname -s)" = Darwin ]; then
    S=${BT3_SDL3:-/opt/homebrew/opt/sdl3}
    [ -f "$S/include/SDL3/SDL.h" ] || { echo "SDL3 not found in $S: brew install sdl3"; exit 1; }
    O=port/build/setup_mac; OUT=Tenkaichi3Decomp-setup; CXX="clang++ -mmacosx-version-min=11.0"; SDL="-I$S/include -L$S/lib"
    LIBS="-L$S/lib -lSDL3 -Wl,-rpath,@executable_path/lib -Wl,-rpath,$S/lib"
else
    O=port/build/setup; OUT=Tenkaichi3Decomp-setup; CXX=g++; SDL=""; LIBS="-lSDL3 -lpthread -ldl -lm -static-libstdc++ -static-libgcc -Wl,-rpath,\$ORIGIN/lib"
fi
case $CXX in *g++) CC=${CXX%g++}gcc ;; *) CC=${CXX%++} ;; esac # (the C compiler beside it: gcc, clang)
mkdir -p $O
# The screen reader (port/src/gs/ui_a11y.cpp, a11y.c, plat_settings.c) needs Dear ImGui's item hooks:
# IMGUI_ENABLE_TEST_ENGINE. An object older than this script is built again (one from before lacks them).
for f in $I/imgui.cpp $I/imgui_draw.cpp $I/imgui_tables.cpp $I/imgui_widgets.cpp $I/imgui_impl_sdl3.cpp $I/imgui_impl_sdlgpu3.cpp $I/imgui_impl_opengl3.cpp port/setup/native.cpp port/setup/setup.cpp port/src/gs/ui_a11y.cpp; do
    o=$O/$(basename "$f" .cpp).o
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ port/setup/native.h -nt "$o" ] || [ port/src/gs/ui.h -nt "$o" ] || [ port/setup/build.sh -nt "$o" ]; then
        $CXX -std=c++17 -O2 -w -DIMGUI_ENABLE_TEST_ENGINE -I$I -Iport/src/gs $SDL -c "$f" -o "$o" || exit 1
    fi
done
for f in port/src/gs/a11y.c port/src/plat_settings.c; do
    o=$O/$(basename "$f" .c).o
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ port/setup/build.sh -nt "$o" ]; then
        $CC -O2 -w $SDL -c "$f" -o "$o" || exit 1
    fi
done
# -Wl,-Bdynamic for SDL3 only: the rest (C++ runtime, threads) is linked in
if [ "$1" = win ]; then
    $CXX -o $OUT $O/*.o $SDL -Wl,-Bdynamic -lSDL3 -Wl,-Bstatic -lstdc++ -lwinpthread -static-libgcc -mwindows && echo "built $OUT"
else
    $CXX -o $OUT $O/*.o $LIBS && echo "built ./$OUT"
fi
