# WartBURG Current State

_Snapshot: 2026-06-13. See [README](../../README.md) for the project overview,
[INSTALL.md](INSTALL.md) for installation, [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for caveats._

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

`a50d232a9771b0db5d98ae6bcb5e38fdc8af05dd` — _"Add GitHub release uploader; fix pinned-version
asset URL"_ (2026-06-08), HEAD of `wartburg`.

> ⚠️ The working tree is **not clean**: `grub-core/commands/wartburg_input.c` has uncommitted
> edits, and `packaging/` + `docs/wartburg/` are untracked. The in-tree `grub-core/wartburg.mod`
> was built 2026-06-08 from an earlier state — **rebuild before trusting it**.

Recent history (newest first):

```
a50d232a9 Add GitHub release uploader; fix pinned-version asset URL
4d9596435 docs(mirror): note GIT_SSH_COMMAND override for GitHub pushes
ca5b14c4c Add Secure Boot installer + signed release pipeline
32bdbeabd docs: add WartBURG README with LLM-authored warning
e8dd0a7ed WartBURG S1.6: console (c) + entry editor (e) via stock GRUB, not a port
7c26fb131 WartBURG S1.5: port the edit component (multi-line entry editor)
21d4357af WartBURG: seamless BURG + GRUB2 theme coexistence (dispatcher)
f656b2881 WartBURG S1.4: submenu drill-down (themed nested menus)
09e2b9740 WartBURG S1.2/S1.3: dialog runner + password component
1107b5b90 WartBURG S1.1: port BURG's real input dispatch (grub_widget_input)
3b33a0a20 WartBURG: auto-load theme fonts from BURG font.lst (name->file)
72617908c WartBURG: port circular_progress component -> 16/16 themes render
```

## New files

Module sources (`grub-core/commands/`):

- `wartburg.c` — module init/fini; the reversible `grub_gfxmenu_try_hook`; BURG⇄GRUB2 dispatcher
- `wartburg_theme.c` — ported BURG `uitree` theme parser
- `wartburg_region.c` — gfx draw backend, bitmap scaling/cache, `font.lst` auto-load
- `wartburg_widget.c` — widget layout/draw engine + scrolling
- `wartburg_ui.c` — component classes: screen, panel, image, text, progressbar, circular_progress, password, edit
- `wartburg_menu.c` — template/parameter machinery + dialog runner
- `wartburg_input.c` — input dispatch: navigation, submenus, timeout, boot

Headers (`include/grub/`):

- `wartburg_region.h`, `wartburg_theme.h`, `wartburg_widget.h`

Packaging & docs (currently **untracked**):

- `packaging/install.sh` — universal side-by-side installer (build-from-source, Secure
  Boot/MOK signing, ESP backup, pinned gfxmode, auto-wired `source`/`09_wartburg`)
- `README.md` — project README (committed)
- `docs/wartburg/INSTALL.md`, `docs/wartburg/KNOWN-ISSUES.md`, `docs/wartburg/STATE.md` (this file)

## Modified files

- `grub-core/Makefile.core.def` — registers the `wartburg` module (lines ~1699–1707), listing
  all seven `common = commands/wartburg*.c` sources.

That is the **only** upstream GRUB file changed — everything else is additive. (Working-tree
also has the uncommitted `wartburg_input.c` edit and the README pointer edit noted above.)

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
| OS-prober `--class` → BURG icon-class mapping (`osx`/`darwin`→`macosx`, `gnu-linux`→`linux`, `opensuse`→`suse`; full class list passed for fallback) | ⚠️ implemented, **uncommitted** in `wartburg_input.c` |
| Submenus, message dialogs, password widget | ✅ |
| `e` editor / `c` console (delegated to stock GRUB) | ✅ |
| BURG ⇄ GRUB 2 `theme.txt` coexistence (dispatcher) | ✅ |
| Restricted-entry (`--users`) auth via GRUB (no SB bypass) | ✅ |
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
- **`packaging/` and `docs/wartburg/` are untracked**, and `wartburg_input.c` has uncommitted
  edits — commit and rebuild to refresh the in-tree `wartburg.mod`.
- **OS auto-detection icons** — the `--class` → BURG icon-class vocabulary mapping is
  implemented (uncommitted, see status table); still open: BURG's `+class/-os` icon *rules*
  and exercising it against real `os-prober`/`grub-mkconfig` output on installed systems.
- **Rendering hardening** — explicit `TODO`s in `wartburg_region.c`: `WB_SCALE_CENTER` (true
  no-scale centering, line ~404), `WB_SCALE_TILING` (real tiling — currently falls back to
  stretch, line ~410), and nine-slice bitmap→bitmap compositing for boxes (line ~440). Plus
  safe arithmetic on theme-supplied sizes and graceful handling of unknown widget classes.
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
- **Runtime UX** — live theme switching and resolution switching (`F2`/`F3`) not yet
  implemented.
