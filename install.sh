#!/usr/bin/env bash
# WartBURG installer — Secure Boot capable, interactive target selection.
#
#   curl -fsSL https://raw.githubusercontent.com/goldsteal/WartBURG/wartburg/install.sh | sudo bash
#
# What it does:
#   * resolves the signed release artifacts (local ./dist or a GitHub Release),
#   * lets you SELECT where to install — a USB stick, an existing EFI System
#     Partition (alongside your current bootloader), or OVERRIDE the fallback
#     bootloader (\EFI\BOOT\BOOTX64.EFI, with a backup),
#   * lays down shim -> grubx64.efi (wartburg embedded) + grub.cfg + font,
#   * enrolls the WartBURG MOK (mokutil) so it boots with Secure Boot ON.
#
# Nothing is written without an explicit pick and a typed confirmation.
# Re-run with --dry-run to see exactly what it would do (zero writes).
set -euo pipefail

REPO="goldsteal/WartBURG"
BRANCH="wartburg"
ESP_GUID="c12a7328-f81f-11d2-ba4b-00a0c93ec93b"

DRY=0; DIST=""; VERSION="latest"; ASSUME_YES=0
while [ $# -gt 0 ]; do case "$1" in
  --dry-run) DRY=1 ;;
  --dist) DIST="$2"; shift ;;
  --dist=*) DIST="${1#*=}" ;;
  --version) VERSION="$2"; shift ;;
  --version=*) VERSION="${1#*=}" ;;
  --yes|-y) ASSUME_YES=1 ;;
  -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
  *) echo "unknown arg: $1" >&2; exit 2 ;;
esac; shift; done

c()  { printf '\033[%sm' "$1"; }
say(){ printf '%s\n' "$*"; }
hd() { printf '\n%s== %s ==%s\n' "$(c '1;36')" "$*" "$(c 0)"; }
warn(){ printf '%s[!] %s%s\n' "$(c '1;33')" "$*" "$(c 0)" >&2; }
die(){ printf '%s[x] %s%s\n' "$(c '1;31')" "$*" "$(c 0)" >&2; exit 1; }
run(){ if [ "$DRY" = 1 ]; then printf '   %s(dry)%s %s\n' "$(c '2')" "$(c 0)" "$*"; else eval "$@"; fi; }
ask(){ # ask "prompt" -> echoes the line the user typed (reads from the terminal, not the curl pipe)
  local r; printf '%s ' "$1" >/dev/tty; read -r r </dev/tty; printf '%s' "$r"; }

[ "$DRY" = 1 ] || [ "$(id -u)" = 0 ] || die "run as root (sudo) — or pass --dry-run to preview."
for t in lsblk mount umount mokutil; do command -v "$t" >/dev/null || warn "missing tool: $t"; done

# ---------------------------------------------------------------- artifacts
resolve_artifacts() {
  if [ -z "$DIST" ]; then
    local self; self="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)"
    [ -n "$self" ] && [ -f "$self/dist/SHA256SUMS" ] && DIST="$self/dist"
  fi
  if [ -n "$DIST" ]; then
    [ -f "$DIST/SHA256SUMS" ] || die "no SHA256SUMS in --dist $DIST"
    hd "Using local artifacts: $DIST"
  else
    command -v curl >/dev/null || die "curl needed to download release"
    local tmp; tmp="$(mktemp -d)"; DIST="$tmp/dist"; mkdir -p "$DIST"
    local base
    if [ "$VERSION" = latest ]; then base="https://github.com/$REPO/releases/latest/download"
    else base="https://github.com/$REPO/releases/download/$VERSION"; fi
    hd "Downloading WartBURG release ($VERSION)"
    curl -fsSL "$base/wartburg-dist.tar.gz" | tar -xz -C "$DIST" --strip-components=1 \
      || die "release download failed (is the repo public / version $VERSION published?)"
  fi
  ( cd "$DIST" && sha256sum -c SHA256SUMS >/dev/null ) || die "artifact checksum mismatch — refusing to install"
  for f in EFI/BOOT/BOOTX64.EFI EFI/BOOT/grubx64.efi EFI/BOOT/mmx64.efi \
           EFI/BOOT/WartBURG-MOK.cer boot/grub/grub.cfg boot/grub/fonts/unicode.pf2; do
    [ -f "$DIST/$f" ] || die "artifact missing: $f"
  done
  say "[ok] artifacts verified (sha256)."
}

# ---------------------------------------------------------------- detection
# Build parallel arrays of candidate targets.
declare -a T_PART T_KIND T_DESC
detect_targets() {
  local path parttype size fstype rm tran mp model
  while IFS='|' read -r path parttype size fstype rm tran mp model; do
    [ -z "$path" ] && continue
    if [ "$parttype" = "$ESP_GUID" ] || { [ "$fstype" = vfat ] && [ "$rm" = 1 ]; }; then
      local kind desc
      if [ "$tran" = usb ] || [ "$rm" = 1 ]; then kind=usb-esp; else kind=int-esp; fi
      desc="$path  ${size:-?}  ${fstype:-?}  [$([ "$rm" = 1 ] && echo removable || echo internal) ${tran:-?}]  ${model:-}${mp:+  mounted:$mp}"
      T_PART+=("$path"); T_KIND+=("$kind"); T_DESC+=("$desc")
    fi
  done < <(lsblk -rno PATH,PARTTYPE,SIZE,FSTYPE,RM,TRAN,MOUNTPOINT,MODEL | tr ' ' '|' | sed 's/||*/|/g')
  # whole removable disks (offer to format a fresh ESP)
  local d
  while IFS='|' read -r path size rm tran model type; do
    [ "$type" = disk ] && [ "$rm" = 1 ] || continue
    T_PART+=("$path"); T_KIND+=("usb-format")
    T_DESC+=("$path  ${size:-?}  [removable ${tran:-?} disk]  ${model:-}  -> FORMAT a fresh ESP (ERASES the stick)")
  done < <(lsblk -rno PATH,SIZE,RM,TRAN,MODEL,TYPE -d | tr ' ' '|' | sed 's/||*/|/g')
}

# ---------------------------------------------------------------- layout copy
copy_layout() { # $1 = mounted ESP root, $2 = mode (alongside|override|removable)
  local root="$1" mode="$2"
  run "mkdir -p '$root/EFI/BOOT' '$root/EFI/WartBURG' '$root/boot/grub/fonts'"
  # vendor copy is always self-contained
  run "cp '$DIST/EFI/BOOT/grubx64.efi'        '$root/EFI/WartBURG/grubx64.efi'"
  run "cp '$DIST/EFI/BOOT/mmx64.efi'          '$root/EFI/WartBURG/mmx64.efi'"
  run "cp '$DIST/EFI/BOOT/BOOTX64.EFI'        '$root/EFI/WartBURG/shimx64.efi'"
  run "cp '$DIST/EFI/BOOT/WartBURG-MOK.cer'   '$root/EFI/WartBURG/WartBURG-MOK.cer'"
  run "cp '$DIST/boot/grub/grub.cfg'          '$root/boot/grub/grub.cfg'"
  run "cp '$DIST/boot/grub/fonts/unicode.pf2' '$root/boot/grub/fonts/unicode.pf2'"
  if [ "$mode" = override ] || [ "$mode" = removable ]; then
    if [ -f "$root/EFI/BOOT/BOOTX64.EFI" ] && [ "$mode" = override ]; then
      warn "backing up existing \\EFI\\BOOT\\BOOTX64.EFI -> BOOTX64.EFI.wartburg.bak"
      run "cp -n '$root/EFI/BOOT/BOOTX64.EFI' '$root/EFI/BOOT/BOOTX64.EFI.wartburg.bak'"
    fi
    run "cp '$DIST/EFI/BOOT/BOOTX64.EFI'  '$root/EFI/BOOT/BOOTX64.EFI'"   # shim as fallback loader
    run "cp '$DIST/EFI/BOOT/grubx64.efi'  '$root/EFI/BOOT/grubx64.efi'"
    run "cp '$DIST/EFI/BOOT/mmx64.efi'    '$root/EFI/BOOT/mmx64.efi'"
    run "cp '$DIST/EFI/BOOT/WartBURG-MOK.cer' '$root/EFI/BOOT/WartBURG-MOK.cer'"
  fi
}

add_boot_entry() { # $1 = disk, $2 = partnum  -> NVRAM entry pointing at vendor shim
  command -v efibootmgr >/dev/null || { warn "efibootmgr missing — skipping NVRAM boot entry"; return; }
  run "efibootmgr --create --disk '$1' --part '$2' --label 'WartBURG' --loader '\\EFI\\WartBURG\\shimx64.efi'"
}

enroll_mok() {
  command -v mokutil >/dev/null || { warn "mokutil missing — enroll $DIST/EFI/BOOT/WartBURG-MOK.cer manually"; return; }
  if mokutil --test-key "$DIST/EFI/BOOT/WartBURG-MOK.cer" 2>/dev/null | grep -q "is already enrolled"; then
    say "[ok] WartBURG MOK already enrolled."; return
  fi
  hd "Enroll WartBURG key for Secure Boot"
  say "mokutil will ask you to set a one-time password. On the NEXT reboot a blue"
  say "MokManager screen appears: choose 'Enroll MOK' -> 'Continue' and enter that password."
  run "mokutil --import '$DIST/EFI/BOOT/WartBURG-MOK.cer'"
}

# ---------------------------------------------------------------- main
resolve_artifacts
detect_targets
hd "Select an install target"
if [ "${#T_PART[@]}" = 0 ]; then die "no EFI System Partition or removable disk found. Plug in a USB stick and retry."; fi
i=0; while [ "$i" -lt "${#T_PART[@]}" ]; do printf '  %2d) %s\n' "$((i+1))" "${T_DESC[$i]}"; i=$((i+1)); done
sel="$(ask 'Enter the number of the target:')"
[[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#T_PART[@]}" ] || die "invalid selection."
idx=$((sel-1)); PART="${T_PART[$idx]}"; KIND="${T_KIND[$idx]}"
say "selected: ${T_DESC[$idx]}"

# choose mode
case "$KIND" in
  usb-format)
    MODE=removable
    warn "This ERASES $PART and creates a fresh FAT32 ESP on it."
    [ "$ASSUME_YES" = 1 ] || [ "$(ask "Type the device path '$PART' to confirm:")" = "$PART" ] || die "confirmation mismatch — aborted."
    # partition + format a fresh ESP
    run "sgdisk --zap-all '$PART'"
    run "sgdisk -n 1:0:0 -t 1:ef00 -c 1:'WartBURG' '$PART'"
    PARTDEV="$(lsblk -rno PATH "$PART" | sed -n 2p)"; [ "$DRY" = 1 ] && PARTDEV="${PART}1"
    run "mkfs.vfat -F32 -n WARTBURG '$PARTDEV'"
    TARGET_PART="$PARTDEV"
    ;;
  int-esp)
    say "Install MODE for this internal ESP:"
    say "  a) alongside  — add a 'WartBURG' UEFI boot entry, leave existing bootloaders intact"
    say "  o) override   — replace \\EFI\\BOOT\\BOOTX64.EFI fallback loader (backs up the old one)"
    m="$(ask 'Choose a/o:')"; case "$m" in a|A) MODE=alongside;; o|O) MODE=override;; *) die "invalid mode.";; esac
    warn "Target is an INTERNAL disk partition ($PART). This modifies your real boot setup."
    [ "$ASSUME_YES" = 1 ] || [ "$(ask "Type 'INSTALL' to proceed:")" = INSTALL ] || die "confirmation mismatch — aborted."
    TARGET_PART="$PART"
    ;;
  usb-esp)
    MODE=removable
    [ "$ASSUME_YES" = 1 ] || [ "$(ask "Install onto $PART? Type 'yes':")" = yes ] || die "aborted."
    TARGET_PART="$PART"
    ;;
esac

# mount + copy
MP="$(mktemp -d)"
cleanup(){ mountpoint -q "$MP" 2>/dev/null && umount "$MP" 2>/dev/null || true; rmdir "$MP" 2>/dev/null || true; }
trap cleanup EXIT
EXISTING_MP="$(lsblk -rno MOUNTPOINT "$TARGET_PART" 2>/dev/null | sed -n 1p || true)"
if [ -n "$EXISTING_MP" ] && [ "$DRY" != 1 ]; then ROOT="$EXISTING_MP"; say "(using existing mount $ROOT)";
else run "mount '$TARGET_PART' '$MP'"; ROOT="$MP"; [ "$DRY" = 1 ] && ROOT="$MP(=$TARGET_PART)"; fi

hd "Writing WartBURG to $TARGET_PART ($MODE)"
copy_layout "$ROOT" "$MODE"

if [ "$MODE" = alongside ]; then
  disk="$(lsblk -rno PKNAME "$TARGET_PART" 2>/dev/null | sed -n 1p || true)"; pnum="$(echo "$TARGET_PART" | grep -o '[0-9]*$')"
  add_boot_entry "/dev/$disk" "$pnum"
fi

[ "$DRY" != 1 ] && sync
cleanup; trap - EXIT

# MOK enrollment: only enroll into THIS machine's firmware when we are installing
# to THIS machine (internal ESP). For a removable target built for another machine
# (e.g. the laptop), the key must be enrolled THERE — shim auto-launches MokManager
# on first boot to enroll WartBURG-MOK.cer from the stick.
case "$MODE" in
  alongside|override) enroll_mok ;;
  removable) : ;;
esac

hd "Done"
case "$MODE" in
  removable)
    say "Boot the TARGET machine from this device via its firmware boot menu (F12/F9/Esc)."
    say "Secure Boot ON: on first boot shim launches MokManager — choose 'Enroll key"
    say "from disk', pick \\EFI\\BOOT\\WartBURG-MOK.cer, confirm, then reboot.";;
  alongside) say "A 'WartBURG' entry was added to UEFI boot order. Reboot to use it.";;
  override)  say "WartBURG is now the fallback bootloader. Reboot to use it (old one backed up).";;
esac
