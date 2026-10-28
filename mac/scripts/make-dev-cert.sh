#!/bin/bash
# Make a self-signed code signing identity "Rec Cue Dev" for development builds, in a keychain of its own (the login
# keychain is not touched). macOS ties the screen recording permission to the app's signature; an ad hoc signature
# changes with every build, this one does not.
#
#   scripts/make-dev-cert.sh            # once per Mac
#   security delete-keychain ~/Library/Keychains/rec-cue-dev.keychain-db     # to remove it again
#
# The keychain's password is kept in ~/Library/Application Support/Rec Cue/dev/keychain-password (only you can read it)
# so scripts/build.sh can unlock it without asking.
set -euo pipefail
KC="$HOME/Library/Keychains/rec-cue-dev.keychain-db"
DIR="$HOME/Library/Application Support/Rec Cue/dev"
NAME="${REC_CUE_SIGN_ID:-Rec Cue Dev}"
if [ -f "$KC" ]; then
  echo "already made: $KC"
  security find-identity -p codesigning "$KC"
  exit 0
fi
umask 077
mkdir -p "$DIR"
TMP="$(mktemp -d -t rec-cue-cert)"
trap 'rm -rf "$TMP"' EXIT
PW="$(/usr/bin/openssl rand -hex 24)"
P12PW="$(/usr/bin/openssl rand -hex 24)"
cat >"$TMP/req.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
EOF
/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config "$TMP/req.cnf" \
  -keyout "$TMP/key.pem" -out "$TMP/cert.pem" 2>/dev/null
/usr/bin/openssl pkcs12 -export -inkey "$TMP/key.pem" -in "$TMP/cert.pem" -name "$NAME" -out "$TMP/id.p12" \
  -passout "pass:$P12PW"
security create-keychain -p "$PW" "$KC"
printf '%s' "$PW" >"$DIR/keychain-password"
security set-keychain-settings "$KC"    # no automatic locking
security unlock-keychain -p "$PW" "$KC"
security import "$TMP/id.p12" -k "$KC" -P "$P12PW" -T /usr/bin/codesign >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PW" "$KC" >/dev/null
echo "made: $KC"
security find-identity -p codesigning "$KC"
