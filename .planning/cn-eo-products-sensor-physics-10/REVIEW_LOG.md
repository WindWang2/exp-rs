# REVIEW_LOG — cn-eo-products-sensor-physics-10

## Review 1 — architecture & science correctness (subagent #1, read-only, 2026-09-13)

Scope: full worktree diff vs origin/master (registry, generations, ImportPlan,
new families, discovery, integration). Verdict: architecture sound; one P0,
three P1, three P2, several P3. All findings verified against code by the main
agent before fixing.

| ID | Severity | Finding (file) | Disposition | Evidence |
| --- | --- | --- | --- | --- |
| R1-1 | P0 | executeCnProductImport built stacking inventory from requestedBands with sourceBand=request position → subset/reorder/unknown requests silently selected wrong TIFF bands; #676 check vacuous | FIXED | inventory now built from plan.bandNames (declared order=TIFF order), sourceBand=inventory position; requestedBands only name-select; unknown names fail closed via unresolvableBands |
| R1-2 | P1 | unknown-element diagnostics recorded depth-1 (root) instead of depth-2 (root children) → every parse reported its own root as unknown | FIXED | xmlPathStart now records depth-2 children into topLevelSeen; root only feeds rootElement; legacy fixture → 0 unknowns, future-root fixture → [newgenerationtag] |
| R1-3 | P1 | test_cn_products.cpp discoverProduct called with std::string (QString param) — target did not compile | FIXED | QString/QStringLiteral arguments |
| R1-4 | P1 | capability sidecars missing for 4 CN import operators (inherited from #956 for 3 of them); 115/115 pin fails | FIXED (in progress) | gen-meta + enrichment apply after build; pins updated 111→115 |
| R1-5 | P2 | calibration IO failure left half-scaled output with DN stamp | FIXED | output removed on GDALOpen failure and transform failure; throwIfCancelled added inside line loop |
| R1-6 | P2 | registry test overwrote same path (path-keyed cache serves stale parse) | FIXED | test split across two SICNU_DATA_DIR roots |
| R1-7 | P2 | GF-7 B1 midpoint 480 inconsistent with 0.45–0.52 µm midpoint semantics | FIXED | 485.0 both cameras |
| R1-8 | P3 | productTypeName(Cn) → "Unknown"; importedState chain lacked Cn | FIXED | case Cn → "CN"; L1 level → digital_number |
| R1-9 | P3 | GF7_ names outside FWD/BWD fell out of recognized set | FIXED | GF7 catch-all refusal added (after supported patterns) |
| R1-10 | P3 | dialog helpTool missing "cn" → generic help id | FIXED | cn_product_import mapping + dialog_help_catalog entry |
| R1-11 | P3 | progress regression 0.95→0.88; no cancel check in calibration loop | FIXED | renumbered 0.90/0.93; cancel check added |
| R1-12 | P3 | siblingImage/siblingRole untested | FIXED | calibration import test asserts siblingRole=pan + PAN1 image |
| R1-13 | P3 | bandSource wording drift (sensor_profile_layout vs band_role_table) | FIXED | discoverCn attribute uses band_role_table |
| R1-14 | P3 | asString() type-unsafety on registry fields; empty role bypasses role_reason | ACCEPTED (defect noted) | registry files are in-repo trusted data; fail-closed loader + tests pin schema; hardening deferred (recorded in PR follow-ups) |
| R1-15 | P3 | planCnProductImport double directory scan | ACCEPTED | bounded 512-entry scans, no correctness impact |
| R1-16 | P3 | BandFile.wavelengthNm carries midpoint (15 nm ambiguity vs center) | ACCEPTED | matches ADR 0146 behavior; ADR 0147 documents both fields; SRF consumers use the spectral store |

Subagent verified-clean list (ranges/wavelengths per family, pan_variant
links, legacy result-key compatibility, fail-closed env handling, dispatch
routing) recorded in agent report; main agent re-verified P0/P1 evidence in
code before applying fixes.

## Review 2 — adversarial test/contract review (subagent #2, read-only, 2026-09-13)

Scope: the 8 (final: 10) commits on the branch, six lenses. Verdict: "needs
fixes" — one P1, two P2, P3 follow-ups. All fixes verified by rebuild+rerun.

| ID | Severity | Finding (file) | Disposition | Evidence |
| --- | --- | --- | --- | --- |
| R2-F1 | P1 | empty requestedBands → calibration vacuously applicable (radiance stamped over DN, bandCount=0 vs full stack); reachable via worker's absent schema validation (bands:[1,2]) | FIXED | executeCnProductImport materializes plan.bandNames for an empty request; evaluateCalibration also guards !requestedBands.isEmpty() |
| R2-F2 | P2 | setRadiometricState failure after successful calibration left "radiance pixels + DN stamp" | FIXED | QFile::remove before throw |
| R2-F3 | P2 | gaofen/hj description() understated coverage (no GF-7 / HJ-2); generated help entries diverged from description() | FIXED | descriptions updated; operators.md entries aligned to description() verbatim |
| R2-F7 | P3 | dangling cb*→cbers.json route (file absent) | FIXED | route removed (CBERS support reintroduces it with the file) |
| R2-F8 | P3 | zy3_pan note over-claimed FWD/BWD (dedicated entries exist) | FIXED | note narrowed to TLC/NAD |
| R2-F9 | P3 | sibling scan: Unsorted + >64 xml nondeterminism; "SPAN" substring could read as PAN | FIXED | QDir::Name ordering; (^|[^A-Z])PAN([0-9]|$) boundary match |
| R2-F4/5 | P3 | plan vs result satellite precision; (n/a merged into F9/F14 sets) | LOGGED | follow-ups |
| R2-F6 | P3 | cnSensorKey swallows GeoError root cause on registry failure | LOGGED | follow-up: thread diagnostic into caller-visible warning |
| R2-F10 | P3 | read-only dir test is a no-op guard on Windows | LOGGED | follow-up (platform-semantics note); Linux-enforced |
| R2-F11 | P3 | HJ rpc_rpb constituent needs real-package confirmation | LOGGED | follow-up (verify against real HJ distribution; adjust registry) |
| R2-F12 | P3 | writeCnImportMetadata failure keeps the valid DN stack (inconsistent with removal policy) | ACCEPTED | keeping a valid, correctly-stamped DN file is defensible; documented |
| R2-F14 | P3 | fixture productId arg ignored; duplicate-scene comment overstates | FIXED (partial) | missing-bands assertion added (F14-c); fixture/comment cosmetics logged |

Reviewer verified clean: 58/58 registry band midpoints == arithmetic range
midpoints (script-checked); 9/9 pan/ms variant links resolve; 15/15 identity
sensor keys exist; concurrency of the path-keyed profile cache (node-stable
map, static storage, mutex discipline); stackedBands index alignment for
subset/reorder/duplicate requests; no path injection. Capability sidecars:
family=io, schema_version=2, zero drift vs operator metadata.
