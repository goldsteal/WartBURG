# WartBURG

**A GRUB 2 module that lets you seamlessly switch between modern GRUB 2 themes and classic [BURG] themes — one menu, either format, your choice.**

GRUB 2 has its own theme format (`theme.txt`); the long-abandoned [BURG] bootloader had a
different one. WartBURG is a single GRUB module (`wartburg.mod`), built on a clean checkout of
upstream **GNU GRUB** (current `master`, just after the 2.14 release — it carries the
in-development version string 2.15), that supports both. It looks at GRUB's `theme` variable
and routes by format:

- a **GRUB 2 `theme.txt`** is handed straight to GRUB's own `gfxmenu` — rendered exactly as
  stock GRUB would;
- a **BURG theme** is rendered by WartBURG itself, via a verbatim port of BURG's theme parser
  onto a fresh renderer that draws through modern GRUB's APIs.

So you can run a modern GRUB 2 theme or an unmodified original BURG theme, and **switch between
them just by changing `theme`** — no recompiling, no second bootloader. WartBURG plugs into
GRUB's graphical-menu hook and is **fully removable**: `rmmod wartburg` restores the previous
menu, and it **patches no GRUB core file**.

> **Status:** all 16 bundled BURG themes render in-theme (navigation, scrolling, per-OS icons,
> fonts, submenus, boot), and GRUB 2 themes fall through to `gfxmenu` unchanged. Verified
> headlessly on x86_64-efi (QEMU/OVMF) and build-proven on i386-pc (BIOS) with zero source
> changes. See [Project status](#project-status).

[BURG]: https://launchpad.net/burg

---

```
╔══════════════════════════════════════════════════════════════════════════════╗
║      ▓▒░  S Y S T E M   N O T I C E  ░▒▓   //   100% LLM-WRITTEN   ▓▒░       ║
╠══════════════════════════════════════════════════════════════════════════════╣
║                                                                              ║
║    __      __                   __    ____     __  __  ____    ____          ║
║   /\ \  __/\ \                 /\ \__/\  _`\  /\ \/\ \/\  _`\ /\  _`\        ║
║   \ \ \/\ \ \ \     __     _ __\ \ ,_\ \ \L\ \\ \ \ \ \ \ \L\ \ \ \L\_\      ║
║    \ \ \ \ \ \ \  /'__`\  /\`'__\ \ \/\ \  _ <'\ \ \ \ \ \ ,  /\ \ \L_L      ║
║     \ \ \_/ \_\ \/\ \L\.\_\ \ \/ \ \ \_\ \ \L\ \\ \ \_\ \ \ \\ \\ \ \/, \    ║
║      \ `\___x___/\ \__/.\_\\ \_\  \ \__\\ \____/ \ \_____\ \_\ \_\ \____/    ║
║       '\/__//__/  \/__/\/_/ \/_/   \/__/ \/___/   \/_____/\/_/\/ /\/___/     ║
║                                                                              ║
║ >> Every line of this code was written by large language models.             ║
║ >> No human has ever performed a thorough code review of it.                 ║
║ >> The neural nets were confident. The neural nets are always confident.     ║
║                                                                              ║
║ ┌──────────────────────────────────────────────────────────────────────────┐ ║
║ │     ⚠  DO NOT RUN ON CRITICAL INFRASTRUCTURE WITHOUT HUMAN REVIEW  ⚠     │ ║
║ └──────────────────────────────────────────────────────────────────────────┘ ║
║                                                                              ║
║ This is unaudited, autonomously-generated bootloader code, and it            ║
║ boots your machine. Read it. Test it. Trust it only after a human            ║
║ you trust has actually looked.   // jack out before you regret it //         ║
║                                                                              ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

---

## Why

BURG was a fork of GRUB (circa 2009) that added its own theme engine. It never tracked
upstream, stopped around GRUB ~1.99, and bit-rotted — so its themes no longer run on a current
GRUB. WartBURG ports BURG's theme engine onto current GRUB as a removable module, rather than
reviving the dead fork: no GRUB core changes, and `rmmod` undoes it.

## Features

- **Switch between GRUB 2 and BURG themes seamlessly.** WartBURG renders BURG themes itself and
  transparently delegates GRUB 2 `theme.txt` themes to stock `gfxmenu`. Pick either by setting
  `theme` — no recompiling, no second bootloader.
- **Runs unmodified BURG themes.** A verbatim port of bean's BURG `uitree` theme parser plus a
  fresh, gfx-only renderer (regions, widget layout, component classes). 16/16 bundled themes
  render in their intended look.
- **The BURG signature menu.** Horizontal (or vertical) icon menus, per-OS icons selected by
  menu-entry `--class` (`arch`, `ubuntu`, `windows`, `fedora`, …), grey→colour/large on
  selection, nine-slice panels, progress bars, and animated `circular_progress` countdown rings.
- **Real navigation.** Arrow keys **and** vim `hjkl`, direction-aware, plus the theme's own
  `mapkey`/`onkey` bindings. Selection scrolls into view when a menu overflows the screen.
- **Automatic theme fonts.** Named fonts (`"Helvetica Bold 27"`, `"lime Regular 10"`, …)
  auto-load from the theme's `font.lst`, so titles render in their real typefaces.
- **Submenus & dialogs.** Themed nested menus (drill in / `ESC` out), message popups, and a
  password widget.
- **Reuses GRUB where GRUB is better.** The `e` key opens GRUB 2's own entry editor and `c`
  opens its command line — full history, completion, and authentication — rather than
  reimplementing them.
- **Fully reversible.** WartBURG saves and restores GRUB's graphical-menu hook: `rmmod
  wartburg` returns you to whatever menu was active before. **No GRUB core file is patched.**
- **Per-entry security preserved.** Restricted (`--users`) entries are authenticated through
  GRUB's own mechanism before booting — no Secure Boot bypass.

## How it works

WartBURG claims GRUB's single graphical-menu hook, `grub_gfxmenu_try_hook`, in `GRUB_MOD_INIT`
(saving the previous value) and restores it in `GRUB_MOD_FINI`. When GRUB displays the menu,
the hook receives the live `grub_menu_t`; WartBURG then:

1. **Dispatches by theme format** — a GRUB 2 `theme.txt` (it declares a `boot_menu`) is handed
   to the saved `gfxmenu` hook; a BURG theme (it declares a `screen`) is rendered by WartBURG.
2. **Parses** the BURG theme into a node tree (ported `uitree` parser).
3. **Builds the widget tree**, populating the menu from the real GRUB entries (icon by
   `--class`, title, command, submenu flag), and lays it out.
4. **Renders** through exported GRUB primitives only — `grub_video_*`, the glyph font API,
   `grub_video_bitmap_*` for scaling/compositing.
5. **Runs its own input loop** (a faithful port of BURG's dispatch, adapted to current GRUB) for
   navigation, submenus, timeout/countdown, and boot via `grub_script_execute_sourcecode`.

The renderer is **platform-agnostic** — pure `grub_video`/`grub_font`, no platform `#ifdef`s —
which is why it builds unchanged for both UEFI and BIOS targets.

## Building

WartBURG lives inside this GRUB tree as the `wartburg` module
(`grub-core/commands/wartburg*.c`, registered in `grub-core/Makefile.core.def`). Build GRUB
as usual and `wartburg.mod` is produced alongside the other modules:

```sh
./bootstrap                                            # once, after a fresh clone
./configure --with-platform=efi --target=x86_64        # UEFI / x86_64
make -j"$(nproc)"
# -> grub-core/wartburg.mod
```

For BIOS instead, configure with `--with-platform=pc --target=i386`.

> **Building on Bedrock Linux:** run the build under a single, self-consistent stratum
> (e.g. `strat arch make …`); mixing strata breaks the GRUB/binutils toolchain.

`wartburg.mod` depends on `bitmap`, `bitmap_scale`, `trig`, `font`, `video`, and `normal`
(resolved automatically via `moddep.lst`).

### Other architectures

WartBURG's renderer makes no platform assumptions (pure `grub_video`/`grub_font`, no
platform `#ifdef`s), so it builds wherever GRUB does. It is developed and run on
**x86_64-efi** and builds unchanged for **i386-pc (BIOS)** — exercising both the UEFI/GOP and
the BIOS/VBE video paths from the same source. GRUB itself cross-compiles to arm64, RISC-V,
PowerPC, SPARC64, MIPS and more, so WartBURG follows with a suitable cross toolchain; only
GRUB's build environment is involved, never WartBURG's code.

For wide cross-compilation the **[T2 SDE]** project (René Rebe, *rxrbln*) is the reference: it
cross-builds GRUB for around a dozen CPU architectures. Its grub2 patch set smooths modern
toolchains — notably `hotfix-ld-image-base` (use `-Wl,-Ttext`, for newer binutils) and
`hotfix-mkiamgexx` (tolerate a section/link address mismatch during cross image generation).
T2 packages the **2.14 release**, so basing a cross build on 2.14 lets those patches apply
directly.

[T2 SDE]: https://t2sde.org/

## Trying it

Put `wartburg.mod` and its dependencies on a GRUB boot image, drop a BURG theme under
`/boot/burg/themes/<name>/` (with its `fonts/` directory and `font.lst`), and use a config
like:

```
loadfont /boot/grub/fonts/unicode.pf2
insmod all_video
insmod gfxterm
terminal_output gfxterm
insmod gfxmenu        # optional: enables GRUB 2 theme.txt fallback
insmod wartburg
set theme=/boot/burg/themes/radiance/theme
menuentry "Arch Linux" --class arch { … }
menuentry "Ubuntu"     --class ubuntu  { … }
submenu  "Advanced"    --class fedora  { … }
```

Navigate with the arrow keys or `hjkl`, `Enter` to boot, `e` to edit, `c` for the console.
`rmmod wartburg` cleanly restores the previous menu.

(The development tree also carries a headless QEMU/OVMF harness used to render-test and
screendump every bundled theme; the renderer is validated this way on each change.)

## Repository layout

WartBURG is contained in a handful of files added to the GRUB tree:

| File | Role |
|------|------|
| `grub-core/commands/wartburg.c` | Module init/fini; the reversible hook; BURG↔GRUB2 dispatcher |
| `grub-core/commands/wartburg_theme.c` | Ported BURG `uitree` theme parser |
| `grub-core/commands/wartburg_region.c` | gfx draw backend, bitmap scaling/cache, font auto-load |
| `grub-core/commands/wartburg_widget.c` | Widget layout/draw engine + scrolling |
| `grub-core/commands/wartburg_ui.c` | Component classes: screen, panel, image, text, progressbar, circular_progress, password, edit |
| `grub-core/commands/wartburg_menu.c` | Template/parameter machinery + dialog runner |
| `grub-core/commands/wartburg_input.c` | Input dispatch: navigation, submenus, timeout, boot |
| `include/grub/wartburg_*.h` | Internal interfaces |

Everything else in this repository is upstream GNU GRUB.

## Project status

| Area | State |
|------|-------|
| BURG theme rendering | ✅ 16/16 bundled themes render in-theme |
| Navigation (arrows + vim, scroll, mapkey) | ✅ |
| Per-OS icons, fonts (`font.lst`), progress, circular_progress | ✅ |
| Submenus, message dialogs, password widget | ✅ |
| `e` editor / `c` console (via GRUB) | ✅ |
| BURG ⇄ GRUB 2 theme coexistence | ✅ |
| Reversible hook, no core patch | ✅ |
| x86_64-efi | ✅ verified (QEMU/OVMF, headless) |
| i386-pc (BIOS) | ✅ builds with zero source changes |

### Roadmap

- **Rendering fidelity & hardening** — true tiling/centre scale modes, safe arithmetic on
  theme-supplied sizes, graceful handling of unknown widget classes.
- **OS auto-detection icons** — wire `+class/-os` icon rules to real `os-prober`/`grub-mkconfig`
  output so installed systems get the right icon automatically.
- **More platforms** — extend the build/test matrix beyond x86_64-efi and i386-pc.
- **Runtime UX** — live theme switching and resolution switching (the `F2`/`F3` actions themes
  already advertise).
- **A native WartBURG identity** — a first-party theme and an animation/transition engine.

## Compatibility notes

- Developed against **upstream GNU GRUB `master`** as of `grub-2.14-37-g…` — i.e. just after
  the 2.14 release (the tree's own version string is the unreleased 2.15; 2.14 is the latest
  tagged GRUB release). WartBURG adapts to API changes since BURG's era (the handler framework,
  terminal/key constants, font and bitmap helpers, etc.).
- It renders the BURG theme **format**; you do not need to recompile themes. Themes that use
  components not yet ported will render everything else.
- Restricted-entry authentication uses GRUB's standard prompt (a fully themed credential dialog
  is constrained by what the current GRUB auth API exposes).

## Credits & license

WartBURG stands entirely on other people's work, with thanks:

- **Bean Lee** (alias *bean* / *bean123ch*) — author of the original [BURG] and its theme
  engine. WartBURG is a port of his parser, layout, and component model
  (© 2009 Bean Lee, GPLv3+).
- **Nate Muench** (alias *n-muench*) — maintained the long-running Ubuntu PPA
  (`ppa:n-muench/burg`) that kept BURG installable and usable for years after upstream went
  quiet.
- The keepers who have repackaged, mirrored, and forked BURG since — including the theme
  collections and the x86_64-EFI fork (GitHub *elokrypt*'s `burg-themes`, *yymirror*'s
  `burg-x86_64-efi`, among others) — which is why original BURG themes and a buildable tree are
  still available today.
- **René Rebe** (alias *rxrbln*) and the **[T2 SDE]** project — whose extensively
  cross-compiled GRUB packaging and patch set are the reference for building GRUB (and so
  WartBURG) across architectures.
- The **GNU GRUB** maintainers, on whose code, modules, and APIs all of this is built.

*Credited by public handle where a real name isn't verifiable — corrections welcome.*

WartBURG is distributed under the **GNU GPL v3 or later**, consistent with both BURG and GRUB.
It is an independent project and is **not affiliated with or endorsed by** the GNU GRUB
project, the original BURG author, or any operating-system vendor whose logo may appear in a
theme.

---

*Released on Wartburgfest, 18 October 2026 — in honour of the [Wartburgfest] of 18 October 1817.*

[Wartburgfest]: https://en.wikipedia.org/wiki/Wartburg_Festival
