# WartBURG: gated GitHub + Gitea mirroring

WartBURG is developed on the self-hosted **Gitea** (`gitea-local`,
`git@gitea-local:goldsteal/WartBURG.git`) as the canonical source of truth, and
**mirrored to a GitHub repo that stays private until Wartburg day (2026-10-18)**.

```
  developer ──push──▶  Gitea  (canonical, origin)
                         │ push-mirror (automatic)
                         ▼
                       GitHub  (goldsteal/WartBURG — PRIVATE until 2026-10-18)
```

## One-time setup

### 1. Create the GitHub repo (gated)
Create an **empty, PRIVATE** repo `goldsteal/WartBURG` on github.com
(no README/license — it receives a mirror push). Keep it private; flip to public
on launch day. For *this test* it may be made temporarily public so the
`curl … | bash` raw URL and Release assets are reachable without a token.

### 2. Local remote (this host can push directly over the existing SSH)
`ssh -T git@github.com` already authenticates as `goldsteal`, so:

```bash
git -C grub remote add github git@github.com:goldsteal/WartBURG.git
git -C grub push -u github wartburg
```

### 3. Gitea push-mirror (keeps GitHub in sync automatically)
In Gitea: repo **WartBURG → Settings → Mirror Settings → Push Mirror**:
- **Git Remote Repository URL:** `https://github.com/goldsteal/WartBURG.git`
- **Authorization:** GitHub username `goldsteal` + a **fine-grained PAT** scoped to
  *only* this repo with `Contents: read/write` (create at GitHub → Settings →
  Developer settings → Fine-grained tokens). Use the PAT as the password.
- **Sync interval:** `8h` (or push on demand). Tick *"Sync when commits are pushed"*
  if available.

Gitea now mirrors `wartburg` (and tags) to the private GitHub repo on every push.

## Gating rules
- GitHub repo **stays private** until 2026-10-18. `curl | bash` from raw
  GitHub will only work while the repo is public (the test window) or via the
  Release assets if those are attached to a public release.
- Signing keys live OUTSIDE the repo (`../secureboot-keys/`); only `WartBURG-MOK.cer`
  / `.crt` (public certs) are ever published — never `WartBURG-MOK.key`.
- The release artifacts (`wartburg-dist.tar.gz`, `WartBURG-MOK.cer`) are attached
  to a **GitHub Release**, not committed (they are in `.gitignore`).

## Release cut (per build)
```bash
release/gen-mok.sh             # once, ever
release/build-signed.sh --build  # -> dist/ + wartburg-dist.tar.gz
# upload wartburg-dist.tar.gz + dist/WartBURG-MOK.cer as a GitHub Release asset
```
