# WartBURG Roadmap

_See [README](../../README.md) for the project overview, [STATE.md](STATE.md) for the
detailed current state, [INSTALL.md](INSTALL.md) to install, and
[KNOWN-ISSUES.md](KNOWN-ISSUES.md) for caveats._

A guiding constraint applies to **everything** below: WartBURG stays a single, **removable**
module. `rmmod wartburg` must fully restore the previous menu, and **no GRUB core file is
patched**. Any new capability is added as WartBURG's own module(s) or as host-side
configuration — never by editing upstream GRUB sources.

---

## v1.0.0 — feature-complete bar

The 1.0 quality bar (GA targeted for Wartburg day, 18 Oct 2026; everything before that is
pre-release / personal use). Items are capabilities, not dates.

| Item | State |
|------|-------|
| Rendering fidelity & hardening — graceful skip of unknown widget classes (no boot-blank on a bad theme), overflow-safe size arithmetic, true tiling/centre image scale | ✅ done |
| Runtime UX — live theme switch (`F2`) + resolution switch (`F3`), reload-in-place, persisted to grubenv | ✅ done |
| OS auto-detection icons — `+class/-os` rules wired to `os-prober`/`grub-mkconfig` output | ✅ done |
| Broader BURG theme compatibility — render-test beyond the bundled 16 (real-world theme corpus); keep the sweep green | ◻ in progress (bundled 16/16 green) |
| Live-boot proven on every packaged distro group (x86_64-efi minimum) | ◻ pending packaging |

> Note: live resolution switching needs a GOP that can re-set its mode at runtime
> (virtio-vga, and real-hardware GOP) — the headless `qemu -vga std`/`bochs-display` GOPs
> cannot and will not preview it. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).

The default install ships a curated set of **existing** BURG reference themes (so a bare
install is never blank); a first-party WartBURG theme is a 1.1 goal (below).

---

## Post-1.0 (1.1 and onward) — none of these block GA

Features inspired by other boot managers (rEFInd, systemd-boot, Clover, openSUSE-snapper),
built as WartBURG modules. WartBURG already owns the menu hook, icon renderer, input loop and
dialogs, so most of these are new **entry sources**, **input modes**, or **boot actions** that
slot into the existing engine. Ordered so each milestone builds on the previous.

Alongside these, the headline 1.1 work is a **first-party WartBURG theme + an
animation/transition engine** for a native identity.

### M1.1 — Expand what can be booted (be a boot *manager*, not a GRUB skin)

- **Zero-config EFI OS discovery (rEFInd-style).** ✅ *Shipped (increments 1–2)* — the
  `wartburg_discover` command does a full directory scan of each FAT/ESP `\EFI` tree
  (descending into `Microsoft\Boot` for Windows) and synthesizes `chainloader` entries
  auto-classed for the icon engine; run it from grub.cfg before the menu. rEFInd-style
  heuristics: one primary loader per vendor dir preferring **shim** (the Secure Boot entry that
  chainloads grub) over grub; a non-loader denylist (MokManager/`mm*`/`fb*`/shell/memtest/
  drivers); fallback `\EFI\BOOT` hidden when a real loader exists; titles from a vendor map →
  volume label → dir name; dedup against hand-written menuentries; `$wartburg_discover_skip`
  exclusions. Increment 3 adds **`\EFI\Linux` UKIs** (each unified kernel image is its own
  directly-chainloadable entry) and **`$wartburg_discover_dirs`** (also_scan_dirs-style extra
  loader directories). *Foundational:* establishes the ESP-scan + entry-synthesis plumbing the
  next two reuse. Increment 4 reads **UKI titles from the embedded `.osrel`** (PRETTY_NAME) so
  `\EFI\Linux` images show their real OS name. Increment 5 adds **volume-root loader scanning**
  and **Secure Boot state detection** (`$wartburg_secureboot` = on/off/unknown) for the UI/config
  to react to. Zero-config discovery is now feature-complete for v1; refinements (macOS/HFS+,
  Linux direct-kernel stanzas) are open if demand appears.
- **Native BLS / UKI entries.** ✅ *Shipped* — **reuses** GRUB 2.15's stock `blsuki` module
  (the `blscfg` command parses Boot Loader Spec Type&nbsp;#1 `/loader/entries/*.conf`; `uki`
  imports Type&nbsp;#2 Unified Kernel Images) rather than reimplementing a parser. WartBURG's
  contribution is the rendering glue: a **title-derived icon class** for the class-less entries
  those commands emit (`blscfg` only classes from a `grub_class` key; `uki` sets none), so they
  show the right OS logo. (Earlier note "not present in upstream GRUB" was wrong — it landed
  post-2.14.)
- **Btrfs / ZFS snapshot boot (graphical).** ✅ *Shipped (render side).* **Reuse, don't
  reinvent:** snapshot *enumeration* is delegated to host-side tooling —
  [grub-btrfs](https://github.com/Antynea/grub-btrfs) (shell + a daemon regenerating the GRUB
  config; *not* a GRUB fork) or openSUSE's snapper grub2 plugin — which emit a normal submenu of
  snapshot menuentries (tagged `--class snapshots --class gnu-linux …`). WartBURG renders that
  submenu via its existing drill-down; the contribution is a **`snapshots` restore icon** (new
  class in `wartburg-icons`, registered via `register-icons.sh`) plus a title fallback so the
  class-less submenu wrapper shows it too. Upstream GRUB has no btrfs subvolume-listing command
  to reuse, and we won't patch `grub-core/fs/btrfs.c`, so an in-module "zero-config" enumerator
  stays out — host tooling owns enumeration, preserving removability.

> **Shipping note — GRUB-version dependence.** WartBURG is rebuilt against each distro's exact
> GRUB (no module ABI), so a feature only works if that GRUB has the modules it leans on. The
> `blsuki` module (`blscfg`/`uki`) is **new in ~2.15**; older upstream GRUB (Debian 2.06/2.12,
> Arch/T2 2.14) lacks it, though Fedora has carried a downstream `blscfg` for years. The split we
> rely on: **WartBURG's own `wartburg_discover` is fully portable** — it uses only long-standing
> APIs (`grub_device_iterate`/`fs_dir`/`file_open`, `pe32.h`), so vendor + UKI + volume-root
> discovery (incl. `.osrel` titles) works on every target regardless of GRUB version. Stock
> `blscfg`/`uki` are treated as *optional enhancements*: where present they add BLS Type#1 import
> (which discovery does not do) and an alternate UKI path; where absent, `blscfg` in grub.cfg just
> errors harmlessly and discovery still covers loaders/UKIs. WartBURG never build- or link-depends
> on `blsuki`. Net: discovery is the baseline everywhere; the dedup hardening above is what makes
> the two coexist on the newer/Fedora GRUBs where both exist.

### M1.2 — Make the (now larger) menu navigable

- **Type-to-search / filter.** ✅ *Shipped (type-ahead jump).* `/` opens incremental search;
  each keystroke jumps the selection to the first title containing the query (case-insensitive)
  and scrolls it in, with a "Search: <q>_" overlay (drawn into both double-buffer passes via a
  new `grub_wb_overlay_hook`). vim hjkl nav preserved. *Possible follow-on:* true filtering (hide
  non-matches + reflow) — deferred because it means teaching the layout engine to skip HIDDEN
  and a horizontal-menu reflow on every keystroke; jump is cleaner for the icon row.
- **Mouse + touch navigation.** ✅ *Implemented* — new `mouse` term module (after a1ive's
  approach) reads the EFI Absolute Pointer (touch/tablet/qemu usb-tablet) and Simple Pointer
  (mice) and emits menu keys (move→arrows, click/tap→Enter, right-click→Esc), so any menu
  becomes pointer-navigable via `terminal_input --append mouse` with no menu-code change.
  Verified under OVMF that it loads + locates both pointer protocols; interactive click/drag
  nav is pending live-hardware confirmation (headless QEMU can't feed OVMF's EFI pointer).
  *Possible follow-on:* absolute hit-testing (click the exact item under the cursor) + a cursor
  sprite, instead of mapping motion to arrow steps.

### M1.3 — Boot lifecycle: flexibility + reliability

- **Reboot-to-firmware + one-shot boot.** Surface a first-class "UEFI Setup" action (via EFI
  `OsIndications`), and a systemd-boot-style boot-once (pick a next-boot-only entry, persisted
  in grubenv, auto-reverting).
- **Boot counting & auto-rollback.** A tries-counter in grubenv that falls back to the
  last-known-good entry after repeated failed boots — pairing with snapshot rollback (M1.1) for
  a complete reliability story. The most involved item (needs a boot-assessment state machine and
  a userspace "boot succeeded" confirmation), so it comes last.

---

### M1.4 — Theming reach & installer polish

Lessons from the wider GRUB-theming ecosystem (e.g. the popular
[vinceliuice/grub2-themes](https://github.com/vinceliuice/grub2-themes), which is theme
*content* + an installer for stock `gfxmenu`, complementary to WartBURG-the-engine):

- **First-class GRUB 2 `theme.txt` compatibility.** Popular GRUB 2 theme packs already render
  *through* WartBURG via its coexistence dispatcher (they fall through to `gfxmenu`). Verified
  2026-06-21 against [vinceliuice/grub2-themes](https://github.com/vinceliuice/grub2-themes)
  "Tela": it renders pixel-identically with or without WartBURG loaded — delegation is
  transparent. One module renders both modern GRUB 2 theme packs and classic BURG themes; more
  well-known packs to be added to the sweep.
- **Installer / `wartburg` CLI UX.** Match the conveniences users expect from theme installers:
  theme + icon-style + resolution flags, a custom-resolution option, an interactive picker when
  run bare, a generate/dry-run mode (emit without installing), clean removal, and `/boot/grub`
  vs `/boot/grub2` auto-detection.
- **Resolution-variant assets + custom backgrounds.** Per-resolution asset selection and an
  easy custom-background swap (pairs with the runtime resolution switch and the new center/tiling
  image scaling).
- **Icon-style variants.** Add a style axis (e.g. color/white) to the icon pipeline alongside
  the existing per-state sets (grey/hover/small/large).

## Platform & packaging reach (ongoing)

WartBURG is verified on **x86_64-efi** and builds unchanged on **i386-pc** (BIOS). Extending
the build/boot test matrix to **arm64-efi** and others is ongoing; the per-distro packaging
work rebuilds the module against each distro's exact GRUB, with a signed module + MOK-enrollment
path for Secure Boot. Target package channels: Debian/Ubuntu/Mint (apt), Fedora (dnf), Arch
(AUR), T2 SDE, and a **NixOS flake module** (a clean self-contained path used to good effect by
other GRUB-theming projects).
