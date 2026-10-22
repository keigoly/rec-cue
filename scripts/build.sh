#!/bin/bash
# Mac 上で Windows 用の rec-cue.exe をビルドする (llvm-mingw によるクロスビルド)。
#   scripts/build.sh            → build/rec-cue.exe
#   LLVM_MINGW=<dir> scripts/build.sh
# ツールチェーンの入手は DEVELOPMENT.md §5 を参照。
set -euo pipefail
cd "$(dirname "$0")/.."

TC="${LLVM_MINGW:-$HOME/.local/opt/llvm-mingw-dl/llvm-mingw-20261006-ucrt-macos-universal}"
CXX="$TC/bin/x86_64-w64-mingw32-clang++"
RC="$TC/bin/x86_64-w64-mingw32-windres"
[ -x "$CXX" ] || { echo "llvm-mingw が見つかりません: $TC (LLVM_MINGW で指定)" >&2; exit 1; }

mkdir -p build
python3 tools/check_lines.py res/shiori-lines.txt >/dev/null   # しおりの台詞 (誤りがあれば止まる)
[ -f res/app.ico ] || uv run --quiet --with pillow python3 tools/make_icon.py res/icon-src.png res/app.ico
"$RC" -O coff -I res -i res/app.rc -o build/app.res.o

"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type \
  -municode -mwindows -static -s \
  -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
  src/*.cpp build/app.res.o -o build/rec-cue.exe \
  -ld3d11 -ldxgi -lmfplat -lmfreadwrite -lmfuuid -lmf -lole32 -loleaut32 -luuid -lavrt \
  -lshell32 -lgdi32 -luser32 -lcomctl32 -lpsapi -lpropsys -lwindowscodecs -ld2d1 -ldwrite -ldwmapi

ls -l build/rec-cue.exe
