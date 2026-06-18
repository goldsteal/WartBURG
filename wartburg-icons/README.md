# WartBURG OS-detection icon pack

Extra per-distro icons for WartBURG's BURG-style menu, on top of the ~20 classes
stock BURG themes ship. 29 classes (× 3 variants = 87 PNGs) live in `icons/`:

- **Arch family:** artix, garuda, endeavouros, manjaro, t2, bedrock
- **Debian/Ubuntu family:** kali, popos, elementary, mx, zorin, deepin, devuan,
  neon, pureos, antix, tails, kubuntu, xubuntu, lubuntu, raspbian, lmde, sparky,
  q4os, peppermint, bodhi
- **SteamOS:** steamos1, steamos2, steamos3

(`parrot` is intentionally absent — no freely-licensed logo on Wikimedia Commons;
a Parrot menu entry falls through `--class debian` to the Debian icon.)

Each class has three variants the BURG icon maps reference:
`large_<c>.png` (128px colour), `small_<c>.png` (24px colour), and a desaturated
`grey_<c>.png` (128px, the unselected state).

## Using the pack

Two steps — **dropping the PNGs in is not enough.** BURG's icon system is an
explicit *class → file* map (`icons/{grey,hover,small,large}`), not a directory
scan, so an unregistered class falls through the menu entry's comma `--class`
list to `linux` (the Tux fallback).

```sh
# 1. copy the PNGs into your BURG theme's icons/ dir
cp icons/*.png /path/to/burg/themes/<theme>/icons/

# 2. register the classes in that dir's grey/hover/small/large map files
./register-icons.sh /path/to/burg/themes/<theme>/icons
```

`register-icons.sh` snapshots each map file to `<map>.orig` on first run and is
idempotent (it always rebuilds the additions from the snapshot).

## Regenerating the artwork

`gen-icons.sh` fetches each logo and emits the three variants:

```sh
WB_ICONS_DIR=./icons ./gen-icons.sh            # all classes
WB_ICONS_DIR=./icons ./gen-icons.sh kali tails # specific classes
```

Sourcing notes:
- Logos come from **Wikidata's P154 "logo image"** property (the Wikipedia
  `pageimages` API returns desktop *screenshots*, not logos).
- `URL[]` / `OVERRIDE[]` / `FLOOD[]` / `CROP[]` maps at the top handle special
  cases: kali → kali.org dragon SVG; antix/tails → specific Commons files;
  raspbian → white background flood-filled to transparent + cropped to just the
  raspberry mark.

## GRUB PNG-decoder constraints (why the pipeline is fussy)

`grub-core/video/readers/png.c` will make an icon render **blank, with no error
in the menu**, unless:

1. **Colour type is RGB/RGBA/palette** — greyscale and grey+alpha (types 0/4)
   are rejected. ImageMagick re-encodes a desaturated "grey" image as grey+alpha
   unless forced through the `PNG32:` writer (`-type TrueColorAlpha` is *not*
   enough).
2. **No ancillary chunks between IDAT and IEND** — the inflater over-reads at the
   IDAT boundary and errors on a trailing `tEXt`/`bKGD`/`cHRM`/`tIME`. Strip to
   `IHDR/IDAT/IEND` (`magick … -strip -define png:exclude-chunks=all PNG32:…`).

`gen-icons.sh` already enforces both. Verify any icon with:
`magick identify -format '%[channels]' large_<c>.png` → want `srgba`.
