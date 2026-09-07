# BURG theme compatibility checks

Verified on 2026-09-07 with the x86_64-efi build at `dec31ed8f`, using real
QEMU/KVM boots under OVMF. No renderer changes were needed for this increment.

| Coverage | Result |
| --- | --- |
| Bundled reference themes | 16/16 static renders passed |
| External themes: Darkness, Darkness Blue, Metro, Toroo | 4/4 static renders passed |
| Live keyboard navigation: Radiance and all four external themes | 5/5 passed |

Static tests require a successful `wbrender` return, no reported renderer errors,
and a nonblank screenshot. Live tests capture the default selection, move to
Ubuntu, require changed pixels, then press Enter and require a serial marker from
that entry. The test entry prints a marker and waits; this does not establish that
an operating system boots. Screenshots of the added themes were also inspected.

## Reproducing the checks

The local workspace harness lives one directory above this GRUB checkout. From
that workspace root, with GRUB built and the reference assets available:

```sh
python3 tests/fetch-theme-corpus.py
./sweep.sh --corpus
./sweep.sh --corpus --navigation radiance darkness darkness-blue metro toroo
```

The fetcher pins commits and SHA-256 hashes in `tests/theme-corpus.json`. The sweep
then uses verified cached archives without network access, preserving each
source's theme files and isolating shared assets per source. Downloaded assets
remain in an ignored local cache. Each run retains serial logs, captures, a disk
image, and TSV/JSON results in a separate `sweep-out/run-*/` directory. A failed
assertion or boot/render error returns a nonzero status.

The 2026-09-07 render report is `sweep-out/run-hJ9Jw8/results.json` (20/20);
the live-input report is `sweep-out/run-fmvm20/results.json` (10/10: five static
checks plus five navigation checks). These are local artifacts, not release files.

## Pinned external sources

| Themes | Repository revision |
| --- | --- |
| Darkness, Darkness Blue | [gustawho/darkness-burg-theme, 05e0305a](https://github.com/gustawho/darkness-burg-theme/tree/05e0305aa063fd771c63d870317efb2e6eaabb89) |
| Metro | [ilhamarrouf/Metro-Burg-Themes, 8751be04](https://github.com/ilhamarrouf/Metro-Burg-Themes/tree/8751be048b40f3b53ba02d4ea79d9b7c6826dbf0) |
| Toroo | [anak10thn/toroo-burg-themes, bdb22130](https://github.com/anak10thn/toroo-burg-themes/tree/bdb22130e53b8ebab9f7fe53ba2fd058b552e569) |

This expands the real-world corpus beyond the bundled 16, but the added themes
share a similar layout lineage. Color counts and selection changes are smoke
checks, not pixel comparisons against original BURG. Wider layout diversity,
other resolutions, dialog coverage, and hardware testing remain open; the broader
compatibility gate is still in progress.
