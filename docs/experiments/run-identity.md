# Run identity: three hashes, three meanings

- `run_config_hash` - SHA-256 over the canonical (RFC 8785-style) parameter
  document. `{"a":1,"b":2}` and `{"b":2,"a":1}` collide by design.
- `runExecutionFingerprint` - adds algorithm identity+version, dataset
  version+fingerprint, split manifest+fingerprint, model digest, seed.
  Equal fingerprints mean the same execution meaning.
- `resultFingerprint` - SHA-256 over artifact digests + metrics: what came
  out (duplicate-result detection input).

All three reuse the platform canonicalizer
(`sicnu::data::canonicalizeJsonRfc8785`) and are pinned by tests never to
be conflated.

Seeds and determinism: every run carries a seed and a
`DeterminismGrade` (strict | best_effort | non_deterministic). Non-strict
runs must say why (`determinism_note`), per goal section 30 / ADR 0124.
