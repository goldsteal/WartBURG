#!/usr/bin/env bash
# WartBURG universal installer — compiles & installs newest GRUB + the WartBURG
# module from source, on any Linux, then wires up a reversible side-by-side boot
# entry. Designed for: curl -fsSL https://<host>/install.sh | bash
#
# SAFETY: side-by-side by default. It does NOT replace your distro's bootloader;
# it installs a separate EFI entry (--bootloader-id=WartBURG) and backs up the
# ESP + grub.cfg first. Your existing GRUB stays as the fallback. Making WartBURG
# the default boot entry is a separate, explicit step (--set-default).
#
# THIS IS UNAUDITED, LLM-WRITTEN CODE THAT TOUCHES YOUR BOOTLOADER.
# Review it, and test in a VM before running on a machine you care about.
set -euo pipefail

# ---- config (override via env or flags) ------------------------------------
WB_REPO="${WB_REPO:-https://github.com/goldsteal/WartBURG.git}"  # GitHub mirror of the fork
WB_REF="${WB_REF:-wartburg}"          # branch/tag to build
WB_SRC="${WB_SRC:-}"                  # use an existing local source tree (skip clone)
WB_PREFIX="${WB_PREFIX:-/usr/local}"  # make install prefix for the new GRUB
WB_MODE="${WB_MODE:-sidebyside}"      # sidebyside | replace
WB_BLID="${WB_BLID:-WartBURG}"        # EFI bootloader-id
WB_THEME_DIR="${WB_THEME_DIR:-}"      # optional BURG theme dir to install as default
WB_GFXMODE="${WB_GFXMODE:-1920x1080,1280x800,1024x768,800x600,auto}"  # pinned mode list (tried in order); avoids blind 'auto' GOP black-screens
DRY_RUN=0; ASSUME_YES=0; SET_DEFAULT=0; SKIP_SB=0; JOBS="$(nproc 2>/dev/null || echo 2)"

usage(){ sed -n '2,18p' "$0"; cat <<EOF

Flags:
  --repo URL     fork repo (default: \$WB_REPO)
  --ref REF      branch/tag (default: $WB_REF)
  --src DIR      build from an existing local tree instead of cloning
  --mode M       sidebyside (default) | replace
  --theme DIR    install this BURG theme dir as the default
  --gfxmode M    pinned gfxmode list for the menu (default: \$WB_GFXMODE)
  --set-default  also make WartBURG the default EFI boot entry
  --no-sb        skip Secure Boot signing/MOK even if SB is on
  --dry-run      print actions, change nothing
  -y, --yes      no prompts
EOF
}
while [ $# -gt 0 ]; do case "$1" in
  --repo) WB_REPO="$2"; shift 2;; --ref) WB_REF="$2"; shift 2;;
  --src) WB_SRC="$2"; shift 2;; --mode) WB_MODE="$2"; shift 2;;
  --theme) WB_THEME_DIR="$2"; shift 2;; --gfxmode) WB_GFXMODE="$2"; shift 2;;
  --set-default) SET_DEFAULT=1; shift;;
  --no-sb) SKIP_SB=1; shift;; --dry-run) DRY_RUN=1; shift;;
  -y|--yes) ASSUME_YES=1; shift;; -h|--help) usage; exit 0;;
  *) echo "unknown flag: $1" >&2; usage; exit 2;; esac; done

# ---- helpers ---------------------------------------------------------------
c(){ printf '\033[1;36m[wartburg]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[wartburg] WARN:\033[0m %s\n' "$*" >&2; }
die(){ printf '\033[1;31m[wartburg] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }
run(){ if [ "$DRY_RUN" = 1 ]; then printf '   + %s\n' "$*"; else eval "$@"; fi; }
confirm(){ [ "$ASSUME_YES" = 1 ] && return 0; read -rp "$1 [y/N] " a; [ "${a:-}" = y ] || [ "${a:-}" = Y ]; }

SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
need_sudo(){ if [ -n "$SUDO" ] && ! sudo -n true 2>/dev/null; then c "sudo password may be requested"; fi; return 0; }

# Bedrock: pin build/grub tooling to the arch stratum (only self-consistent one)
STRAT=""
if command -v brl >/dev/null 2>&1 || [ -d /bedrock ]; then
  STRAT="strat arch"; c "Bedrock Linux detected -> build under 'strat arch'"
fi

# ---- detect ----------------------------------------------------------------
detect_os(){
  . /etc/os-release 2>/dev/null || true
  OS_ID="${ID:-unknown}"; OS_LIKE="${ID_LIKE:-}"
  case " $OS_ID $OS_LIKE " in
    *debian*|*ubuntu*) PKG=apt;;
    *fedora*|*rhel*|*centos*) PKG=dnf;;
    *arch*) PKG=pacman;;
    *suse*) PKG=zypper;;
    *) PKG="";;
  esac
  c "distro: ${PRETTY_NAME:-$OS_ID}  (pkg mgr: ${PKG:-unknown})"
}
detect_target(){
  case "$(uname -m)" in
    x86_64) ARCH=x86_64;; aarch64|arm64) ARCH=aarch64;;
    riscv64) ARCH=riscv64;; i?86) ARCH=i386;; *) ARCH="$(uname -m)";;
  esac
  if [ -d /sys/firmware/efi ]; then
    FW=efi; PLATFORM=efi
    case "$ARCH" in x86_64) GRUB_DIRNAME=x86_64-efi;; aarch64) GRUB_DIRNAME=arm64-efi;;
      riscv64) GRUB_DIRNAME=riscv64-efi;; *) GRUB_DIRNAME="${ARCH}-efi";; esac
    TARGET="$ARCH"
  else
    FW=bios; PLATFORM=pc; GRUB_DIRNAME=i386-pc; TARGET=i386
  fi
  c "arch: $ARCH   firmware: $FW   grub platform: $GRUB_DIRNAME"
}
detect_esp(){
  ESP=""
  if [ "$FW" = efi ]; then
    ESP="$(findmnt -no TARGET /boot/efi 2>/dev/null || findmnt -no TARGET /efi 2>/dev/null || true)"
    [ -z "$ESP" ] && ESP="$(findmnt -rno TARGET,FSTYPE | awk '$2=="vfat"{print $1; exit}')"
    [ -z "$ESP" ] && warn "no mounted ESP found (looked at /boot/efi, /efi, vfat mounts)"
    c "ESP: ${ESP:-NONE}"
  fi
}
detect_sb(){
  SB=unknown
  if command -v mokutil >/dev/null 2>&1; then
    case "$(mokutil --sb-state 2>/dev/null)" in
      *enabled*) SB=on;; *disabled*) SB=off;; *) SB=unknown;;
    esac
  fi
  c "Secure Boot: $SB"
}

# ---- dependencies ----------------------------------------------------------
install_deps(){
  c "installing build dependencies ($PKG)"
  case "$PKG" in
    apt) PKGS="build-essential git autoconf automake autopoint gettext bison flex pkg-config python3 gawk libdevmapper-dev liblzma-dev libfreetype-dev fonts-unifont efibootmgr dosfstools mtools"
         [ "$SB" = on ] && PKGS="$PKGS sbsigntool mokutil"
         run "$SUDO apt-get update -qq"
         run "DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends $PKGS";;
    dnf) PKGS="@development-tools git autoconf automake gettext-devel bison flex pkgconf-pkg-config python3 gawk device-mapper-devel xz-devel freetype-devel unifont-fonts efibootmgr dosfstools mtools"
         [ "$SB" = on ] && PKGS="$PKGS sbsigntools mokutil"
         run "$SUDO dnf install -y $PKGS";;
    pacman) PKGS="base-devel git autoconf automake gettext bison flex pkgconf python gawk device-mapper xz freetype2 unifont efibootmgr dosfstools mtools"
         [ "$SB" = on ] && PKGS="$PKGS sbsigntools mokutil"
         run "$SUDO pacman -Sy --needed --noconfirm $PKGS";;
    zypper) PKGS="-t pattern devel_basis; git autoconf automake gettext-tools bison flex pkg-config python3 gawk device-mapper-devel xz-devel freetype2-devel unifont efibootmgr dosfstools mtools"
         run "$SUDO zypper install -y $PKGS";;
    *) warn "unknown package manager — ensure a C toolchain + autotools + bison/flex/python3 + unifont are installed";;
  esac
}

# ---- fetch + build ---------------------------------------------------------
fetch_source(){
  if [ -n "$WB_SRC" ]; then SRC="$WB_SRC"; c "using local source: $SRC"; return; fi
  SRC="$(mktemp -d)/WartBURG"
  c "cloning $WB_REPO ($WB_REF)"
  run "git clone --depth 1 --branch '$WB_REF' '$WB_REPO' '$SRC'"
}
build_grub(){
  c "building newest GRUB + WartBURG for $GRUB_DIRNAME (this takes a few minutes)"
  ( cd "$SRC"
    [ -x configure ] || run "$STRAT ./bootstrap"
    run "$STRAT ./configure --prefix='$WB_PREFIX' --with-platform=$PLATFORM --target=$TARGET"
    run "$STRAT make -j$JOBS"
    [ "$DRY_RUN" = 1 ] || [ -f grub-core/wartburg.mod ] || die "wartburg.mod was not built"
  )
  c "build OK"
}
install_grub(){
  c "installing GRUB to $WB_PREFIX (make install)"
  ( cd "$SRC"; run "$SUDO $STRAT make install" )
}

# ---- backup + Secure Boot --------------------------------------------------
backup(){
  TS="$(date +%Y%m%d-%H%M%S)"; BK="/var/backups/wartburg-$TS"
  c "backing up ESP + grub.cfg -> $BK"
  run "$SUDO mkdir -p '$BK'"
  [ -n "${ESP:-}" ] && [ -d "$ESP/EFI" ] && run "$SUDO cp -a '$ESP/EFI' '$BK/EFI'"
  for f in /boot/grub/grub.cfg /boot/grub2/grub.cfg; do
    [ -f "$f" ] && run "$SUDO cp -a '$f' '$BK/'"
  done
  c "backup done (restore by copying these back if needed)"
}
setup_secureboot(){
  [ "$FW" = efi ] && [ "$SB" = on ] || return 0
  [ "$SKIP_SB" = 1 ] && { warn "SB is ON but --no-sb set: the self-built GRUB will NOT boot until you sign+enroll or disable SB"; return 0; }
  c "Secure Boot is ON -> signing the new GRUB with a MOK key (shim will then trust it; modules load unsigned under our own grub)"
  local KD="/var/lib/wartburg"; local KEY="$KD/MOK.key" CRT="$KD/MOK.crt" DER="$KD/MOK.cer"
  run "$SUDO mkdir -p '$KD'"
  if [ "$DRY_RUN" = 0 ] && [ ! -f "$KEY" ]; then
    run "$SUDO openssl req -new -x509 -newkey rsa:2048 -keyout '$KEY' -out '$CRT' -nodes -days 3650 -subj '/CN=WartBURG MOK/'"
    run "$SUDO openssl x509 -in '$CRT' -outform DER -out '$DER'"
  fi
  command -v sbsign >/dev/null 2>&1 || die "sbsign (sbsigntool) required for SB signing"
  local EFI="$ESP/EFI/$WB_BLID/grub${ARCH_EFI_SUFFIX}.efi"
  SB_KEY="$KEY"; SB_CRT="$CRT"; SB_DER="$DER"   # used post-install to sign the placed binary
  SB_PENDING=1
}
enroll_mok(){
  [ "${SB_PENDING:-0}" = 1 ] || return 0
  c "enrolling the WartBURG MOK key (you'll set a one-time password, confirmed at next reboot)"
  run "$SUDO mokutil --import '$SB_DER'"
  MOK_ENROLLED=1
}

# ---- install GRUB to ESP + wire up -----------------------------------------
efi_suffix(){ case "$ARCH" in x86_64) echo x64;; aarch64) echo aa64;; riscv64) echo riscv64;; i386) echo ia32;; *) echo "$ARCH";; esac; }
do_install(){
  local GI="$WB_PREFIX/sbin/grub-install"; [ -x "$GI" ] || GI="grub-install"
  if [ "$FW" = efi ]; then
    local args="--target=${ARCH}-efi --efi-directory='$ESP' --boot-directory=/boot --recheck"
    # Side-by-side installs need a real EFI entry so --set-default and the
    # firmware picker can find WartBURG. The existing distro entry remains in
    # BootOrder unless the caller explicitly asks for WartBURG as default.
    [ "$WB_MODE" = sidebyside ] && args="$args --bootloader-id='$WB_BLID'"
    [ "$WB_MODE" = sidebyside ] || args="$args --bootloader-id='$WB_BLID'"
    c "grub-install ($WB_MODE) -> $ESP"
    run "$SUDO $STRAT $GI $args"
  else
    local disk; disk="$(lsblk -no PKNAME "$(findmnt -no SOURCE /boot 2>/dev/null || findmnt -no SOURCE /)" 2>/dev/null | head -1)"
    c "grub-install (BIOS) -> /dev/${disk:-sda}"
    run "$SUDO $STRAT $GI --boot-directory=/boot /dev/${disk:-sda}"
  fi
  # place the WartBURG module + sign the EFI binary if SB
  local DST="/boot/grub/$GRUB_DIRNAME"; [ -d /boot/grub2 ] && DST="/boot/grub2/$GRUB_DIRNAME"
  run "$SUDO mkdir -p '$DST'"
  run "$SUDO cp '$SRC/grub-core/wartburg.mod' '$DST/wartburg.mod'"
  if [ "${SB_PENDING:-0}" = 1 ]; then
    local EFI="$ESP/EFI/$WB_BLID/grub$(efi_suffix).efi"
    c "signing $EFI with the MOK key"
    run "$SUDO sbsign --key '$SB_KEY' --cert '$SB_CRT' --output '$EFI' '$EFI'"
    enroll_mok
  fi
}
install_theme(){
  [ -n "$WB_THEME_DIR" ] && [ -d "$WB_THEME_DIR" ] || { warn "no theme provided (--theme DIR); install a BURG theme under /boot/burg/themes/<name>/ to see the menu"; return 0; }
  local name; name="$(basename "$WB_THEME_DIR")"
  c "installing theme '$name' -> /boot/burg/themes/$name"
  run "$SUDO mkdir -p /boot/burg/themes"
  run "$SUDO cp -a '$WB_THEME_DIR' /boot/burg/themes/$name"
  WB_ACTIVE_THEME="/boot/burg/themes/$name/theme"
}
write_cfg(){
  local DST="/boot/grub/$GRUB_DIRNAME"; local CFGROOT=/boot/grub
  [ -d /boot/grub2 ] && { DST="/boot/grub2/$GRUB_DIRNAME"; CFGROOT=/boot/grub2; }
  c "writing WartBURG grub.cfg snippet (loads wartburg + theme, pinned gfxmode)"
  local theme="${WB_ACTIVE_THEME:-/boot/burg/themes/REPLACE_ME/theme}"
  run "$SUDO tee '$CFGROOT/wartburg.cfg' >/dev/null <<EOF
# Loaded by WartBURG's GRUB. Edit 'theme' to point at a BURG theme.
insmod all_video
insmod gfxterm
# Pin the video mode before gfxterm inits: blind 'auto' can pick a GOP mode the
# panel won't sync (alive-but-black). Modes are tried in order; 'auto' is last.
set gfxmode=$WB_GFXMODE
terminal_output gfxterm
insmod wartburg
set theme=$theme
EOF"
  # Auto-wire the snippet so a fresh install renders without hand-editing grub.cfg.
  local SRCLINE='source $prefix/wartburg.cfg'
  if [ "$DRY_RUN" = 1 ]; then
    printf '   + ensure "%s" in %s/grub.cfg\n' "$SRCLINE" "$CFGROOT"
    printf '   + write /etc/grub.d/09_wartburg (survives grub-mkconfig regen)\n'
  else
    if [ -f "$CFGROOT/grub.cfg" ] && ! $SUDO grep -qF "$SRCLINE" "$CFGROOT/grub.cfg"; then
      c "wiring '$SRCLINE' into $CFGROOT/grub.cfg"
      printf '\n# WartBURG: load the wartburg module + theme\n%s\n' "$SRCLINE" | $SUDO tee -a "$CFGROOT/grub.cfg" >/dev/null
    fi
    if [ -d /etc/grub.d ]; then
      c "installing /etc/grub.d/09_wartburg so it survives grub-mkconfig regen"
      printf '#!/bin/sh\necho %s\n' "'$SRCLINE'" | $SUDO tee /etc/grub.d/09_wartburg >/dev/null
      $SUDO chmod +x /etc/grub.d/09_wartburg
    fi
  fi
  c "WartBURG wired into $CFGROOT/grub.cfg (gfxmode pinned: $WB_GFXMODE)."
}
set_default_entry(){
  [ "$SET_DEFAULT" = 1 ] || return 0
  if [ "$DRY_RUN" = 1 ]; then
    c "would set '$WB_BLID' first in the EFI BootOrder (existing entries retained)"
    return 0
  fi
  command -v efibootmgr >/dev/null 2>&1 || die "efibootmgr required for --set-default"
  local listing bootnum order rest
  listing="$($SUDO efibootmgr 2>/dev/null)" || die "efibootmgr could not read EFI variables"
  bootnum="$(printf '%s\n' "$listing" | awk -v id="$WB_BLID" \
    '$1 ~ /^Boot[0-9A-Fa-f]+\*?$/ && index($0,id) { sub(/^Boot/,"",$1); sub(/\*$/, "",$1); print $1; exit }')"
  [ -n "$bootnum" ] || die "EFI entry '$WB_BLID' was not found after install"
  order="$(printf '%s\n' "$listing" | awk '/^BootOrder:/ {sub(/^BootOrder:[[:space:]]*/,""); print; exit}')"
  rest="$(printf '%s\n' "$order" | awk -v b="$bootnum" -F, '{for (i=1;i<=NF;i++) if ($i != b) printf "%s%s", (out++ ? "," : ""), $i}')"
  if [ -n "$rest" ]; then order="$bootnum,$rest"; else order="$bootnum"; fi
  c "setting '$WB_BLID' first in EFI BootOrder (preserving remaining entries)"
  run "$SUDO efibootmgr --bootorder '$order'"
}

# ---- main ------------------------------------------------------------------
main(){
  c "WartBURG universal installer (mode=$WB_MODE, dry-run=$DRY_RUN)"
  [ "$(uname -s)" = Linux ] || die "Linux only"
  detect_os; detect_target; detect_esp; detect_sb; need_sudo
  [ "$FW" = efi ] && [ -z "${ESP:-}" ] && die "UEFI but no ESP found; mount it and retry"
  echo
  c "PLAN: build newest GRUB+WartBURG ($GRUB_DIRNAME) from $WB_REPO@$WB_REF,"
  c "      install $WB_MODE as EFI id '$WB_BLID' (distro bootloader preserved),"
  [ "$SB" = on ] && c "      Secure Boot ON -> sign GRUB with a MOK key + enroll (one reboot)."
  echo
  confirm "Proceed?" || die "aborted by user"
  install_deps
  fetch_source
  build_grub
  backup
  install_grub
  setup_secureboot
  do_install
  install_theme
  write_cfg
  echo
  c "DONE."
  [ "${MOK_ENROLLED:-0}" = 1 ] && c "REBOOT now: at the MOK manager screen, choose 'Enroll MOK' with the password you set."
  c "WartBURG installed side-by-side as EFI entry '$WB_BLID'. Your distro bootloader is untouched."
  set_default_entry
  c "Rollback: restore the backup under /var/backups/wartburg-* and/or 'efibootmgr -b <num> -B' to remove the entry."
}
main "$@"
