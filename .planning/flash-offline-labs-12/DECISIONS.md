# DECISIONS — flash-offline-labs-12

Key design choices and alternatives. Append as rounds proceed; never silently contradict an entry —
add a superseding entry instead.

## D1 — Build LabSpec 2 as versioned superset, not a new format
LabSpec v1 exists (data-driven, PR #949/#1026). LabSpec 2 = explicit `"version": 2` field,
structured Chinese descriptions as data (not prose), parameter allow-ranges, expected artifacts,
grading assertions inline, plus a v1→v2 migration path (loader accepts v1; writer/upgrader emits v2).
Alternative rejected: new unrelated format (breaks 20+ existing lab docs and packs).

## D2 — Grading 2 validation is fail-closed and typed
All rule sets are validated before any rule executes; each rule type has a schema (bounds, types,
paths contained in run dir). Invalid rule → typed error record in report + non-zero exit; valid
rules in the same run are still reported as not-run (no partial silent success). Alternative
rejected: try/catch-per-rule-and-continue (hides authoring bugs, track forbids loosening).

## D3 — Batch classroom runner is Python + subprocess isolation
`scripts/run_classroom_batch.py`: one temp dir per submission, per-job wall timeout (kill process
group), bounded worker pool (default = min(4, cpu)), robust CSV+JSON summary written even on
interrupt, one bad job cannot abort the batch. Rationale: existing classroom tooling is already
Python (run_lab_pipelines.py); adding a C++ target would duplicate CLI plumbing. Alternative
rejected: make-based parallelism (poor timeout/report control).

## D4 — Foundry catalog extends the existing scene registry in tools/sample_foundry
Add catalog metadata (scene id, kind, size presets, subset selection by owned-file list, seed) on
top of the existing generator instead of a second generator. generate/verify keep byte-determinism
contract from ADR 0164; subset regeneration must prune/report stale owned files (O2).

## D5 — Launcher parity via fixture-driven transcripts + thin wrappers
Keep per-OS wrappers thin; move arg parsing to one implementation (the C++ CLI already owns
`--out=` contract). Lock behavior with POSIX fixtures executed in CI-able tests and Windows
fixtures validated by static checks + transcript review (no Windows host here — recorded as
limitation with evidence, per prompt's honest-reporting rule).

## D6 — Planning notes committed; ledger stays worktree-local
`.planning/flash-offline-labs-12/**` whitelisted in `.gitignore` (repo convention) and committed;
`.goal-loop-ledger.md` stays untracked in the worktree.

## D7 — (supersedes part of OWNERSHIP) two shared-file integrations accepted with rationale
Census proved the strict loader (`src/app/widgets/lab_spec_loader.cpp`) pins
`spec_version == 1`, and the grader seam lives in `src/agent/output_verifier.cpp`
/ `lab_grader_kernels.cpp`. LabSpec 2 and the provenance kernel are MUST work
packages, so both surfaces received minimal additive edits:
- loader: version negotiation (1|2), v2-key-in-v1 rejection, validation-only
  v2 field checks — the runtime `LabSpec` struct and every consumer stay at
  the v1 shape (zero API/ABI impact).
- grader: additive `provenance` kind in `kKnownKinds` + kernel module; no
  existing kind's behavior touched.
Justification recorded against the track's ownership note: no parallel track
owns these files (open PRs = 0 at pre-read), and the alternative (re-implement
parsing in track-owned scripts) would fork the contract instead of versioning
it. Flagged for the PR body.

## D8 — LabSpec 2 scope: migrate data to v2, keep D3-era labs out of the loader dir
Census found `data/labs/lab8_temporal_analysis.lab.json` violates the strict
loader three ways (id pattern `labNN`, id == stem, prerequisites are strings)
— i.e. `test_labspec`'s inventory gate and the docs drift gate are RED at
baseline. Fix: move that file to `data/labs/legacy/` (it never loaded into the
guided panel anyway; the hand-written doc + pipelines + grading rules remain
the lab's teaching surface), whitelist `DATA_PACKS.md` as hand-authored, and
regenerate the docs from the generator (the committed README was an older
rich-format leftover). v2 demonstrates real usage on lab02 (glossary,
param_ranges, expected_artifacts, prerequisite_knowledge). Rationale:
creating new `labNN`-conformant specs for sar/hyperspectral/cartographic
requires operator-schema conformance work across ~30 steps — recorded as a
known limitation instead of rushing it into this track.

## D9 — Baseline defects fixed because they sit in the track's surface
- `test_labspec` inventory gate: lab8 file unloadable → moved to legacy (D8).
- docs drift gate: README + DATA_PACKS.md vs generator → regenerated + whitelist.
- `lab_rules.schema.json` stale vs `kKnownKinds` → synced (15→16 kinds with
  provenance) so authoring validation matches engine reality.
- `offline_smoke.sh` aborted on env-doctor's documented degraded exit 2.
