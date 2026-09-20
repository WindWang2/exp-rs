# DEDUP — glm53-scientific-verification-12

Live overlap determination against existing PRs, branches and issues.
**Rule: never cherry-pick a stale branch; never duplicate a merged feature.**

## 1. Open PRs — NONE

`gh pr list --state=open --limit=100` → **0 results**.

No open PR covers any part of this track. There is nothing to shrink the
scope for.

## 2. Open issues — NONE

`gh issue list --state=open --limit=200` → **0 results**.

No open issue must be satisfied by this track.

## 3. Remote branches — all judged "historical residue", none is a baseline

| Branch | Commits ahead of `adf8f9895` | Verdict | Rationale |
|---|---|---|---|
| `agent/ds41-http-fetch-strict` | 2 (`1b8a55a9d`, `27c7f4c6a`) | **superseded-adjacent / not merged** | Touches `httpFetch` strict status → **#1092 was closed by PR #1110** on master. Its `tests/test_io_http_fetch.cpp` is a *different* concern (typed status vs dead `truncated` flag). No overlap with `src/contracts/**`. |
| `agent/ds41-pipeline-drag-lifetime` | (already local) | historical | = #1085 fix, landed via #1112 |
| `agent/ds41-range-cache-msvc` | (already local) | historical | = #1055 fix, landed via #1058 |
| `agent/ds41-remote-pool-init` | (already local) | historical | = #1047/#1098 fix, landed via #1098 |
| `agent/flash-data-transaction-integrity` | — | superseded | #1045 → PR #1105 |
| `agent/flash-geo-fabric-integrity` | — | superseded | #1053/#1082 → PR #1110 |
| `agent/flash-lab-foundry-determinism` | 3 | **unmerged increment** | Touches `tools/sample_foundry.{h,cpp}` only — outside this track's owner scope; read-only evidence for the "scene the foundry cannot produce" #1087 class |
| `agent/flash-mcp-containment-routing` | — | superseded | #1079 → PR #1106 |
| `agent/flash-processing-atomic-errors` | — | superseded | #1043 → PR #1104 |
| `agent/flash-workflow-integrity` | — | superseded | #1078 → PR #1107 |
| `agent/glm53-desktop-lifecycle` | (different track) | **different track** | Desktop/lifecycle, no `src/contracts/**` overlap |
| `agent/glm53-plugin-sdk-trust` | (different track) | **different track** | Plugin trust, no overlap |
| `fix/ci-master-unblock` | 2 | **unmerged CI fix** | `src/geospatial/io/*` + cmake protobuf — no overlap |
| `fix/r2-ci-protobuf-multimode` | — | superseded | CI portability |
| `fix/review-issues-1033-1056` | 1 (127 files, +2944) | **large unmerged batch** | Wide fail-closed remediation; **does touch `tests/CMakeLists.txt`** (append-only shared). No `src/contracts/**` overlap, but is the biggest rebase hazard. |

**Conclusion**: no branch overlaps this track's writable surface
(`src/contracts/**`, `data/contracts/**`, `tests/verification12/**`,
`docs/verification/**`). Two branches touch the shared append-only
`tests/CMakeLists.txt` → expect append-merge conflicts, resolve by append.

## 4. Concurrent sibling track (same 12.0 wave, live process)

`agent/glm53-mission-workbench-12` exists in a **locked, actively-written
worktree** (`../exp-rs-worktrees/glm53-mission-workbench-12`, `index.lock`
present and reappearing during this session).

At baseline it is **byte-identical to `adf8f9895`** (`git rev-parse` equal,
`git diff` empty) — it has landed **nothing** yet.

- Its declared scope is **Mission Workbench** (an app/UX surface), not
  verification. No file-level overlap expected.
- Risk: it may append to `tests/CMakeLists.txt` / `CHANGELOG.md`.
- Mitigation: keep all my own CMake additions in one contiguous appended
  block; rebase before PR.

**Do not touch that worktree.** A previous session's interrupted
`worktree add` already corrupted the shared ref store (see BASELINE.md
"Repo health incident"); interfering with a *live* worktree would repeat it.

## 5. Merged predecessor — the real dedup target

**PR #1027 (F09 Verification 11.0)** is merged and is the platform this track
extends. Everything in its file list is *prior art to build on*, not to
repeat. Specifically **already done and must not be re-implemented**:

- determinism census + snapshot gate
- contract-or-exemption over the full live registry
- replay determinism corpus (byte-identical)
- 6 metamorphic relations with sensitivity controls
- long-double numeric references (NDVI/SAVI) + analytic translate/clip/threshold
- 10-mutant mutation kill
- failure lane F1–F5 (corrupt / missing / bad params / pre-set cancel / missing out-dir)
- cross-surface welding + capability-aware ladder, READINESS report

## 6. Recorded 11.0 known-limitations (candidate seeds for 12.0)

From the #1027 PR body — *seeds, re-verified by 12.0's own evidence, never inherited as claims*:

1. 5 pre-existing master L2 failures (spectral unmixing sidecar drift, 3 dup
   capability entries, `cartography.repair` help gap, CATEGORICAL_MISMATCH page
   gap, matched_filter projection).
2. Temporal time-shift metamorphic relation **not implemented**.
3. Ladder lanes L3–L5/L7 not built on this host.
4. Graph scanner cannot see lambda-registered / inline-schema operators
   (cartography x5 + io-fabric x4) — pinned to an exact missing set.
5. `band_ratio` IHS-mode NoData audit.

## 7. Freshness re-check obligation

Before opening the PR this file is **re-generated** after a fresh
`git fetch origin --prune` + `gh pr list` + `gh issue list`; if master has
drifted, rebase and re-run every affected oracle (track brief §独立 Review).
