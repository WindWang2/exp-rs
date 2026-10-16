# R14-E Oracles — RED → Fix → GREEN×2 evidence map

Each row: failing input · old behavior · fix · test (file + case) · run evidence captured in
TEST_RESULTS.md. "Compile-RED" = the new test could not compile against pristine master
(API-shape oracle); runtime REDs are asserted inside the cases against the old behavior.

| Oracle | Failing input | Old behavior | Fix | Test |
|---|---|---|---|---|
| O1 studio matrix model-backed | 10k-row StudyReport projected into the B tab | 6 QTableWidgetItem per row allocated (60k heap widgets) — unresolved #1482 review thread | `RunMatrixTableModel` (QAbstractTableModel) + QTableView; view virtualizes; read-only — **DROPPED at push time: PR #1510 addresses the thread with its bounding arm; dedupe per protocol (LEDGER §WP08)** | (evidence retained: compile-RED captured; 10k rows in 19 ms measured) |
| O2 discovery symlink containment | `submissions/bob -> /home/other/dir`, `alice/leak.json -> external file`, dangling link | links followed (grades content outside root); dangling link vanished with no row | per-entry classification: links (live/dangling) + specials → typed DiscoverySkip; hidden/nested keep old behavior | `test_teaching_batch_discovery_r10.cpp` "symlinked entries are refused, not followed" |
| O3 NFC student ids | 张三(NFC dir) + 张三(NFD dir) | two students, counts cannot reconcile | ids normalized to NormalizationForm_C at both derivation points | `test_teaching_batch_discovery_r10.cpp` "Unicode normalization variants grade as one student" |
| O4 bounded document reads | >16 MiB pack/manifest, >64 MiB checkpoint | whole-file readAll() slurp before any parse (OOM vector) | shared `readBounded` stat-gate; oversize → typed issue / fresh-start degradation | `test_teaching_batch_discovery_r10.cpp` "oversize documents are typed refusals" |
| O5 lab session seq envelope | doc with `checkpoint_results[].seq=500`, `last_seq=3` | parsed OK; forged verdict wins every latest-wins gate after reload | post-parse envelope check: every event seq ∈ (0, last_seq] else `lab.session.schema` | `test_lab_runtime.cpp` strict-envelope SECTIONs |
| O6 faultlab Changed baseline | text/truth observable present only in faulted set | `passed=true` vacuously | fails with note "clean baseline for this observable is absent" (mirrors number path) | `test_faultlab.cpp` "Changed without a clean baseline fails" |
| O7 faultlab LO grammar | `learning_objective.id = "LO-abc"` | accepted (grammar was `[a-z0-9_]+`), message claimed `^LO-[0-9]{2}$` | exact 2-digit grammar enforced, matching published JSON schema | `test_faultlab.cpp` "ids outside ^LO-[0-9]{2}$ are refused" |
| O8 sidecar finite accuracy | sidecar text `overallAccuracy: 1e999` (jsoncpp → +inf) | +inf flowed into passport; written as null; re-read refused own output | finiteness gate at sidecar parse → typed InvalidField | `test_scientific_state_provenance.cpp` "refuses a non-finite overallAccuracy" |
| O9 preflight unresolved slots | pair rule with secondary slot facts unresolved | slot dropped silently → PASS "fewer than two raster inputs" | unresolved slots → SPF_CRS/RESOLUTION/QUALITY/TEMPORAL_UNKNOWN + insufficient_facts (pair/quality/temporal) | `test_preflight_rules.cpp` "unresolved raster slot degrades to typed unknown" |
| O10 router concurrent replace | thread A `refreshRecipes()`/`clear()` while thread B `search()` | data race on `mRecipes` vector | internal mutex; registry I/O outside the lock | `test_science_context_broker.cpp` "consistent under concurrent search and replacement" (smoke; race itself is TSan-lane territory) |

## Dedupe oracles (no code change, evidence recorded)

- #1466 ×5, #1464 ×2, #1492, #1465 broker-key + std::equal ×4, #1488 master part: verified
  fixed on master by direct code read (BASELINE.md table). No diffs created.
- #1488 exception barrier + agent_adapter/capability_router + canonical_json: open PR #1509 —
  untouched to avoid collision.
