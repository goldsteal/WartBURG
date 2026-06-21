#!/usr/bin/env bash
# register-icons.sh — register WartBURG's extra distro icon classes into a BURG
# theme's four icon map files (grey/hover/small/large). BURG's icon system is an
# explicit class->file map, NOT a directory scan: an unregistered class falls
# through the menu entry's comma class-list to `linux` (Tux), so dropping a PNG
# in is not enough. Idempotent: always rebuilds the additions from the *.orig
# snapshot it makes on first run.
#
# Usage: ./register-icons.sh BURG_ICONS_DIR
#   BURG_ICONS_DIR = a BURG theme's icons/ dir containing the grey/hover/small/
#   large map files (e.g. burg-ref/themes/themes/icons).
set -eu
ICONS="${1:-}"
[ -n "$ICONS" ] || { sed -n '2,12p' "$0"; exit 2; }
[ -d "$ICONS" ] || { echo "no such icons dir: $ICONS" >&2; exit 1; }

# The classes WartBURG adds on top of stock BURG (parrot omitted: no free logo).
CLASSES=(artix garuda endeavouros manjaro t2 bedrock kali popos elementary mx
  zorin deepin devuan neon pureos antix tails kubuntu xubuntu lubuntu raspbian
  lmde sparky q4os peppermint bodhi steamos1 steamos2 steamos3
  snapshots)   # non-distro: btrfs/zfs snapshot restore icon (grub-btrfs/snapper)

emit() { # $1 = map name; prints "-<class> { image = ... }" lines
  local m="$1" c
  for c in "${CLASSES[@]}"; do
    case "$m" in
      grey)  printf '  -%s { image = "$$/grey_%s.png" }\n' "$c" "$c";;
      hover) printf '  -%s { image = "$$/grey_%s.png:$$/large_%s.png" }\n' "$c" "$c" "$c";;
      small) printf '  -%s { image = "$$/small_%s.png" }\n' "$c" "$c";;
      large) printf '  -%s { image = "$$/large_%s.png" }\n' "$c" "$c";;
    esac
  done
}

for m in grey hover small large; do
  [ -f "$ICONS/$m" ] || { echo "skip: $ICONS/$m missing"; continue; }
  [ -f "$ICONS/$m.orig" ] || cp "$ICONS/$m" "$ICONS/$m.orig"   # pristine snapshot
  block="$(emit "$m")"
  # Insert the additions just before the default `-image { ... }` entry.
  awk -v block="$block" '
    /^[[:space:]]*-image[[:space:]]*\{/ && !done { print "  # --- WartBURG additions (register-icons.sh) ---"; print block; done=1 }
    { print }
  ' "$ICONS/$m.orig" > "$ICONS/$m"
  echo "$m: +${#CLASSES[@]} classes"
done
