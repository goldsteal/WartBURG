#!/usr/bin/env bash
# gh-release.sh — create a GitHub Release and upload the WartBURG artifacts via the
# REST API. Needs a token in $GITHUB_TOKEN (fine-grained: Contents read/write on the
# repo, or a classic token with `repo`). The token is read from the env only — never
# printed, never written to disk.
#
#   GITHUB_TOKEN=*** release/gh-release.sh [TAG]    # default TAG: v0.1.0
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; GRUB_DIR="$(cd "$HERE/.." && pwd)"
REPO="goldsteal/WartBURG"
TAG="${1:-v0.1.0}"
TARGET="${TARGET:-wartburg}"                  # branch/commit the tag points at
TARBALL="$GRUB_DIR/wartburg-dist.tar.gz"
CER="$GRUB_DIR/dist/WartBURG-MOK.cer"
API="https://api.github.com"; UP="https://uploads.github.com"

[ -n "${GITHUB_TOKEN:-}" ] || { echo "[!] set GITHUB_TOKEN (fine-grained: Contents RW)"; exit 1; }
[ -f "$TARBALL" ] || { echo "[!] $TARBALL missing — run release/build-signed.sh first"; exit 1; }
[ -f "$CER" ] || { echo "[!] $CER missing — run release/build-signed.sh first"; exit 1; }
auth=(-H "Authorization: Bearer $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" \
      -H "X-GitHub-Api-Version: 2022-11-28")

notes="First signed WartBURG Secure Boot release (test).

Chain: shim (Microsoft UEFI CA 2011) -> grubx64.efi (WartBURG-MOK) -> wartburg embedded + SBAT.

Install (Secure Boot capable, interactive target picker):
\`\`\`
curl -fsSL https://raw.githubusercontent.com/$REPO/$TARGET/install.sh | sudo bash
\`\`\`
On first boot under Secure Boot, enroll WartBURG-MOK.cer at the MokManager screen."

echo "[*] creating release $TAG on $REPO (target $TARGET)"
resp="$(curl -fsSL "${auth[@]}" -X POST "$API/repos/$REPO/releases" \
  -d "$(printf '{"tag_name":"%s","target_commitish":"%s","name":"WartBURG %s","body":%s,"draft":false,"prerelease":false}' \
        "$TAG" "$TARGET" "$TAG" "$(printf '%s' "$notes" | python3 -c 'import json,sys;print(json.dumps(sys.stdin.read()))')")" \
  )" || { echo "[!] release creation failed (tag exists? token scope?)"; exit 1; }
rid="$(printf '%s' "$resp" | grep -m1 '"id"' | grep -o '[0-9]\+' | head -1)"
[ -n "$rid" ] || { echo "[!] could not parse release id"; printf '%s\n' "$resp" | head; exit 1; }
echo "[*] release id $rid"

upload() { # $1 path, $2 content-type
  local name; name="$(basename "$1")"
  echo "[*] uploading $name"
  curl -fsSL "${auth[@]}" -H "Content-Type: $2" \
    --data-binary @"$1" "$UP/repos/$REPO/releases/$rid/assets?name=$name" >/dev/null \
    && echo "    ok" || { echo "[!] upload $name failed"; exit 1; }
}
upload "$TARBALL" application/gzip
upload "$CER"     application/x-x509-ca-cert

echo
echo "[ok] https://github.com/$REPO/releases/tag/$TAG"
echo "      asset: wartburg-dist.tar.gz  (consumed by install.sh latest)"
