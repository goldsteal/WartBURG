# Installing WartBURG

This guide covers installing WartBURG onto a **real machine or VM** with the universal
installer, [`packaging/install.sh`](../../packaging/install.sh). For just trying the module on
a throwaway boot image, see *Trying it* in the [README](../../README.md).

> ⚠️ **Read this first.** WartBURG is unaudited, LLM-written code that touches your bootloader.
> The installer is **side-by-side by default** and backs up your ESP, but you should still
> **review `install.sh` and test in a VM** before running it on a machine you care about.
> See [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for hardware caveats.

## What the installer does

`install.sh` builds the newest GRUB **plus** the `wartburg` module from source, then wires up
a **separate** EFI boot entry — it does **not** replace your distro's bootloader:

- installs a new GRUB under `--bootloader-id=WartBURG` (your existing GRUB stays the default
  and the fallback);
- **backs up** the ESP `EFI/` tree and any `grub.cfg` to `/var/backups/wartburg-<timestamp>/`
  before changing anything;
- places `wartburg.mod` next to the other modules and writes a `wartburg.cfg` snippet that
  loads the module, **pins a graphics mode**, and points `theme` at a BURG theme;
- auto-wires that snippet so a fresh install renders without hand-editing `grub.cfg`
  (appends a `source` line and installs `/etc/grub.d/09_wartburg` so it survives
  `grub-mkconfig` regeneration);
- under Secure Boot, signs the new GRUB with a generated **MOK** key and runs `mokutil --import`
  (one reboot to enroll).

Making WartBURG the **default** boot entry is a separate, explicit step (`--set-default`).

## Requirements

- A Linux host with **UEFI** firmware and a mounted **ESP** (`/boot/efi` or `/efi`); BIOS is
  supported too (GRUB installs to the disk MBR).
- A C toolchain + autotools + `bison`/`flex`/`python3` + `unifont`. The installer installs
  these automatically on apt / dnf / pacman / zypper distros.
- At least one **BURG theme** to actually see a menu — pass `--theme DIR` or stage one under
  `/boot/burg/themes/<name>/`. Without a theme the module loads but draws nothing.

## Quick start

Remote (build from the public GitHub mirror):

```sh
curl -fsSL https://raw.githubusercontent.com/goldsteal/WartBURG/wartburg/packaging/install.sh \
  | bash -s -- --theme /path/to/burg/themes/radiance
```

From a local checkout (skip the clone, build the tree you have):

```sh
WB_SRC=/path/to/WartBURG ./packaging/install.sh --theme grub-core/themes/… --dry-run   # preview
WB_SRC=/path/to/WartBURG ./packaging/install.sh --theme /path/to/theme                 # do it
```

`--dry-run` prints every action and changes nothing — run it first.

## Flags

| Flag | Effect |
|------|--------|
| `--repo URL` | fork repo to clone (default: the GitHub mirror) |
| `--ref REF` | branch/tag to build (default: `wartburg`) |
| `--src DIR` | build from an existing local tree instead of cloning |
| `--mode M` | `sidebyside` (default) or `replace` |
| `--theme DIR` | install this BURG theme dir and make it the default |
| `--gfxmode M` | pinned `gfxmode` list for the menu (default: `1920x1080,1280x800,1024x768,800x600,auto`) |
| `--set-default` | also make WartBURG the default EFI boot entry |
| `--no-sb` | skip Secure Boot signing/MOK even if SB is on |
| `--dry-run` | print actions, change nothing |
| `-y`, `--yes` | no prompts |

Most flags are also settable via environment (`WB_REPO`, `WB_REF`, `WB_SRC`, `WB_MODE`,
`WB_BLID`, `WB_THEME_DIR`, `WB_GFXMODE`).

### About `--gfxmode`

The default is a **list**, tried in order, with `auto` last as a fallback. This avoids a blind
`gfxmode=auto` picking a mode the panel won't sync. Pin a single mode (e.g. `--gfxmode
1920x1080`) if you know your display. **Note:** this does **not** help the cold-boot modeset
race described in [KNOWN-ISSUES.md](KNOWN-ISSUES.md) — that is a timing problem, not a
wrong-mode problem.

## Secure Boot

If `mokutil --sb-state` reports SB **on**, the installer:

1. generates a MOK key pair under `/var/lib/wartburg/`,
2. `sbsign`s the placed GRUB binary,
3. runs `mokutil --import` so you can **Enroll MOK** at the next reboot (you set a one-time
   password, confirmed in the MOK manager screen on reboot).

Pass `--no-sb` to skip this; the self-built GRUB then won't boot until you sign+enroll or
disable SB.

## Verifying / rollback

- The new entry shows up in `efibootmgr` as **WartBURG**; your distro entry is untouched.
- **Rollback:** restore the backup from `/var/backups/wartburg-<timestamp>/` and/or remove the
  EFI entry with `efibootmgr -b <num> -B`. Removing the module at runtime is just
  `rmmod wartburg` (restores the previous menu; patches no core file).

## Testing in a VM first (recommended)

The project ships a headless QEMU/OVMF harness (`verify-hostinstall.sh`,
`run-wartburg-local.sh`) that replicates an ESP payload and screendumps the menu — use it to
confirm a theme renders before installing on metal. A fresh UEFI VM (OVMF) is the safest place
to run `install.sh` end-to-end, and on a well-behaved virtual GOP the menu renders reliably.

See [KNOWN-ISSUES.md](KNOWN-ISSUES.md) before installing on bare metal.
