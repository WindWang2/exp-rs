# ADR 0178: One Canonical JSON Serializer — the Digest Authority (#1387)

Status: accepted · Branch `fix/issue-1387-digest-canonicalizer-convergence` · Baseline `origin/master@1e28de8677`

## Context

Every identity, digest, and content-addressed row in the platform hashes
*canonical JSON*. By #1387 there were three divergent canonicalizers plus
two non-canonical digest inputs:

- **A (the authority)** `sicnu::data::canonicalizeJsonRfc8785`
  (`src/data/execution_fingerprint.cpp`), a hand-rolled RFC 8785 serializer:
  UTF-16 key sort, shortest-round-trip doubles, `-0`→`0`. Serves execution
  fingerprints, experiment identity, capsules, dataset manifests, feature
  schemas.
- **B (teaching-admin)** `sicnu::teaching_admin::canonicalJsonBytes` +
  `sortKeys` (`src/teaching_admin/json_util.h`): Qt-Compact bytes with a
  caller-side recursive key sort. Qt's number formatting diverges from A,
  and the sort was the caller's job. Fed curriculum/labSpec/rules/pack
  digests, the batch config/report digests, and the release-report digest.
- **C (jsoncpp copy-paste family)** three near-identical
  `StreamWriterBuilder` locals: the harness projection digest
  (`provenance_projection.cpp`), the workflow-explain byte accounting
  (`workflow_explain.cpp`), and the cartography export-manifest digest
  (`export_manifest.cpp` — internally forked with `precision=17`). jsoncpp
  sorts by UTF-8 byte (≠ JCS UTF-16 for non-BMP keys) and formats numbers
  `%.17g`-style.
- **Non-canonical inputs**: the leakage-report digest
  (`dataset_store_splits.cpp`) hashed the Qt-Compact *stored text* while
  split manifests in the SAME file hashed through A; the label-schema digest
  (`dataset_store_samples.cpp`) hashed the stored text bytes, binding the
  digest to whichever serializer wrote the row.

Equal content therefore hadhed to different SHA-256 values depending on the
path — the exact failure mode a single canonical serializer exists to
prevent.

## Decision

**A is the only canonical serializer. Every fork converges onto it.**

1. A gains a jsoncpp adapter overload, `canonicalizeJsonRfc8785(const
   Json::Value&)` (`src/data/execution_fingerprint.h/.cpp`): a recursive
   read-only conversion (jsoncpp's `Json::Value` inspection API is
   header-only; the link is `PRIVATE jsoncpp` so the data layer's public
   interface stays Qt-only). Integers ride through `QJsonValue`'s double —
   exact below 2^53, the documented caveat — and key order is re-established
   by the authority, so the jsoncpp path inherits A's UTF-16 ordering.
2. Family C's three locals are deleted; all call sites delegate to A
   (projection digest, sidecar bytes, explain byte accounting, manifest
   digest + recompute check + written sidecar).
3. B's `canonicalJsonBytes` delegates to A. `sortKeys()` remains, but only
   for deterministic *output documents* — never for digest bytes.
4. The leakage-report and label-schema digests canonicalize the parsed
   document through A. Stored row text stays Compact (stored text is not the
   digest input).
5. `tests/test_canonical_digest_parity.cpp` locks the convergence: one fixed
   document (scrambled insertion order, integer doubles, `-0`, exponent
   forms, escapes, non-BMP strings, and two keys whose UTF-16 order is the
   reverse of jsoncpp's UTF-8 order) must digest to one golden hex through
   A, B, and C.

Deliberately left alone (self-aware, documented leaf forks with their own
contracts): `sicnu::grader::canonicalizeJson` (independent RFC 8785 port)
and `agentbench::deterministicSerialize` (explicitly not RFC 8785). A fourth
jsoncpp StreamWriterBuilder local survives in
`workflow_planner.cpp` (writes the engine document that CARRIES the
projection digest; not itself a digest input) — out of this track's scope.

## Consequences

- **Digest VALUES change once** wherever a document carries a value whose
  serialization differed. Measured on the forked paths: the export manifest's
  `dpi` double (jsoncpp `300.0` → authority `300`), and the jsoncpp family's
  `%.17g` doubles (`0.3` → `0.29999999999999999`, `-0.0`, `9.99…e-08` — now
  the authority's `0.3`, `0`, `1e-07`). The teaching-admin path was already
  byte-equivalent to the authority on the current Qt (QJsonObject sorts
  keys; Qt's double format matches); what that fork removed is the SECOND
  implementation, not the values. No fixture or golden file pins a literal
  digest from these paths (verified: the affected tests assert
  determinism/equality, verification round-trips, and sizes — not literal
  hex), so the only new pin is the parity lock's golden. Stored rows
  written before the change keep their old digests.
- `leakage_reports` is append-only keyed by `report_digest`: an identical
  re-save across the change inserts one new row beside the old (latest-wins
  reads see identical content; no reader keys content by this digest).
- jsoncpp's UTF-8 member ordering no longer leaks into any digest: the
  adapter erases it before A sorts.
- `sicnu_teaching_admin` now links `sicnu_data` (PUBLIC) — the one new
  cross-layer edge, required by the delegation. No cycle (the data layer
  references nothing in teaching).
