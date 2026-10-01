# R6 EVIDENCE

## Authority

`src/platform/durable_sidecar.{h,cpp}` — `sicnu::platform::sidecar`
- write(): prepare(validate) → staged O_EXCL temp (same dir, `#1097` shape) →
  write → fsync/FlushFileBuffers gate → atomic publish (POSIX rename(2) /
  Windows ReplaceFileW | MoveFileExW-WRITE_THROUGH, READONLY cleared) →
  best-effort dir fsync → last-good rotation (same path) → verify read-back
  (in-loop size cap enforced).
- read(): main → last-good → Missing/Corrupt with per-source rejection detail;
  content-agnostic (schema gates stay with callers, fail-closed).
- Fault seams: WriteHooks/ReadHooks named phases + payloadOverride; every fault
  routes through the REAL failure branch (D2: registry-free so the plugin SDK
  can link the static platform leaf without a runtime DLL).
- Status vocabulary shared via writeStatusName().

## Deterministic regression suite

`tests/test_platform_durable_sidecar.cpp` — 14 cases (light lane, Catch2 +
sicnu_platform only). Post-remediation runs: ALL PASS ×3 consecutive
(67–71 assertions; the concurrent case's per-error REQUIRE count varies with
Windows ReplaceFileW same-target transients — every observed failure is typed
`publish failed`, invariants unconditional: zero torn reads, complete final
artifact, zero temp residue).

FINAL double-run (post final-review remediation, passes 3 & 4, identical):
test_portability_source_contract 76/9 ×2 PASS; platform_durable_sidecar 68-69/14
×2 PASS; mission_runtime_store 98/10 ×2; workflow_checkpoint_cache 1093-1257/45
×2; fault_registry 31/9 ×2; class_table_widget 58/11 ×2; exprs_plugin_loader
415/35 ×2; mlops9_evidence (bundle) 98/4 ×2; data_platform_surface (#1405) 154/9
×2; study_e2e EXIT 0 ×2. Documented environment-native (pre-existing, code
byte-identical to master): test_mission_context 10/11 (RO-dir POSIX semantics
cannot be exercised on Windows), test_harness_evidence 7/9 (GDAL GTiff fixture
verdict variance). test_cli_batch_manifest = POSIX-only lane (popen fixture).

Crash-window matrix covered: WriteTemp/Durability/Publish/LastGood/Verify
faults, torn-write payload override (verify gate catches), validation refusals
(EmptyPath/TooLarge), read resilience (Main/LastGood/Missing/Corrupt, faulted
main, oversize fallthrough, no-suffix variant), inert temp residue, Unicode
paths, 4-writer concurrency with continuous reader, Windows locked target
(fail-closed, old generation intact), Windows READONLY attr cleared, POSIX
read-only directory claim failure.

## WP-H fault matrix → seam mapping

| Required fault | Deterministic seam |
|---|---|
| write failure | WriteHooks failAt(WriteTemp) + short-write branch |
| rename failure | failAt(Publish) + Windows locked-target test (real ReplaceFileW failure) |
| permission failure | POSIX chmod 0500 dir claim failure; Windows READONLY/locked |
| disk-full simulation | payloadOverride torn write (short write family) + TooLarge pre-refusal |
| malformed manifest / digest mismatch | #1403 test_io_metadata_patch + #1406 bundle checksum suites (unchanged, green) |
| newer schema | classification version gate test (new); mission exact-match refusal (existing); store read-only gate #1390 (existing) |
| concurrent write | 4×12-writer test + continuous share-delete reader |
| process interruption | phase-fault × post-state equivalence (DECISIONS D6) + existing workflow QProcess/SIGKILL crash injector (R4, untouched) |
| rollback failure | LastGood fault (rotation failure never fails committed main); committer !publishOk rollback preserved |
| verify failure | injected Verify fault + real mismatch via payload override |

## WP-I performance notes

- Verify read-back: one extra small-file read per save (sidecars are KB-scale);
  disabled for large-payload lanes (knob) — no caller here needed it off except
  the concurrency test's last-write-wins semantics.
- Last-good rotation: one extra small-file write per save on classification and
  mission authority saves (existed before with QSaveFile; now fsynced).
- No new digests (digest-authority fork is #1387, out of scope); no new SQL; the
  workflow temp sweep addition runs on the recovery path only (not hot); the
  1 MiB→64 MiB journal write cap bounds the audit trail without re-serializing.
- mission authority stays Compact JSON (#1170); no double serialization (the
  authority takes bytes once).

## Migration ledger (writer → authority)

classification saveClassMetaData/loadClassMetaData (+version gate, +last-good);
mission_context_store save; mission_runtime_store writeBytesAtomically
(last-good rotation); plugin_registry saveUserIndex; plugin_discovery storeIndex;
cli_batch_runner writeResultIndex; cli lab_batch_runner atomicWrite;
workflow_checkpoint saveCheckpoint (fault names preserved);
harness evidence atomicWrite; harness provenance_projection writeCompileSidecar;
harness context_checkpoint saveSession; agent_loop session_journal writeFileAtomic;
workflow_runtime provenance sidecar; experiment reproduction_bundle members +
checksums.txt (#1386 ordering kept); experiment capsule export; study report;
experiment_studio export; registration quality report; teaching_admin batch
checkpoint + JSON/CSV reports.
EXEMPT: range_cache putBlock (cache-only), atomic_fs dataset groups,
mission-runtime-gate legacy-sidecar test fixtures (QFile fixture, not production).

## Remaining QSaveFile in sicnu-owned src

ZERO active writers (grep audit post-wave-2; only comments/mentions remain).

## WP-G surface consistency map (durable semantics per surface)

| Surface | Writers on the path | Post-R6 semantics |
|---|---|---|
| App/UI (mission workbench, classification, teaching dock) | mission_context_store, mission_runtime_store last-good, saveClassMetaData, teaching batch artifacts | authority write: temp+fsync+publish; mission + classification additionally rotate .last-good; classification decode-gated |
| CLI | cli_batch_runner result index, lab_batch_runner reports | authority write, no last-good (re-derivable artifacts), typed log callback |
| Agent / MCP / harness | evidence sidecars, compile projection, session checkpoints, session journal | authority write; uncertainty/provenance keep never-overwrite guard |
| Runtime / workflow | workflow checkpoints, provenance sidecar | authority write; fault-point names + promotion/lock/archive semantics preserved |
| Processing | output_committer pair publish, registration reports | same-dir staging + fsync + atomic rename; #617 backup/rollback intact |
| Chunk runtime (pre-existing authority-grade) | scratch_registry, tile_checkpoint, commit journal, published marker | unchanged (fsync_compat + journal = truth); outside sidecar scope |
| Stores (sqlite) | dataset/experiment/governance/artifact | #1405 baseline; WAL+busy_timeout+schema-gate audited |

Interfaces keep their native forms (QString error codes, Result<T>, typed
statuses, qWarning best-effort) — the DURABILITY semantics (temp + fsync +
atomic publish + typed failure, never a torn artifact) are now identical.
