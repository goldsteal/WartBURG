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

- **Zero-config EFI OS discovery (rEFInd-style).** Enumerate disks, walk each EFI System
  Partition for `\EFI\*\*.efi` plus Windows/macOS loaders, synthesize `chainloader` entries,
  and auto-pick icons via the existing OS-detection engine — reducing reliance on a hand-built
  `grub.cfg`/`os-prober`. *Foundational:* establishes the ESP-scan + entry-synthesis plumbing
  the next two reuse.
- **Native BLS / UKI entries.** Read Boot Loader Specification Type&nbsp;#1 entries
  (`/loader/entries/*.conf`) and Type&nbsp;#2 Unified Kernel Images (`\EFI\Linux\*`) directly —
  future-proofing for Fedora and image-based / atomic distros. (Not present in upstream GRUB →
  net-new.)
- **Btrfs / ZFS snapshot boot (graphical).** List snapshots as a themed submenu (timestamp,
  description, icon) and boot read-only or stage a rollback. **Reuse, don't reinvent:** the
  enumeration is delegated to host-side tooling — [grub-btrfs](https://github.com/Antynea/grub-btrfs)
  (shell scripts + a daemon that regenerate the GRUB config; *not* a GRUB fork) or openSUSE's
  snapper grub2 plugin — which emit standard menu entries that WartBURG simply renders. Upstream
  GRUB can read btrfs but does **not** enumerate snapshots or expose subvolume-selection
  commands (those are downstream patches), so a fully in-module "zero-config" enumerator is a
  later, optional follow-on — and, if built, it lives in a WartBURG module rather than patching
  `grub-core/fs/btrfs.c`, to preserve removability.

### M1.2 — Make the (now larger) menu navigable

- **Type-to-search / filter.** Incremental filter-as-you-type, motivated by the entry growth
  from M1.1. Cheap — WartBURG owns the input loop.
- **Mouse + touch navigation.** The horizontal icon menu is an ideal click/tap target (rEFInd
  and Clover have pointer support; GRUB effectively does not). EFI Simple/Absolute Pointer
  protocols are available; add a pointer poll alongside the keyboard loop.

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
