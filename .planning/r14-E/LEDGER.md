# R14-E Work-Package Ledger

Legend: FIXED (this track, RED→GREEN) · ALREADY_FIXED_ON_MASTER (verified, no diff) ·
OWNED_BY_OPEN_PR · OUT_OF_OWNERSHIP · NOT_APPLICABLE · RESIDUAL (documented, not fixed).

| # | Work package | Finding | Status |
|---|--------------|---------|--------|
| 01 | 本科实验用户流程审计 | explore-agent sweep of teaching/lab/lab_pack entries; no unmapped step seams found | AUDITED (negative) |
| 02 | Lab Session 状态恢复 | event `seq` outside `(0, last_seq]` adopted by parser → forged verdict wins latest-wins gates after reload (`src/lab/session_state.cpp`) | **FIXED** |
| 03 | 自动批改及解释 | grader feedback/status lattice audited (2nd agent): no failed→passed transition, NaN refused, cycles refused, duplicate evidenceId refused | AUDITED (negative) |
| 04 | 教师批量作业发现 | (a) QDir follows symlinks → grades files OUTSIDE submissions root; dangling links vanish silently. (b) NFC/NFD student-id variants split one student into two | **FIXED** (a) + (b) |
| 05 | Windows 离线实验 | `resolveDefaultPythonInterpreter` env pins + Store-alias probing verified on master | ALREADY_FIXED_ON_MASTER |
| 06 | 教学数据包校验 | pack verify confines inputs (#1186 guard), atomic session store, fail-closed reload verified | AUDITED (negative); oversize docs now typed refusals (**FIXED**, bounded reads) |
| 07 | Study/实验设计 | stepCount<2, min==max, seedReplicates<1, seed>2^53 all refused (study_spec.cpp); aggregation ordered-container discipline verified | AUDITED (negative) |
| 08 | Experiment Studio 表格规模 | unresolved #1482 review thread: 6 QTableWidgetItem per row (60k heap objects @10k rows) | **ADDRESSED_BY_OPEN_PR #1510** (dedupe at push time, see below) |
| 09 | 验证证据真实性 | verify/verify_adapters fail-closed audit: absent→typed failure, zero checks→Indeterminate, non-regular files refused, bounded digests | AUDITED (negative) |
| 10 | 统一 Canonical JSON | master has isUInt64 path (#1488); exception barrier owned by open PR #1509 | ALREADY_FIXED / OWNED_BY_OPEN_PR |
| 11 | FaultLab 确定性 | (a) `Changed` for text/truth passed vacuously when clean baseline absent. (b) LO-id grammar accepted `LO-abc` despite documented `^LO-[0-9]{2}$`. Determinism itself clean (no wall-clock/rand, seeded streams, digest re-check) | **FIXED** (a)+(b); determinism AUDITED (negative) |
| 12 | 科学适配性 preflight | pair_crs/pair_resolution/quality/temporal silently dropped unresolved raster slots → "fewer than two raster inputs" PASS | **FIXED** (typed SPF_*_UNKNOWN + insufficient_facts) |
| 13 | 教学帮助/术语对接 | explain module audited honest (no fabricated state); `src/help/terminology_provider.cpp` dedup-suffix defect is `src/help/**` | OUT_OF_OWNERSHIP (recorded for owner) |
| 14 | 大班级性能与资源 | unbounded `readAll()` slurps: checkpoint (64 MiB cap), bundle manifest (16 MiB), pack documents (16 MiB) — now stat-gated typed refusals via shared `readBounded` | **FIXED** (100/500/1000-scale grading path unchanged otherwise; bounded capture already on master) |
| 15 | 安全与隐私边界 | symlink escape (WP04a) was the concrete exfiltration seam; grader temp/output hygiene otherwise typed | **FIXED** (WP04a) |
| 16 | 长期回归和用户体验 | existing dock/e2e suites kept green; new regression oracles added per fix | **FIXED** (via per-fix RED→GREEN×2) |

## WP08 dedupe decision (2026-10-15, before push)

PR #1510 (r13-l, opened 11:42Z today) landed on the SAME three files
(`src/app/experiment_studio/experiment_studio_dock.{h,cpp}`, `tests/test_experiment_studio_dock.cpp`)
implementing the #1482 thread's OTHER accepted arm: a 500-row bounded preview with truthful
status disclosure (the reviewer asked for "a model-backed table OR genuinely bounded/lazy
population"). Per the R14 dedupe protocol the model-backed implementation this track had
built, RED→GREEN×2'd and reviewed (10k rows in 19 ms, zero per-cell widgets) was DROPPED at
push time (`git rebase --onto origin/master`); the measurement is retained below as evidence
the alternative exists if the maintainer prefers unbounded virtualized display. After the
drop, this branch shares zero files with every open PR.

## Additional fixed findings (outside the 16 WP numbers, in-scope)

- **RecipeRouter data race** (`src/science_context/recipe_router.*`): agent path could
  `refreshRecipes()` (vector replace) while another thread iterated in `synthesize()`'s
  `search()`. Fixed with an internal mutex; registry I/O deliberately stays outside the lock.
  → **FIXED**
- **Sidecar non-finite accuracy** (`src/scientific_state/model_sidecar.cpp`): jsoncpp parses
  `1e999` to +inf; value poisoned the passport round-trip (written null, refused on re-read).
  → **FIXED** (finitess gate at the typed boundary)

## Residual risks (documented, not fixed — with reasons)

1. **grader vs asset_state_resolver ISO-8601 grammar divergence** (audit F4, P3): grader accepts
   bare `±HH` offsets and refuses >9 fraction digits; resolver requires `±HH:MM`/`±HHMM` and
   accepts unbounded fractions. Both fail closed in their own domain (refusal/typed unknown vs
   correct ordering), so this is inconsistency, not corruption. A shared parser crosses the
   grader↔scientific_state module seam and would churn code #1464 just stabilized → documented,
   deferred.
2. **Cross-student leak regex ASCII-only** (`feedback_pack.cpp:52`): `student_[A-Za-z0-9_-]+`
   cannot capture non-ASCII ids in leak scanning. P3, display-path only. Deferred.
3. **Report counter reconciliation wrinkle** (`batch_assessment.cpp`): a row both
   `status=="unavailable"` and `missingEvidence==true` counts only in `missingEvidence`;
   documented deliberate in comments. Deferred (comment already explains intent).
4. **spec_runtime authoring gap**: `choice` question carrying `expected_numeric` silently
   ignored. P3, authoring-validation only. Deferred.

## Negative audit results worth keeping (evidence the sweep happened)

- lab_pack `verify()` confines inputs and canonicalizes containment; session store save/load
  atomic + fail-closed; study sampling LHS fixed draw order; suitability profiles/time clean;
  verify family entirely fail-closed (bounded digests, Indeterminate for unanswerable);
  grader no failed→passed transitions, NaN refused, cycle/self-ref refused; faultlab zero
  wall-clock/rand usage; explain refuses ungrounded evidence; broker cache bounded LRU + mutex.
