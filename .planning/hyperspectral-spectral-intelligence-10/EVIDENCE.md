# EVIDENCE — hyperspectral-spectral-intelligence-10

Policy: every capability claim maps to a local command + exit code or is
marked `not-executed`. Append per phase.

## Phase 0

* `git fetch --all --prune && git pull --ff-only` → master == origin/master == `7d78059d1a6d316d606656759a506d17bc5e3b55`. (First pull attempt: transient `TLS connect error … unexpected eof`; clean on retry.)
* `git worktree add ../exp-rs-hyperspectral-spectral-intelligence-10 -b zcode/hyperspectral-spectral-intelligence-10 origin/master` → OK.
* `git add -n .planning/hyperspectral-spectral-intelligence-10/PROBE.md` → "add '.planning/…/PROBE.md'" (whitelist effective).
* `gh pr list --state open` → empty. Remote branches: only `origin/master` + itk-upstream (all zcode/* merged branches deleted).
* Disk baseline: root fs 94% used / 56 GB free; `/tmp` 32 GB tmpfs 50% used; transient ENOSPC observed once writing a shell cwd file in /tmp → build dirs stay on /home; df monitored per phase.
