# Known issues

## 1. Cold-boot black screen on some GPU + DisplayPort combos (GOP modeset race)

**Symptom.** On a cold boot the menu is **black but alive** — pressing `Enter` boots the
default entry, arrow keys still navigate blind. It is **intermittent**: it rides the
monitor's power-on timing, so some boots are fine.

**Observed on.** An NVIDIA **GTX 980 (GM204 / Maxwell)** driving a panel over **DisplayPort**,
on this project's development host. Likely affects other GPUs/firmware whose UEFI GOP and DP
link-training race on cold boot.

**Root cause.** A graphical bootloader must perform a **GOP graphics modeset** to draw. On
these setups that modeset can fire *before the monitor has finished DisplayPort link
training/power-on*, so the panel never syncs to the freshly-set mode → black, while the
bootloader renders fine underneath. It is a **timing race on the act of switching modes**, not
a wrong resolution.

**What does NOT fix it.**

- Pinning the resolution — even to the panel's **native** mode (`set gfxmode=1920x1080`,
  `GRUB_GFXMODE=1920x1080`). Verified on the dev host: the pin landed in the live `grub.cfg`
  and it still black-screened. The mode *value* was never the problem.
- The installer's `--gfxmode` mode list. That only avoids "`auto` picks an unsyncable mode";
  it cannot avoid the modeset itself.

**What does fix it — and why WartBURG can't use it.** Not doing a graphical modeset at all:
`GRUB_TERMINAL=console` makes **stock** GRUB reuse the firmware's POST text mode (already
synced), so it never goes black. But WartBURG is a **graphical** menu by definition — it
*requires* the GOP modeset to draw a theme — so it cannot take this escape hatch. On an
affected host, WartBURG (like any graphical bootloader menu) will hit the same race.

**Recommendation.**

- Treat an affected machine as a **poor on-metal target** for a graphical bootloader. Use the
  QEMU/OVMF harness and fresh VMs (well-behaved virtual GOP) to develop and test WartBURG.
- If you must boot such a host reliably, keep a **non-graphical stock GRUB**
  (`GRUB_TERMINAL=console`) as the default boot entry and treat WartBURG as opt-in.

**Diagnostic.** Set `GRUB_TERMINAL=console`, `update-grub`, and cold-boot several times:
- rock-solid → confirms the GOP-modeset race (graphical menus are the risk on this box);
- still black → the failure is *earlier than the bootloader* (firmware/GPU POST handshake),
  and nothing in GRUB/WartBURG can address it.

## 2. Themes using not-yet-ported components

WartBURG renders the BURG theme **format**; all 16 bundled themes render in-theme. A theme that
uses a component class not yet ported will **render everything else** and skip the unknown
widget rather than fail. Report such themes so the component can be added.

## 3. Restricted-entry authentication uses GRUB's standard prompt

Entries marked `--users` are authenticated through GRUB's own mechanism before booting (no
Secure Boot bypass). A *fully themed* credential dialog is constrained by what the current GRUB
auth API exposes, so the prompt is GRUB's standard one rather than a themed widget.

## 4. Build must use a single toolchain stratum on Bedrock Linux

On Bedrock Linux, run the GRUB build and binutils under one self-consistent stratum
(`strat arch make …`). Mixing strata breaks the GRUB/binutils toolchain (missing
`Automake/Config.pm`, `libguile`, `libbfd`, etc.). The installer auto-detects Bedrock and
prefixes `strat arch`.
