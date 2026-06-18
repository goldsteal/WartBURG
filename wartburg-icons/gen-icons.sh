#!/usr/bin/env bash
# gen-icons.sh — fetch distro logos from Wikipedia (pageimages API) and emit the
# three WartBURG/BURG icon variants (large=128 colour, small=24 colour,
# grey=128 desaturated), all forced to RGBA/PNG32 (GRUB's PNG loader is happiest
# with truecolour+alpha; indexed/greyscale PNGs render as garbage or not at all).
#
# Usage: ./gen-icons.sh [class ...]   (no args = the full NEW set below)
# Output goes straight into burg-ref/themes/themes/icons/.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
# Output dir for the generated PNGs. Defaults to the local dev BURG icon set;
# override (e.g. WB_ICONS_DIR=./icons) when regenerating the committed pack.
ICONS="${WB_ICONS_DIR:-$HERE/burg-ref/themes/themes/icons}"
UA="WartBURG-icon-fetch/1.0 (https://github.com/goldsteal/WartBURG)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# class            Wikipedia article title (URL-encoded spaces as _)
declare -A TITLE=(
  [kali]="Kali_Linux"            [popos]="Pop!_OS"          [elementary]="Elementary_OS"
  [mx]="MX_Linux"                [zorin]="Zorin_OS"         [deepin]="Deepin"
  [devuan]="Devuan"              [parrot]="Parrot_OS"       [neon]="KDE_neon"
  [pureos]="PureOS"              [antix]="AntiX"            [tails]="Tails_(operating_system)"
  [kubuntu]="Kubuntu"            [xubuntu]="Xubuntu"        [lubuntu]="Lubuntu"
  [raspbian]="Raspberry_Pi_OS"   [lmde]="Linux_Mint_Debian_Edition"
  [sparky]="SparkyLinux"         [q4os]="Q4OS"              [peppermint]="Peppermint_OS"
  [bodhi]="Bodhi_Linux"          [manjaro]="Manjaro"      [endeavouros]="EndeavourOS"
  [steamos1]="SteamOS"           [steamos2]="SteamOS"       [steamos3]="SteamOS"
)

# Direct Commons-filename overrides for distros whose Wikidata entity has no
# P154 "logo image" (or where P154 is a poor wordmark). Skips the QID lookup.
declare -A OVERRIDE=(
  [antix]="AntiX logo.png"
  [manjaro]="Manjaro-logo.svg"      # mark-only; P154 logo carries the wordmark
  [steamos1]="Steam icon logo.svg"  # SteamOS has no text-free mark; use the Steam
  [steamos2]="Steam icon logo.svg"  # gear (also dark, so the grey variant stays
  [steamos3]="Steam icon logo.svg"  # visible -- the white wordmark desaturated away)
  # mark-only swaps for thin-wordmark P154 logos:
  [kubuntu]="Kubuntu logo full rounded.svg"   # K-gear roundel
  [xubuntu]="Xubuntu Icon.svg"                # mouse roundel
  [elementary]="Elementary logo.svg"          # the "e" mark
  [lmde]="Linux Mint Debian Edition.svg"      # Mint leaf roundel
  [q4os]="Q4OS Icon.png"                      # Q4OS disc
  [devuan]="Devuan-emblem.svg"                # the deity swirl mark
  [peppermint]="Peppermint 3.svg"             # candy swirl (cropped from wordmark)
)
# Direct image-URL overrides (used verbatim; bypass Commons/Wikidata entirely).
declare -A URL=(
  [kali]="https://www.kali.org/images/kali-dragon-icon.svg"
  [endeavouros]="https://endeavouros.com/wp-content/uploads/2021/04/eos-icon.png"
  [tails]="https://tails.net/contribute/how/promote/logo/tails-logo-drawing.svg"
)
# Classes whose only logo has an opaque (usually white) background rectangle:
# edge-flood it to transparent after rasterising. Connected-from-corner flood,
# so interior logo colours (incl. white highlights) are preserved.
declare -A FLOOD=(
  [raspbian]=1                       # "Raspberry Pi OS Logo.png" is on solid white
)
# Crop a mark out of a wider wordmark (applied after FLOOD, before resize):
# "-gravity West -crop <geom> -trim". Value is the pre-trim crop window.
declare -A CROP=(
  [raspbian]="235x280+0+0"           # keep the raspberry, drop "Raspberry Pi OS"
  [peppermint]="470x520+0+0"         # keep the candy swirl, drop "peppermint"
)

CLASSES=("$@")
[ ${#CLASSES[@]} -eq 0 ] && CLASSES=("${!TITLE[@]}")

printf '%-12s %-9s %s\n' CLASS RESULT "SOURCE / NOTE"
for c in "${CLASSES[@]}"; do
  t="${TITLE[$c]:-}"
  [ -z "$t" ] && { printf '%-12s %-9s\n' "$c" "NO-TITLE"; continue; }
  # Prefer a direct URL override; then a Commons-filename override; else
  # article title -> Wikidata QID -> P154 "logo image" -> Commons filename.
  if [ -n "${URL[$c]:-}" ]; then
    src="${URL[$c]}"; fn="${src##*/}"
    raw="$TMP/$c.src"
    curl -sL -A "$UA" "$src" -o "$raw" || { printf '%-12s %-9s\n' "$c" "DL-FAIL"; continue; }
    png="$TMP/$c.png"
    if file "$raw" | grep -qi svg; then
      rsvg-convert -h 512 "$raw" -o "$png" 2>/dev/null || cp "$raw" "$png"
    else cp "$raw" "$png"; fi
    dim=$(magick identify -format '%wx%h' "$png" 2>/dev/null)
    magick "$png" -background none -resize 120x120 -gravity center -extent 128x128 -define png:color-type=6 "PNG32:$ICONS/large_$c.png"
    magick "$png" -background none -resize 22x22 -gravity center -extent 24x24 -define png:color-type=6 "PNG32:$ICONS/small_$c.png"
    magick "$ICONS/large_$c.png" -channel RGB -modulate 100,0 +channel -define png:color-type=6 "PNG32:$ICONS/grey_$c.png"
    optipng -quiet -o2 "$ICONS/large_$c.png" "$ICONS/small_$c.png" "$ICONS/grey_$c.png" 2>/dev/null
  # GRUB's PNG decoder (a) errors on greyscale/grey+alpha colour types -- only
  # RGB/RGBA/palette are accepted -- and (b) over-reads at the IDAT boundary,
  # erroring if any chunk (tEXt/bKGD/cHRM/...) sits between IDAT and IEND. So
  # force RGBA via the PNG32: writer (desaturated greys would otherwise be
  # re-encoded as grey+alpha) and strip to IHDR/IDAT/IEND.
  for v in large small grey; do
    magick "$ICONS/${v}_$c.png" -strip -define png:exclude-chunks=all "PNG32:$ICONS/${v}_$c.png" 2>/dev/null
  done
    printf '%-12s %-9s %s\n' "$c" "OK" "$fn (url override, $dim)"
    continue
  fi
  fn="${OVERRIDE[$c]:-NONE}"
  if [ "$fn" = NONE ]; then
    qid=$(curl -sL -A "$UA" \
      "https://en.wikipedia.org/w/api.php?action=query&format=json&prop=pageprops&ppprop=wikibase_item&redirects=1&titles=$t" \
      | jq -r '.query.pages[].pageprops.wikibase_item // "NONE"')
    if [ "$qid" != NONE ] && [ -n "$qid" ]; then
      fn=$(curl -sL -A "$UA" \
        "https://www.wikidata.org/w/api.php?action=wbgetclaims&format=json&property=P154&entity=$qid" \
        | jq -r '.claims.P154[0].mainsnak.datavalue.value // "NONE"')
    fi
  fi
  if [ "$fn" = NONE ] || [ -z "$fn" ]; then
    printf '%-12s %-9s %s\n' "$c" "NO-LOGO" "$t (qid=$qid)"; continue
  fi
  src="https://commons.wikimedia.org/wiki/Special:FilePath/$(jq -rn --arg f "$fn" '$f|@uri')"
  raw="$TMP/$c.src"
  curl -sL -A "$UA" "$src" -o "$raw" || { printf '%-12s %-9s\n' "$c" "DL-FAIL"; continue; }
  # Rasterise SVG to a generous size first so the 128 downscale is crisp.
  png="$TMP/$c.png"
  if file "$raw" | grep -qi svg; then
    rsvg-convert -h 512 "$raw" -o "$png" 2>/dev/null || cp "$raw" "$png"
  else
    cp "$raw" "$png"
  fi
  dim=$(magick identify -format '%wx%h' "$png" 2>/dev/null)
  if [ -n "${FLOOD[$c]:-}" ]; then
    magick "$png" -alpha set -bordercolor white -border 1 -fuzz 12% \
           -fill none -draw 'alpha 0,0 floodfill' -shave 1x1 "$png"
  fi
  if [ -n "${CROP[$c]:-}" ]; then
    magick "$png" -gravity West -crop "${CROP[$c]}" +repage -trim +repage "$png"
  fi
  # large: fit in 120, centre on transparent 128 canvas, force PNG32 (RGBA).
  magick "$png" -background none -resize 120x120 -gravity center -extent 128x128 \
         -define png:color-type=6 "PNG32:$ICONS/large_$c.png"
  magick "$png" -background none -resize 22x22  -gravity center -extent 24x24 \
         -define png:color-type=6 "PNG32:$ICONS/small_$c.png"
  # grey: desaturate the colour large, keep RGBA.
  magick "$ICONS/large_$c.png" -channel RGB -modulate 100,0 +channel \
         -define png:color-type=6 "PNG32:$ICONS/grey_$c.png"
  optipng -quiet -o2 "$ICONS/large_$c.png" "$ICONS/small_$c.png" "$ICONS/grey_$c.png" 2>/dev/null
  # GRUB's PNG decoder (a) errors on greyscale/grey+alpha colour types -- only
  # RGB/RGBA/palette are accepted -- and (b) over-reads at the IDAT boundary,
  # erroring if any chunk (tEXt/bKGD/cHRM/...) sits between IDAT and IEND. So
  # force RGBA via the PNG32: writer (desaturated greys would otherwise be
  # re-encoded as grey+alpha) and strip to IHDR/IDAT/IEND.
  for v in large small grey; do
    magick "$ICONS/${v}_$c.png" -strip -define png:exclude-chunks=all "PNG32:$ICONS/${v}_$c.png" 2>/dev/null
  done
  # Flag wide/tall wordmarks (aspect far from square) for manual review.
  flag=""; case "$dim" in *x*) w=${dim%x*}; h=${dim#*x};
    [ "$h" -gt 0 ] && r=$((w*100/h)) || r=100
    { [ "$r" -gt 170 ] || [ "$r" -lt 59 ]; } && flag="  <-- WORDMARK? ${dim}";; esac
  printf '%-12s %-9s %s\n' "$c" "OK" "$fn$flag"
done
