#!/bin/bash
# build-signed.sh — produce the WartBURG Secure Boot release artifacts in dist/.
#
# Chain (what the laptop runs):
#   firmware --(SB, MS cert)--> shimx64.efi (BOOTX64.EFI)
#           --(MOK)--> grubx64.efi  [wartburg + deps EMBEDDED + SBAT, MOK-signed]
#
# Under Secure Boot grub will NOT insmod unsigned .mod files from disk, so the
# wartburg module and everything the menu needs are baked into grubx64.efi here.
#
# Bedrock: grub-mkimage / sbsign must run under `strat arch` (consistent toolchain).
#
#   ./build-signed.sh           # mkimage + sign + stage dist/ (assumes grub already built)
#   ./build-signed.sh --build   # also (re)build grub first (make -j under strat arch)
#
# Key: signed with the maintainer MOK at $WARTBURG_KEYDIR (default ../../secureboot-keys,
# i.e. OUTSIDE the git repo). Generate once with release/gen-mok.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"          # .../grub/release
GRUB_DIR="$(cd "$HERE/.." && pwd)"             # .../grub
DIST="$GRUB_DIR/dist"
KEYDIR="${WARTBURG_KEYDIR:-$(cd "$GRUB_DIR/.." && pwd)/secureboot-keys}"
FONT="${FONT:-/usr/share/grub/unicode.pf2}"
DO_BUILD=0; [ "${1:-}" = "--build" ] && DO_BUILD=1
command -v strat >/dev/null 2>&1 && STRAT=(strat "${STRATUM:-arch}") || STRAT=()

KEY="$KEYDIR/WartBURG-MOK.key"; CRT="$KEYDIR/WartBURG-MOK.crt"; CER="$KEYDIR/WartBURG-MOK.cer"
for f in "$KEY" "$CRT" "$CER"; do
  [ -f "$f" ] || { echo "[!] missing $f — run release/gen-mok.sh first"; exit 1; }
done

# Locate a Microsoft-signed shim + MokManager to use as first stage.
find_first() { for p in "$@"; do [ -f "$p" ] && { echo "$p"; return; }; done; return 1; }
SHIM="$(find_first \
  /usr/lib/shim/shimx64.efi.signed \
  /bedrock/strata/debian/usr/lib/shim/shimx64.efi.signed \
  /usr/share/shim-signed/shimx64.efi)" \
  || { echo "[!] no MS-signed shimx64.efi found (install shim-signed)"; exit 1; }
MM="$(find_first \
  /usr/lib/shim/mmx64.efi \
  /bedrock/strata/debian/usr/lib/shim/mmx64.efi \
  /usr/share/shim-signed/mmx64.efi)" \
  || { echo "[!] no mmx64.efi (MokManager) found"; exit 1; }

if [ "$DO_BUILD" = 1 ]; then
  echo "[*] building grub (strat arch make -j)"
  ( cd "$GRUB_DIR" && "${STRAT[@]}" make -j"$(nproc)" >/dev/null )
fi
[ -f "$GRUB_DIR/grub-core/wartburg.mod" ] || { echo "[!] wartburg.mod missing — build grub first (--build)"; exit 1; }

# Modules embedded into the signed image. wartburg's own deps (bitmap bitmap_scale
# boot font normal trig video) are pulled automatically; we add fs/part/search/boot
# helpers a real menu needs, plus png/jpeg (loaded dynamically at runtime, so they
# MUST be embedded under SB) for future theme assets.
EMBED=(
  part_gpt part_msdos fat ext2 ntfs iso9660
  normal configfile echo test true loadenv search search_fs_uuid search_fs_file search_label
  terminal font video all_video efi_gop gfxterm gfxmenu
  bitmap bitmap_scale png jpeg tga trig
  chain linux halt reboot sleep ls cat help gettext minicmd
  wartburg
)

echo "[*] grub-mkimage -> dist/grubx64.efi (wartburg embedded, SBAT, prefix /boot/grub)"
mkdir -p "$DIST/EFI/BOOT" "$DIST/boot/grub/fonts"
"${STRAT[@]}" "$GRUB_DIR/grub-mkimage" \
  -O x86_64-efi -o "$DIST/grubx64.efi" -p /boot/grub \
  -d "$GRUB_DIR/grub-core" --sbat "$HERE/sbat.csv" \
  "${EMBED[@]}"

echo "[*] sbsign grubx64.efi with WartBURG-MOK"
"${STRAT[@]}" sbsign --key "$KEY" --cert "$CRT" \
  --output "$DIST/grubx64.efi" "$DIST/grubx64.efi"

echo "[*] verify signature"
"${STRAT[@]}" sbverify --cert "$CRT" "$DIST/grubx64.efi"

echo "[*] stage release layout in dist/"
cp "$SHIM" "$DIST/EFI/BOOT/BOOTX64.EFI"      # MS-signed first stage
cp "$DIST/grubx64.efi" "$DIST/EFI/BOOT/grubx64.efi"  # shim's default 2nd stage name
cp "$MM"   "$DIST/EFI/BOOT/mmx64.efi"        # MokManager (enroll screen)
cp "$CER"  "$DIST/EFI/BOOT/WartBURG-MOK.cer" # cert to enroll
cp "$HERE/grub.cfg" "$DIST/boot/grub/grub.cfg"
[ -f "$FONT" ] && cp "$FONT" "$DIST/boot/grub/fonts/unicode.pf2" || echo "[!] font $FONT missing"
# keep a top-level copy of the bare signed grub + cert for release uploads
cp "$CER" "$DIST/WartBURG-MOK.cer"

echo "[*] SHA256SUMS"
( cd "$DIST" && find EFI boot grubx64.efi WartBURG-MOK.cer -type f | sort \
    | xargs sha256sum > SHA256SUMS )

echo "[*] release tarball (GitHub Release asset consumed by install.sh)"
# wrap in a top-level dist/ so install.sh's --strip-components=1 lands files at EFI/.., boot/..
tar -czf "$GRUB_DIR/wartburg-dist.tar.gz" -C "$GRUB_DIR" \
  dist/EFI dist/boot dist/grubx64.efi dist/WartBURG-MOK.cer dist/SHA256SUMS
sha256sum "$GRUB_DIR/wartburg-dist.tar.gz"

echo
echo "[ok] dist/ ready:"; ( cd "$DIST" && find . -type f | sort | sed 's/^/    /' )
echo
echo "Upload as a GitHub Release:  wartburg-dist.tar.gz  (+ WartBURG-MOK.cer)"
echo "Then:  curl -fsSL https://raw.githubusercontent.com/goldsteal/WartBURG/wartburg/install.sh | sudo bash"
