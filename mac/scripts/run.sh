#!/bin/bash
# Run the built app through LaunchServices (`open`), so macOS sees Rec Cue! itself — not the terminal — as the app
# that records the screen, and print its JSON result. The app stays in the background (it does not take the focus).
#
#   scripts/run.sh --list-devices
#   scripts/run.sh --record --seconds 10 --test-pattern --system auto --mic none --out "<file>.mp4"
#   scripts/run.sh --verify-file "<file>.mp4"
set -euo pipefail
cd "$(dirname "$0")/.."
APP="$(pwd)/build/Rec Cue.app"
[ -d "$APP" ] || { echo "build it first: scripts/build.sh" >&2; exit 1; }
TMP="$(mktemp -d -t rec-cue-run)"
trap 'rm -rf "$TMP"' EXIT
open -n -g -W --stdout "$TMP/stdout" --stderr "$TMP/stderr" "$APP" --args "$@" --json-out "$TMP/result.json"
if [ -s "$TMP/result.json" ]; then
  cat "$TMP/result.json"
else
  echo '{"ok":false,"error":"no result from the app"}'
  cat "$TMP/stderr" >&2
  exit 1
fi
