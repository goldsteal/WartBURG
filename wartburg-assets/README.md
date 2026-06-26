# WartBURG icon assets

The WartBURG OS-/action-icon pack and the tooling that produces it.

## Layout

- `icons/` — the icon pack. For each class there are up to three colour
  variants (`large_<class>.png` 128×128 colour, `grey_<class>.png` 128×128
  desaturated, `small_<class>.png` 24×24 colour), all forced to truecolour+alpha
  (RGBA/PNG32) because GRUB's PNG loader renders indexed/greyscale PNGs as
  garbage. The four map files (`grey` / `hover` / `small` / `large`) are BURG's
  explicit class→file maps — BURG does *not* directory-scan, so a class only
  resolves if it is listed in the map. `*.orig` are pristine pre-WartBURG
  snapshots of the maps; `register-icons.sh` rebuilds the maps from these.

## Classes

On top of stock BURG's distro icons, WartBURG adds extra distros and the
`firmware` action class (the M1.3 "UEFI Firmware Setup" / boot-once entries get
the chip glyph rather than a distro logo). The authoritative list of additions
is the `CLASSES` array in `register-icons.sh`.

## Tooling

- `gen-icons.sh [class ...]` — fetch distro logos (Wikipedia/Wikidata) and emit
  the large/small/grey RGBA variants. `firmware` is hand-drawn, not fetched.
- `register-icons.sh <icons-dir>` — register WartBURG's extra classes into the
  four map files. Idempotent: rebuilds the additions from the `*.orig` snapshot
  each run, so re-running never duplicates entries.

Both scripts originate from the WartBURG workspace root and default to operating
on the workspace's `burg-ref/themes/themes/icons` tree. To run them against this
tracked copy instead, point them here, e.g.:

```bash
WB_ICONS_DIR=wartburg-assets/icons ./wartburg-assets/gen-icons.sh
./wartburg-assets/register-icons.sh wartburg-assets/icons
```
