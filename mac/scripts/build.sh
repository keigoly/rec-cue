#!/bin/bash
# Rec Cue! for macOS: build with SwiftPM (the Command Line Tools are enough, no Xcode), put the program into
# "build/Rec Cue.app" and sign it. Warnings fail the build, as with the Windows build.
#
#   scripts/build.sh
#
# Signing: with the development identity from scripts/make-dev-cert.sh the app keeps its screen recording permission
# across rebuilds (macOS ties the permission to the signature). Without it the app is signed ad hoc, and macOS asks
# again after every build.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
APP="$ROOT/build/Rec Cue.app"
KC="$HOME/Library/Keychains/rec-cue-dev.keychain-db"
PWFILE="$HOME/Library/Application Support/Rec Cue/dev/keychain-password"
ID_NAME="${REC_CUE_SIGN_ID:-Rec Cue Dev}"

mkdir -p build
if ! swift build -c release --product rec-cue -Xswiftc -warnings-as-errors >build/swift-build.log 2>&1; then
  grep -E "error:|warning:" build/swift-build.log >&2 || tail -n 40 build/swift-build.log >&2
  echo "build failed (build/swift-build.log)" >&2
  exit 1
fi
# SwiftPM adds the test-library folders of a full Xcode to the link; with only the Command Line Tools the linker
# says they are missing. That one is about the tools, not the code: every other warning stops the build.
QUIRK="ld: warning: search path '/Library/Developer/CommandLineTools/Developer/"
if grep "warning:" build/swift-build.log | grep -vqF "$QUIRK"; then
  grep "warning:" build/swift-build.log | grep -vF "$QUIRK" >&2
  echo "warnings (build/swift-build.log)" >&2
  exit 1
fi
BIN="$(swift build -c release --product rec-cue --show-bin-path)/rec-cue"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/rec-cue"
cp Info.plist "$APP/Contents/Info.plist"
# the guide's pictures and lines, shared with the Windows version, and the Mac's changes to her lines
cp ../res/guide-idle.png ../res/guide-rec.png ../res/shiori-lines.txt shiori-mac.txt "$APP/Contents/Resources/"
plutil -lint -s "$APP/Contents/Info.plist"

SIGN="-"
KCARGS=()
if [ -f "$KC" ]; then
  [ -f "$PWFILE" ] && security unlock-keychain -p "$(cat "$PWFILE")" "$KC"
  HASH="$(security find-identity -p codesigning "$KC" | awk -v n="\"$ID_NAME\"" 'index($0, n) {print $2; exit}')"
  if [ -n "$HASH" ]; then
    SIGN="$HASH"
    KCARGS=(--keychain "$KC")
  fi
fi
codesign --force --sign "$SIGN" ${KCARGS[@]+"${KCARGS[@]}"} "$APP"
codesign --verify --strict "$APP"
if [ "$SIGN" = "-" ]; then WHO="ad hoc (macOS will ask for screen recording again after each build)"; else WHO="$ID_NAME"; fi
echo "built: $APP"
echo "signed: $WHO"
codesign -d -r- "$APP" 2>&1 | sed -n 's/^#* *designated => /requirement: /p'
