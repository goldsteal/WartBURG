# WartBURG Current State

_Snapshot: 2026-06-20. See [README](../../README.md) for the project overview,
[INSTALL.md](INSTALL.md) for installation, [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for caveats, and
[ROADMAP.md](ROADMAP.md) for what's next._

## Goal

Port BURG's theme engine to modern GRUB 2 as a **removable module** (not a revived fork): a
single `wartburg.mod`, built on a clean upstream GNU GRUB checkout, that renders unmodified
BURG themes itself and transparently delegates GRUB 2 `theme.txt` themes to stock `gfxmenu` —
switchable just by changing the `theme` variable. No GRUB core file is patched; `rmmod
wartburg` fully reverts.

## Repository

| Remote | URL | Role |
|--------|-----|------|
| `upstream` | `https://git.savannah.gnu.org/git/grub.git` | clean GNU GRUB (master, post-2.14 / version string 2.15) |
| `origin` | `gitea-local:goldsteal/WartBURG.git` | self-hosted Gitea fork (primary) |
| `github` | `git@github.com:goldsteal/WartBURG.git` | public mirror (install.sh source, releases) |

## Branch

`wartburg`

## Last known working commit

`135753a6f` — _"docs: add post-1.0 roadmap; mark completed 1.0 gate items"_ (2026-06-20),
HEAD of `wartburg`, pushed to both `origin` and `github`. The working tree is **clean**.
Rebuild `grub-core/wartburg.mod` with `make` before trusting the in-tree copy.

Recent history (newest first):

```
135753a6f docs: add post-1.0 roadmap; mark completed 1.0 gate items
87b8611b8 WartBURG: harden renderer against bad themes
848c759b4 WartBURG: live in-menu theme (F2) and resolution (F3) switch
7e220a58e WartBURG icons: swap thin wordmarks for mark-only logos
86a265d46 WartBURG icons: drop t2's white tile for a transparent mark
e50232773 WartBURG icons: use official Tails drawing logo for tails
1960b21b4 WartBURG icons: use mark-only logos for endeavouros/manjaro/steamos
24004a902 WartBURG: add OS-detection icon pack + reproducible tooling
2a64f191f WartBURG: Bedrock-aware composite stratum icon
509281273 gitignore: never track local AGENTS.md operator runbook
4c75da789 docs+packaging: install guide, known issues, state doc; harden installer
eac9c5d2a WartBURG: map os-prober --class vocabulary to BURG icon classes
a50d232a9 Add GitHub release uploader; fix pinned-version asset URL
```

## New files

Module sources (`grub-core/commands/`):

- `wartburg.c` — module init/fini; the reversible `grub_gfxmenu_try_hook`; BURG⇄GRUB2 dispatcher
- `wartburg_theme.c` — ported BURG `uitree` theme parser
- `wartburg_region.c` — gfx draw backend, bitmap scaling/cache, `font.lst` auto-load
- `wartburg_widget.c` — widget layout/draw engine + scrolling
- `wartburg_ui.c` — component classes: screen, panel, image, text, progressbar, circular_progress, password, edit
- `wartburg_menu.c` — template/parameter machinery + dialog runner
- `wartburg_input.c` — input dispatch: navigation, submenus, timeout, boot, live theme/gfxmode switch
- `wartburg_bedrock.c` — Bedrock-aware composite stratum icon

Headers (`include/grub/`):

- `wartburg_region.h`, `wartburg_theme.h`, `wartburg_widget.h`, `wartburg_bedrock.h`

Packaging & docs (all committed):

- `packaging/install.sh` — universal side-by-side installer (build-from-source, Secure
  Boot/MOK signing, ESP backup, pinned gfxmode, auto-wired `source`/`09_wartburg`)
- `README.md` — project README
- `docs/wartburg/INSTALL.md`, `KNOWN-ISSUES.md`, `STATE.md` (this file), `ROADMAP.md`

## Modified files

- `grub-core/Makefile.core.def` — registers the `wartburg` module (lines ~1699–1707), listing
  all seven `common = commands/wartburg*.c` sources.

That is the **only** upstream GRUB file changed — everything else is additive, and the working
tree is clean (all changes committed and pushed).

## Build procedure

```sh
# On Bedrock Linux, prefix every build/binutils command with `strat arch`.
cd grub
./bootstrap                                       # once, after a fresh clone
./configure --with-platform=efi --target=x86_64   # UEFI / x86_64 (primary target)
make -j"$(nproc)"
# -> grub-core/wartburg.mod
```

- **BIOS:** `./configure --with-platform=pc --target=i386` (builds with zero source changes).
- `wartburg.mod` deps (auto-resolved via `moddep.lst`): `bitmap`, `bitmap_scale`, `trig`,
  `font`, `video`, `normal`.
- Local boot/test harness: `run-wartburg-local.sh` (QEMU/OVMF GUI; `HEADLESS=1` for a
  screendump), `verify-m1.sh` (headless acceptance), `verify-hostinstall.sh` (replicates an
  ESP install into QEMU and screendumps).

## Current status

| Area | State |
|------|-------|
| Custom `wartburg` command / module registers & loads | ✅ |
| Reversible graphical-menu hook (save/restore, no core patch; `rmmod` reverts) | ✅ |
| BURG theme parser (`uitree` port) | ✅ |
| Menu renderer (regions, widget layout, component classes) | ✅ |
| BURG theme rendering | ✅ 16/16 bundled themes render in-theme |
| Navigation (arrows + vim `hjkl`, scroll, `mapkey`/`onkey`) | ✅ |
| Per-OS icons (by `--class`), fonts (`font.lst`), progressbar, circular_progress | ✅ |
| OS-prober `--class` → BURG icon-class mapping + full class-list fallback | ✅ committed |
| OS-detection icon pack + reproducible tooling (`gen-icons.sh`/`register-icons.sh`) | ✅ |
| Bedrock-aware composite stratum icon | ✅ |
| Submenus, message dialogs, password widget | ✅ |
| `e` editor / `c` console (delegated to stock GRUB) | ✅ |
| BURG ⇄ GRUB 2 `theme.txt` coexistence (dispatcher) | ✅ |
| Restricted-entry (`--users`) auth via GRUB (no SB bypass) | ✅ |
| Live theme switch (`F2`) + resolution switch (`F3`), reload-in-place, grubenv-persisted | ✅ |
| F2 theme switch inside delegated GRUB 2 themes (injected hotkey entry) | ✅ |
| Zero-config EFI OS discovery (`wartburg_discover`, known-loader table) | ✅ increment 1 |
| Renderer hardening: graceful unknown-class skip, clamped size math, true center/tiling scale | ✅ |
| x86_64-efi | ✅ verified headless (QEMU/OVMF) |
| i386-pc (BIOS) | ✅ builds, zero source changes |
| Secure Boot install path (shim → MOK-signed GRUB w/ embedded module + SBAT) | ✅ implemented |
| Universal installer (`packaging/install.sh`, side-by-side) | ✅ implemented |

## Open issues

- **Cold-boot GOP/DisplayPort black screen** (host-specific, e.g. GTX 980 + DP): graphical
  modeset races the monitor's DP link-training → black-but-alive menu. Pinning the resolution
  does **not** fix it; only `GRUB_TERMINAL=console` does, which a graphical bootloader can't
  use. WartBURG hits the same race on affected metal — test in VMs. See
  [KNOWN-ISSUES.md](KNOWN-ISSUES.md#1-cold-boot-black-screen-on-some-gpu--displayport-combos-gop-modeset-race).
- **Secure Boot trust path is the #1 release risk** — the MOK key must be enrolled before
  re-enabling SB on a test host (currently SB is off/Setup-Mode on the dev host; WartBURG MOK
  not yet in the MOK list).
- **Broader theme compatibility (1.0 gate, open)** — the bundled 16 render green; render-testing
  a wider real-world corpus (and any renderer gaps it surfaces) is still to do. See
  [ROADMAP.md](ROADMAP.md).
- **Live-boot on packaged distros (1.0 gate, open)** — verified in QEMU/OVMF; per-distro
  packaging + on-metal boot proof pending the packaging workstreams.
- **Live resolution switch needs a re-settable GOP** — `F3` works on virtio-vga and real
  hardware GOP, but the headless `qemu -vga std`/`bochs-display` GOPs cannot re-set their mode
  and collapse to the firmware console mode; the test/run harness uses `-device virtio-vga`.
- **Animation/transition engine** — animation paths are `TODO` stubs; the bundled BURG themes
  use no animation directives, so this is unexercised. Part of the planned native WartBURG
  identity (first-party theme + transitions).
- **Single graphical-menu hook (by design)** — GRUB exposes exactly one
  `grub_gfxmenu_try_hook`. WartBURG claims it reversibly (save/restore, no core patch), so it
  cannot run *concurrently* with another graphical-menu module; `rmmod wartburg` hands the hook
  back. Reusing stock `gfxmenu` cross-module would require patching core, which would break
  reversibility — hence WartBURG renders BURG themes itself and only *delegates* `theme.txt`.
- **Platform matrix** — arm64/RISC-V build-proven only in principle (no local cross toolchain
  here); live BIOS boot on metal blocked by host `gcc-16` GRUB tooling. Beyond x86_64-efi +
  i386-pc, untested.
