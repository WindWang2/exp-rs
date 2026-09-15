# EVIDENCE — deployment-packaging-11

Evidence policy (goal-template D-024): every capability claim = local command + exit code,
or an explicit `not-executed` with the blocking condition. Nothing else.

## Phase 0 (audit + planning)

- `git fetch origin --prune && git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`, exit 0.
- `gh pr list --state open` → #1009 (MERGEABLE), #1008 (CONFLICTING); #991/#992 confirmed merged via `git log --decorate`.
- `gh issue list --state open` → #1001–#1007, all scientific-domain (see BASELINE §1).
- `gh pr diff 1009 --name-only` / `gh pr diff 1008 --name-only` → ownership table in PARALLEL_OWNERSHIP.md.
- Subagent #1 (read-only audit, 73 tool calls) → BASELINE §2.
- `make -n sicnu_geo_rs_cli` in main build-dev → 192 compile steps (build-strategy input; main tree not built).
- Host capability: `which cmake ninja g++ python3 pwsh docker` → all present except pwsh → **not-executed (host limitation): execution of Windows .ps1/.cmd scripts and pwsh-based conformance lane**.
- `git check-ignore -v .planning/deployment-packaging-11/GOAL.md` → no output (exit 1) after whitelist added → tracked OK (verify again post-commit).

## Phase 1 — manifest contract

(filled during Phase 1)

## Phase 2 — env_doctor module

(filled during Phase 2)

## Phase 3 — CLI surface + inventory scripts

(filled during Phase 3)

## Phase 4 — packaging surfaces + docs

(filled during Phase 4)

## Phase 5 — hardening + cleanroom

(filled during Phase 5)

## Phase 6 — validation

(filled during Phase 6)

## Phase 7 — review

(filled during Phase 7)

## Phase 8 — final

(filled during Phase 8)

## OUT_OF_SCOPE findings

- GUI has no `--offline` flag / offline-state surface; GUI (src/app) is read-only scope for
  this track (parallel-PR safety). Follow-up candidate. (BASELINE gap F)
- QNetworkAccessManager-based transports (LLM streaming client, file downloader) are not all
  routed through `offline_gate`; no test denies QNAM traffic. Crosses scientific/agent code →
  out of scope; recorded for follow-up. (BASELINE gap F)
- App version authority inconsistent (CLI `0.9.2-dev` vs GUI `1.0` vs vcpkg `1.0.0`); this
  track records versions as declared per-component in manifest `components` without forcing
  a repo-wide version unification (would touch app/cli/core — cross-track). (BASELINE)
