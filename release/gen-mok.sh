#!/bin/bash
# gen-mok.sh — generate the WartBURG Secure Boot MOK keypair ONCE.
# Keys land OUTSIDE the git repo ($WARTBURG_KEYDIR, default ../../secureboot-keys)
# so they can never be committed. The .crt/.cer are publishable; guard the .key.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
KEYDIR="${WARTBURG_KEYDIR:-$(cd "$HERE/../.." && pwd)/secureboot-keys}"
command -v strat >/dev/null 2>&1 && STRAT=(strat "${STRATUM:-arch}") || STRAT=()
mkdir -p "$KEYDIR"; chmod 700 "$KEYDIR"

if [ -f "$KEYDIR/WartBURG-MOK.key" ]; then
  echo "[=] $KEYDIR/WartBURG-MOK.key already exists — refusing to overwrite."; exit 0
fi
echo "[*] generating WartBURG-MOK (RSA-2048, 10y, codeSigning) in $KEYDIR"
"${STRAT[@]}" openssl req -new -x509 -newkey rsa:2048 -nodes \
  -keyout "$KEYDIR/WartBURG-MOK.key" -out "$KEYDIR/WartBURG-MOK.crt" \
  -days 3650 -sha256 \
  -subj "/CN=WartBURG Secure Boot MOK/O=WartBURG/" \
  -addext "basicConstraints=critical,CA:FALSE" \
  -addext "keyUsage=digitalSignature" \
  -addext "extendedKeyUsage=codeSigning"
"${STRAT[@]}" openssl x509 -in "$KEYDIR/WartBURG-MOK.crt" -outform DER \
  -out "$KEYDIR/WartBURG-MOK.cer"
chmod 600 "$KEYDIR/WartBURG-MOK.key"
echo "[ok] $(ls "$KEYDIR")"
