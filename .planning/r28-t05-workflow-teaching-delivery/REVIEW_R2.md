# REVIEW R2 — independent second review (fresh context, post-fix)

Reviewer: general-purpose subagent with no implementation context; read the
diff, sources, tests and build evidence; ran the suites and reproduced the
delivery chains itself. Full report archived verbatim in the agent transcript.

## Part A — R1 fix verdicts (all FIXED-CORRECT)

P1-1 (negative counts, both paths), P1-2 (dry-run writes nothing — every FS
call traced; one residual P3 below), P1-3 (oversized checksums refusal),
P2-4 (exclusive 2^63 both paths), P2-5 (third unbounded read removed),
P2-6 (relative epsilon + genuine ulp pair), P2-7 (sorted sparse emission).
New regressions verified as genuine pins; 17/17 (now 21/21 with the R2
regressions) green.

## Part B — evidence verdicts

- **B1 snapshot regeneration: VALID.** Committed file == generator output
  (re-ran the generator); drift is real product change (#1739 demUnit alias
  table; #1733 approve removal); data/contracts/* untouched.
- **B2 configure honesty: VALID** (caveat addressed: the gate is the root's
  full-component find; sicnu_processing itself needs only core — message and
  comment now say so; the always-true conditional was simplified).
- **B3 chain3_install.sh: was PARTLY VALID — FIXED.** Check 10 (artifact
  reopen) was vacuous (both branches PASSed unconditionally, passport rc
  discarded). Now a real assertion (artifact must exist, passport rc=0 with
  --json content); the script also clears the poisoned resume scratch and
  uses `passport --path`.
- **B4 D-T05-CHAIN1: CONFIRMED with full root cause** (see ISSUE_TRIAGE.md —
  the chunked-run `.published` marker is keyed without the output path, so
  repeated runs report success with zero tiles sinked). Owner: T02 lane;
  handoff recorded.
- **B5 fast-lane classification: conclusion VALID, enumeration was
  INCOMPLETE — FIXED.** 71 failures re-derived and matching; attribution rule
  sound; the doc now names all 71 (the 10 missing entries — #667 self-heal,
  both #727 resume tests, JobEngine bucketed queue, I9 join, plan bounds,
  decision_record, live operator scan, GDAL reproject, RemoteDatasetPool ×3,
  python worker typed — are added).
- **B6 planning docs: problems FIXED.** chain2_experiment.sh was broken
  (missing `--pipeline`, no export/validate steps) — rewritten and now 6/6
  green; the four chain scripts were git-ignored — whitelisted and committed;
  stale numbers corrected (21/21 not 14/14; 9,793 measured fast-lane);
  ACCEPTANCE.md gate table updated.

## Part C — new findings (all fixed in 9f6c93ab1b)

- [P3] evaluation.cpp increment()/setCount() out-of-grid → heap WRITE past the
  totals vectors on a read path → now bounds-checked (regression added).
- [P3] dry-run's unconditional QFile::remove(staged) could delete a
  pre-existing .importing file → now staging-gated (regression added).
- [P3] dry-run of an already-imported bundle reported dryRun=false → now
  echoes the requested mode (regression added).
- [P3] sparse fromJson r/c used toInteger(-1), truncating fractional
  coordinates → now requires exact integral in-grid indices (regression
  added).
- [P3] EvidenceIndex implicit copy/move left buckets pointing at the
  source's storage → copy/move deleted.
- [P3-evidence] delivery-chain evidence not reproducible from the branch →
  scripts committed + fixed.

## Deferred (with rationale)

- toJson() drops the dense `counts` key above 1024 labels (forward-compat:
  an OLDER binary cannot read a sparse doc). In-repo consumers are safe (no
  raw-counts readers); a format-version field would be the clean fix — noted
  as a follow-up because no consumer exists today.
- macro*/toJson recompute per-class aggregates (~12 passes; measured 16.0 s
  at the 4Mi-cell budget). Perf-only, no hang/OOM; hoisting is a follow-up.
- No test exercises the 4Mi-cell budget at scale (largest: 9000 cells).
