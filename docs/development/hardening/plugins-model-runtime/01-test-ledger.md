# Hardening 15/20 — plugins / model runtime: test ledger

Build: worktree `build-pmr`, Ninja, Debug, `-j2`, `LD_LIBRARY_PATH=$SDK_ROOT/usr/lib`
(test-binary runs directly; Catch2 self-registered).

## Oracle ↔ fix binding + RED/GREEN evidence

RED method: revert ONLY the binding source file (`git stash push -- <file>`), incremental
ninja rebuild of the affected test target, run the filtered oracle on the old
implementation, record the failing assertions, restore (`git stash pop`) + rebuild.

| # | Oracle (file → TEST_CASE) | Fix | RED on master | GREEN on fix |
|---|---------------------------|-----|---------------|--------------|
| 1 | test_exprs_plugin_system → "uninstall holds the cross-process install lock" | uninstall takes `CrossProcessInstallLock` | ✅ 5 assertions FAILED (:903-:914 — uninstall succeeded, deleted the install, no lock message) | ✅ pass |
| 2 | test_exprs_plugin_system → "install fails typed and bounded while the lock is held elsewhere" | POSIX `LOCK_NB` + 5 s bounded retry | ✅ **exit 124 — HUNG** (killed by `timeout 20`; the hang IS the defect) | ✅ pass (typed refusal, elapsed < 30 s) |
| 3 | test_exprs_plugin_system → "user index save survives a stale fixed-name temp" | pid-unique `<index>.tmp.<pid>` | ✅ FAILED (:977 — index never written when legacy `.tmp` name is occupied) | ✅ pass |
| 4 | test_plugin_host_process → "missing worker binary fails fast with a typed diagnostic" | POSIX `access(X_OK)` pre-check | ✅ FAILED (`typedFailure == false` — master reports `IpcProtocolError` after the full handshake wait) | ✅ pass (typed `HostProcessUnavailable`, < 4 s) |
| 5 | test_ensemble_detection → "detection lane applies linear scale and offset exactly like the raster lane" | gate + `v*scale+offset` parity | ✅ 2 CHECKs FAILED (fed-blob mean 200 / 400 on master vs required 100 / 350 — offset silently dropped; raw pixels fed when scale==1) | ✅ pass (100.0 / 350.0) |
| 6 | test_ensemble_scene → "non-finite member scores fail the scene ensemble typed" | finite gate in `classifyScene` | ✅ FAILED (:886 — master PUBLISHES the NaN product, run succeeds) | ✅ pass (typed `ComputationError`, no artifact) |
| 7 | test_ensemble_scene → "single-model scene classification refuses non-finite scores" | same gate, `probability` branch (`std::clamp(NaN)==NaN`) | ✅ FAILED (:912) | ✅ pass |
| 8 | test_model_tasks → "a sidecar failure keeps the previous raster product and its provenance" | single-input publish parks + restores the old sidecar | ✅ 2 CHECKs FAILED (:636/:638 — sidecar destroyed on master) | ✅ pass (product + byte-identical sidecar restored) |
| 9 | test_ensemble_parallel → "a sidecar failure restores the previous product instead of destroying it" (expectations updated by this branch) | ensemble-raster publish parks + restores the old sidecar | ✅ 2 CHECKs FAILED (:741 sidecar absent, :744 verifier not Ok on master) | ✅ pass (restored pair verifies `Ok`) |
| 10 | test_model_ensemble → "all-zero-weight ensembles refuse before any member forward" | weight-sum hoist before session acquisition | ✅ 2 CHECKs FAILED (member forwards == 2 on master — full raster passes + VRAM reservations for a statically undefined manifest) | ✅ pass (forwards == 0) |
| 11 | test_ensemble_detection → "all-zero-weight detection ensembles refuse before any member forward" | same hoist | ✅ 2 CHECKs FAILED (forwards == 2 on master) | ✅ pass (forwards == 0) |
| 12 | test_exprs_plugin_loader → "#1156" fixture repair | test-only: manifest fixture drifted behind the validator (no `capabilities`, operator without `external` section) — configure() marked the plugin Broken so the whole AB-BA oracle body NEVER ran on master | ✅ pre-existing failure on pristine master (first `REQUIRE(load)` false, probe: code=1004/1007) | ✅ pass — the AB-BA oracle executes again (27/27) |

## Suite results (fixed tree)

| Suite | Cases | Result |
|-------|-------|--------|
| test_exprs_plugin_system | 16 | all passed (incl. oracles 1-3) |
| test_exprs_plugin_loader | 27 | all passed (incl. repaired #1156) |
| test_plugin_host_process | 29 | all passed (incl. oracle 4) |
| test_model_ensemble | 12 | all passed (incl. oracle 10) |
| test_ensemble_scene | 8 | all passed (incl. oracles 6-7) |
| test_ensemble_detection | 17 | all passed (incl. oracles 5, 11) |
| test_model_tasks | 12 | all passed (incl. oracle 8) |
| test_ensemble_parallel | 6 | all passed (incl. oracle 9) |

Key oracles run twice consecutively before PR (see PR body).

## Honest oracle limits

- **P-C (`unloadAll` `record()` UAF) and P-D (unlocked `mDiagnostics` appends)**: these are
  data-race fixes; the load-bearing evidence is structural — the header's own #943 comment
  declares the reverted pattern a use-after-free, and `reload()`'s own comment declares the
  reverted append pattern a race. A deterministic interleaving oracle would require new
  test seams (an injectable drain blocker under `unloadAll`); the existing #1156/#928
  lock-drop suites cover the surrounding paths and pass. No TSan kill-proof is claimed.
- Oracle 2 is not run to completion against master (the defect IS the infinite hang); the
  RED demonstration is the `timeout 20` → exit 124 reproduction above.
- Oracle 9 deliberately CHANGES pinned behavior: the old test asserted the sidecar was
  "detectably absent" after rollback; hardening 15/20 restores the previous verified
  product+sidecar PAIR (crash invariant unchanged: missing, never stale).
