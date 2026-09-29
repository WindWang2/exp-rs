# R6 REVIEW LOG — adversarial review round 1 (subagent #2, read-only)

Reviewer scope: full diff 1e28de867..working tree + adjacent audit of #1403/#1405/#1406.
VERDICT at review time: BLOCK (5×P1, 8×P2, 4×P3).

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P1 | provenance_projection.cpp: raw LF in string literal + missing function brace (slice-replace damage) | FIXED — literal restored to `"\n"`, function `}` re-added before the namespace close |
| 2 | P1 | cli_batch_runner.cpp: `'\n'` char literal mangled to raw LF | FIXED |
| 3 | P1 | #1394 removal: mission-runtime-gate/tests/test_mission_runtime_persistence.cpp:254,297 still called saveMissionTimelineToSidecar; 4 gate targets compile mission store TUs without Sicnu::Platform | FIXED — gate test now writes legacy fixtures via a local QFile helper (fixtures are not production writers); `sicnu_add_mission_runtime_test` links Sicnu::Platform |
| 4 | P1 | output_committer: passing the temp directly to publishStagedFile → Windows MOVEFILE_COPY_ALLOWED fallback is a NON-ATOMIC copy onto the live target after the .old rename (#617 regression) | FIXED — restored the `.new` same-dir staging + one-step rename flow (atomic on both platforms) and added the missing `fsyncFile(staging)` durability gate; .old backup/rollback/fault-point unchanged |
| 5 | P1 | batch_assessment.cpp: BOM literal carried raw UTF-8 bytes (fragile) | FIXED — restored `"\xEF\xBB\xBF"` hex-escape form |
| 6 | P2 | readWholeFile: stat error classified as Missing (fail-open) | FIXED — ec set → Unreadable; only non-existence is Missing |
| 7 | P2 | injected-fault branches dropped StagedCleanupFailed reporting | FIXED — all three hook branches mirror the real failure branch incl. cleanup status |
| 8 | P2 | workflow sweep globs only the old `.json.tmp.*` shape; authority temps never swept | FIXED — added `checkpoint_*.tmp.json` sweep with runId reconstruction (pid.ctr.rng suffix) and the same LiveOwner probe guard |
| 9 | P2 | size cap was stat-then-unbounded-read | FIXED — in-loop cap enforcement on both platform branches |
| 10 | P2 | session journal rewrites hit the 32 MiB authority default | FIXED — explicit 64 MiB write cap with headroom over the 1 MiB compaction threshold |
| 11 | P2 | exported sicnu_platform target shipped without headers | FIXED — install(FILES durable_sidecar.h portable.h DESTINATION include/platform COMPONENT sdk) |
| 12 | P2 | classification: torn-but-READABLE main returned Main, failed JSON decode, never consulted last-good | FIXED — on Main-source decode failure the reader retries the last-good copy before failing |
| 13 | P2 | DECISIONS D5 said putBlock migrated | FIXED — recorded as EXEMPT with rationale |
| 14 | P3 | workflow publish-fault consumed at entry → temp-cleanup assertions vacuous for that case | ACCEPTED — contract (nothing promoted, old intact) preserved; documented; the write-fault case still exercises the real branch |
| 15 | P3 | Sicnu::Platform linked on sicnu_workflow_runtime while the TU lives in sicnu_workflow (SHARED) | FIXED — link moved to sicnu_workflow; sicnu_workflow_runtime restored to its original single line (also repaired the mangled line from the earlier patch) |
| 16 | P3 | dead code: session_journal stagingCounter/<random>, unused QSaveFile includes (study/studio/registration/lab/teaching/mission stores), duplicate include in study_export, unreferenced fsyncDirectory, stale comments, one-line return/format | FIXED (all) |
| 17 | P3 | concurrent test accepts typed publish failures on Windows | ACCEPTED — documented contract: Windows ReplaceFileW same-target races/AV produce real typed failures; invariants (completeness, zero torn reads) asserted unconditionally; POSIX stays strict zero-failure |

## Adjacent audit (no re-fixes; recorded)

- #1403: crash window genuinely closed (hardlink backup keeps the entry alive;
  main-last ordering; typed restore). Residuals noted: atomic_fs.h doc omits
  MOVEFILE_COPY_ALLOWED; failure-path restore has a narrow remove→rename window;
  .bak/.publish-cross-device residue unswept (documented).
- #1405: split-insert txn and promotion gates sound. Adjacent observation:
  dataset_store.cpp runs `PRAGMA journal_mode=WAL` BEFORE the schema gate.
  ACCEPTED AS DESIGNED (not a defect): the pragma is connection-level and a
  no-op on stores already in WAL (the only stores this code family writes);
  the up-front ds_meta creation is #1405's explicit gate carrier ("Only ds_meta
  is created up front"). Re-ordering would re-litigate a fresh #1405 decision
  with no observed harm — recorded per the adjacent-audit-only rule.
- #1406: ordering correct; "crash-durable" is detect-and-refuse via checksums
  (QSaveFile never fsynced — members now on the authority raise that).

## Post-remediation verdict

All P1/P2 fixed; P3-14/P3-17 dispositioned as accepted-with-rationale. Re-verification
is the final Oracle double-run (subagent #3 reviews the remediated tree).
