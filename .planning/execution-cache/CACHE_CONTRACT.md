# Execution Cache Correctness Contract (#726)

Status: implemented by `zcode/execution-cache-correctness`.
Governing rule: **宁可 miss，也绝不能返回错误科学结果** (prefer a miss over any
possible wrong scientific result). The cache stays OFF by default
(`SICNU_EXECUTION_CACHE`); nothing in this work changes the default.

## C1. Fingerprint is semantically injective

`ExecutionFingerprint =`
  `algorithm identity`
+ `implementation/version identity` (schema hash + execution-cache contract
  version + platform version — never timestamps, randomness, or build paths)
+ `semantic parameters` (all parameter keys/values EXCEPT keys whose name is
  platform output vocabulary: exact `output`/case-variants, or keys containing
  `output`/`result` case-insensitively — the same vocabulary
  `findOutputPathInParams` uses to *find* a destination)
+ `semantic input identities/revisions` (every input resolves to either a
  registered asset `(AssetId, revision)`, a workspace temporal collection
  record `(CollectionId, record revision)` with live scene revisions, or an
  in-pipeline producer's own execution fingerprint — "chained identity")
+ `input/output port wiring` (chained inputs carry the consuming port name;
  inputs are order-normalized).

* The destination path is excluded **by parameter-key semantics**, never by
  string-value equality. A non-output key that merely *carries* the destination
  value (e.g. a scratch param) stays hashed.
  Therefore `{input:x, output:x}` and `{input:y, output:y}` can never collide,
  and `{output:O, scratch:O}` ≠ `{output:O}`.
* The destination value under a NON-output key is not erased from input
  identity either: an in-place run `{input:x, output:x}` fingerprints with
  `x@currentRevision`, so re-registering new bytes at `x` invalidates it.

## C2. Any unidentifiable semantic input ⇒ not cacheable

`SourceIdentity` classes (classification reused from
`QgsDataSourceResolver`, not a new `startsWith("http")` rule):

| Class | Identity source | Unresolvable ⇒ |
|---|---|---|
| Local file | registered asset `(id, revision)` via canonical path | **cache disabled for that execution** |
| Remote / VSI (`http(s)`, `/vsicurl/`, `/vsis3/`, `/vsigs/`, `/vsiaz/`, OGR connection strings) | registered asset via the datasource's registered canonical source | **cache disabled** — never silently omitted |
| Workspace temporal collection | collection record revision + live scene revisions | **cache disabled** |
| In-pipeline producer output | producer step's execution fingerprint | **cache disabled** when the producer has no valid fingerprint |
| Placeholder reference (`$step.port`) | statically resolved + chained producer identity; **verified at dispatch** against the actually-substituted params (divergence ⇒ cache disabled) | **cache disabled** |

There is no code path that drops an input from the fingerprint and still
allows a hit. `QFileInfo(...).isFile() == false` no longer means "not an
input"; it means "resolve through the catalog or refuse to cache".

## C3. Chained identity & thread affinity

Fingerprints are computed **once, at submission/enqueue time**, on the
submitting thread (where the DataManager affinity already holds). Pipeline
steps are fingerprinted in topological order; a step that consumes
`$upstream.output` hashes the upstream step's *execution fingerprint* as the
input identity instead of the file's catalog revision. Consequences:

* Downstream steps admitted on JobEngine **worker** threads never need the
  catalog — no cross-thread DataManager access, no event-loop dependency,
  no marshaling, no deadlock.
* A cold A→B→C pipeline acquires a usable cache identity for **every**
  deterministic step during its **first** full execution (no N-run warm-up).
* Soundness: a producer is fingerprinted only behind the determinism gate
  (`deterministic` metadata or `determinismGrade() == "bit-exact"`), so
  identical fingerprint ⇒ byte-identical output ⇒ the chained consumer's
  input identity is exactly as strong as a revision stamp.
* At dispatch, the stored fingerprint's parameter snapshot is verified against
  the final (post-substitution) parameters; any divergence drops the
  fingerprint (conservative miss).

## C4. Repeated publication does not fabricate revision change

Re-registering an artifact whose producing execution is *the same execution
identity* (same `executionFingerprint`) and whose structure snapshot is
unchanged is a silent reuse: no revision bump, no `assetChanged`. A real
overwrite (different fingerprint, or changed structure) still bumps. This
keeps `A(cache-hit) → a.tif@k → B` stable across submissions so chained and
revision-keyed consumers converge to hits.

## C5. Cache and ArtifactGC share one lifecycle

`ExecutionResultCache` publishes its live artifact set through
`ArtifactGC::installProtectedArtifactProvider` (installed by TaskCenter).
`ArtifactGC::inspectReapable` refuses to reap any file whose canonical path is
cache-protected. Protection ends when the entry is invalidated, evicted,
self-healed away (missing file), or the cache is cleared — GC never leaks a
file forever, and a cache-served intermediate outlives run finalization.

## C6. Cache payload is the unit of reuse

A cache entry stores the **execution result**: the declared output path, every
produced artifact referenced by the result payload (`output`, `outputs[]`,
multi-output/grouped shapes), and the full JSON result payload. A hit restores
exactly what the producing run returned — `resultPayload`, `output`,
`outputs[]` — with paths rewritten to the serving run's destination, so GUI
auto-load, agents, and workflow placeholder resolution cannot tell a hit from
a real run. Grouped `rs:temporal_composite` (`period != all`) hits restore all
period rasters, not a bare path.

## C7. Serving is transactional

Materialization copies each cached artifact to a same-directory temporary and
atomically renames it onto the destination (never naked `remove + copy`);
stale sidecar files at the destination are refreshed/removed with the same
sidecar vocabulary ArtifactGC uses. Any materialization failure aborts the
serve and falls through to a real execution. A cache entry validates its
artifact set (existence + size + mtime of the declared output) before it is
served, and storing a new fingerprint's claim on a path evicts other
fingerprints' claims on that path — a destination rewritten by a different
execution can never vouch for the old one.

## C8. Residual risks (documented, not silently accepted)

* The mtime+size serve validation has a theoretical same-size-same-timestamp
  external-rewrite window; ownership eviction (C7) closes the realistic path,
  and the serve re-stats every staged source before renaming (abort ⇒ miss).
* A downstream fingerprint hashes the statically-resolved intermediate path,
  so relocating a producer's destination costs the downstream step its hit
  (miss-only, never wrong data).
* Sidecar files transferred on serve are not stat-validated (cosmetic
  metadata, not raster bytes); `.provenance.json` sidecars are outside the
  shared sidecar vocabulary (widening it would alter GC sweep behavior).
* enqueueTask-chained children whose producer task already completed are
  uncacheable — the producer fingerprint is consumed at the producer's
  terminal transition. Pipeline submissions (the #726 scope) fingerprint in
  topological order and are unaffected.
* The output-key vocabulary (contains "output"/"result") is contract-coupled:
  a deterministic operator must not carry a scientific parameter whose NAME
  contains those substrings (e.g. `outputMode`), or the parameter would be
  hashed out of the identity. Schema-declared output ports are the long-term
  refinement.
* Multi-port placeholder divergence (a port whose payload path differs from
  the producer's declared output) fails verification ⇒ conservative miss.
* A deterministic operator with a colon-bearing scientific value that matches
  the hidden-datasource shape (identifier prefix + colon, e.g. a CRS
  authority string) stays uncacheable — shape cannot distinguish it from a
  GDAL subdataset string.
