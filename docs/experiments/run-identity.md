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

## Repeat-execution classification (12.0)

`RepeatExecutionClassifier::classify(identity, resultFingerprint,
executionRef, repeatEnvironment)` answers "did this execution already
happen?" at store scale via the indexed `execution_fingerprint` column:

- `new` — no recorded run shares the identity;
- `same_execution` — identity AND result fingerprint match (duplicate);
- `same_identity` — identity matches but no result evidence was supplied
  (duplicate-vs-rerun is not guessed);
- `equivalent_rerun` — identity matches, results differ; the verdict cites
  the matched run's declared determinism (a Strict-deterministic twin with
  differing results is drift evidence, not a benign rerun);
- `deviated` — the platform `executionRef` already recorded runs under
  different pins (the same execution silently re-run with changed inputs).

Environment drift between identity twins is REPORTED (`environment_drift`)
and never downgrades the verdict — environment is not an identity pin
(ADR 0137).
