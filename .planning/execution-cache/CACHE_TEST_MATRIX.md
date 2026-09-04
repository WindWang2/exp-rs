# Cache Test Matrix — #726

| # | Requirement (issue §11) | Case | Suite |
|---|---|---|---|
| I1 | in-place `x/x` vs `y/y` never collide | fingerprint digests differ for `{input:x,output:x}` vs `{input:y,output:y}` | `test_execution_fingerprint` |
| I2 | same destination, different input | digests differ; second run miss | fingerprint unit + e2e |
| I3 | same input, different scientific param | digests differ | fingerprint unit |
| I4 | destination under a non-output key stays hashed | `{output:O,scratch:O}` ≠ `{output:O}` ≠ `{output:O,scratch:P}` | fingerprint unit |
| I5 | in-place run keeps input revision identity | `{input:x,output:x}` identity contains x@rev; re-registration at x invalidates | e2e in-place case |
| I6 | chained producer identity | downstream digest tracks upstream fp; upstream fp change ⇒ downstream digest change | fingerprint unit |
| I7 | contract/impl version changes digest | contract version bump ⇒ different digest | fingerprint unit |
| R1 | remote asset revision enters identity | registered `/vsicurl/` asset identity = (id, rev); bump ⇒ different digest | fingerprint/workspace unit |
| R2 | unidentifiable remote input ⇒ uncacheable | unregistered `https://`, `/vsicurl/`, `/vsis3/`, `PG:` params all FAIL the fingerprint policy with a reason; unclassified subdataset syntax (`HDF5:"…"`) likewise; existing-file-but-unregistered local input ⇒ real execution, never a hit, nothing stored | `test_temporal_workspace` remote case + `test_workflow_cache_e2e` unidentifiable case |
| C1 | A→B convergence with per-run registration | run1 miss/miss; register; run2 hit(A)+hit(B); register; run3 hit/hit | `test_workflow_cache_e2e` (rewritten loop) |
| C2 | A→B→C cold single-run identity | one submission; every deterministic step stores a usable identity during run 1; second submission all-hit | e2e cold-chain case |
| C3 | param change invalidates only affected/downstream | kernel change: A hit, B miss | e2e (existing case, kept green under new registration loop) |
| G1 | cache-owned intermediate survives GC | run → store → sweep completed run → cached artifact exists; second run hits | `test_workflow_artifact_gc` / e2e |
| G2 | invalidate/clear releases protection | clear() → sweep reaps | artifact-gc case |
| T1 | worker-thread admission fingerprints | cold chained pipeline run 1 stores identities for downstream steps (the store happens from worker-thread completion) | e2e C2 asserts stores after run 1 |
| M1 | grouped temporal composite hit | period≠all run stores payload+period rasters; identical resubmission hits; restored payload's `outputs[]` == real-run `outputs[]` (modulo cache markers); all period files materialize | e2e grouped case (registry operator if available, else two-step shape via payload contract) |
| M2 | hit payload equals real-run payload | GUI `collectProducedOutputs` fields (`output`, `outputs[*].output`) restored | M1 assertions |
| S1 | serve is atomic; never leaves partial destination | materialization failure (unwritable dir) ⇒ fall through to real execution | e2e safety case |
| S2 | path reuse cannot vouch for old bytes | fp1→P stored; fp2 writes P; fp1 lookup ⇒ miss (ownership eviction); bytes rewritten in place with different size/mtime ⇒ miss; chained INPUT rewritten out-of-band ⇒ consumer miss | fingerprint-cache unit |
| S3 | missing cached artifact ⇒ self-heal miss | delete declared output ⇒ lookup erases ⇒ miss | existing self-heal case, kept green |
| S4 | hit never overwrites an undeclared file | run with destination O2: unrelated sibling file untouched | e2e destination case |
| D1 | same-fp re-publication does not bump | registerSource(fp) ×2 ⇒ revision stable, no assetChanged | `test_data_manager` |
| D2 | different-fp re-publication bumps | fp' ⇒ revision+1 + assetChanged | data-manager case |
| D3 | structure change bumps despite same fp | structure differs ⇒ bump | data-manager case |
