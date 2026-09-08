# Reproducibility

A run is replayable to the degree its pins resolve:

- `Exact` - dataset version available + fingerprint match, model digest
  match, algorithm available, workflow valid, artifacts present,
  environment identical, determinism strict;
- `Compatible` - pins hold, some checkable dimension differs within policy;
- `BestEffort` - replayable in principle, gaps recorded;
- `Impossible` - a required pin is missing or mismatched.

`ReproductionHooks` carry the availability checks the module cannot do
itself (model catalog, operator registry, workflow store, artifact
presence). Unwired hooks degrade to BestEffort - never a fake Exact.

Environment capture is ALLOWLISTED (platform, versions, locale, timezone,
named SICNU_* flags) with a denylist pass over names AND value shapes
(Bearer/Basic, PEM keys, AKIA, ghp_, xox, sk-). The filter runs again
on load and before bundle serialization (tested in
test_experiment_evaluation).
