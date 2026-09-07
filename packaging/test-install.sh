#!/usr/bin/env bash
# Installer regression checks. The dry-run exercises planning without touching
# an ESP, NVRAM, packages, or the source tree.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

bash "$HERE/install.sh" --src "$ROOT/grub" --dry-run --yes --mode sidebyside \
  --set-default --no-sb > "$OUT"

/bin/grep -Fq "backing up ESP + grub.cfg" "$OUT"
/bin/grep -Fq "grub-install (sidebyside)" "$OUT"
/bin/grep -Fq "would set 'WartBURG' first in the EFI BootOrder" "$OUT"

backup_line="$(/bin/grep -nF "backing up ESP + grub.cfg" "$OUT" | /usr/bin/head -1 | /usr/bin/cut -d: -f1)"
install_line="$(/bin/grep -nF "grub-install (sidebyside)" "$OUT" | /usr/bin/head -1 | /usr/bin/cut -d: -f1)"
test "$backup_line" -lt "$install_line"

if /bin/grep -Fq -- "--no-nvram" "$OUT"; then
  echo "side-by-side install must create an EFI entry" >&2
  exit 1
fi
echo "installer dry-run: PASS"
