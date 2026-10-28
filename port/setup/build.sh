#!/bin/sh
# Builds the setup window (port/setup/setup.cpp, native.cpp).
#   port/setup/build.sh        -> ./Tenkaichi3Decomp-setup            (needs g++ and SDL3; Dear ImGui is in the repository)
#   port/setup/build.sh win    -> port/build/Tenkaichi3Decomp-setup.exe
#        on Linux: cross-compiled with mingw-w64 (x86_64-w64-mingw32-g++) and SDL3's mingw development package
#                  unpacked under port/build/win (or its x86_64-w64-mingw32 folder named by BT3_SDL3);
#        on Windows (MSYS2 shell): with that shell's g++ and SDL3.
cd "$(dirname "$0")/../.." || exit 1
I=port/third_party/imgui
python3 port/tools/interface_strings.py || exit 1
if [ "$1" = win ]; then
    O=port/build/setup_win; OUT=port/build/Tenkaichi3Decomp-setup.exe; LIBS="-lSDL3 -static -static-libgcc -static-libstdc++ -mwindows"
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) CXX=g++; SDL="" ;;
        *) CXX=x86_64-w64-mingw32-g++
           S=${BT3_SDL3:-$(ls -d port/build/win/SDL3-*/x86_64-w64-mingw32 2>/dev/null | tail -1)}
           [ -d "$S" ] || { echo "SDL3 for mingw not found: unpack SDL3-devel-*-mingw.tar.gz under port/build/win"; exit 1; }
           SDL="-I$S/include -L$S/lib" ;;
    esac
else
    O=port/build/setup; OUT=Tenkaichi3Decomp-setup; CXX=g++; SDL=""; LIBS="-lSDL3 -lpthread -ldl -static-libstdc++ -static-libgcc -Wl,-rpath,\$ORIGIN/lib"
fi
mkdir -p $O
for f in $I/imgui.cpp $I/imgui_draw.cpp $I/imgui_tables.cpp $I/imgui_widgets.cpp $I/imgui_impl_sdl3.cpp $I/imgui_impl_sdlgpu3.cpp $I/imgui_impl_opengl3.cpp port/setup/native.cpp port/setup/setup.cpp; do
    o=$O/$(basename "$f" .cpp).o
    translation_changed=0
    case "$f" in
        port/setup/setup.cpp|port/setup/native.cpp)
            if [ port/src/gs/ui_localization.h -nt "$o" ] || [ port/build/gen/gs/ui_catalog_generated.h -nt "$o" ]; then translation_changed=1; fi ;;
    esac
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ port/setup/native.h -nt "$o" ] || [ "$translation_changed" = 1 ] || { [ "$f" = port/setup/setup.cpp ] && [ port/setup/game_launch.h -nt "$o" ]; }; then
        $CXX -std=c++17 -O2 -w -I$I -Iport/build/gen/gs $SDL -c "$f" -o "$o" || exit 1
    fi
done
# -Wl,-Bdynamic for SDL3 only: the rest (C++ runtime, threads) is linked in
if [ "$1" = win ]; then
    $CXX -o $OUT $O/*.o $SDL -Wl,-Bdynamic -lSDL3 -Wl,-Bstatic -lstdc++ -lwinpthread -static-libgcc -mwindows && echo "built $OUT"
else
    $CXX -o $OUT $O/*.o $LIBS && echo "built ./$OUT"
fi
