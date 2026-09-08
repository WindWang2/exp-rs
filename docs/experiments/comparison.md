# Run comparison: comparability first

`RunComparison::compare(a, b)` diffs, in order: dataset (id+fingerprint),
split (id+fingerprint), model (digest), algorithm identity, canonical
config, seed, environment - and ONLY THEN do metrics matter.

Verdicts:
- `Comparable` - every pin identical;
- `ComparableWithDifferences` - same dataset/split/model, different
  config/seed/environment/algorithm (the legit scientific A/B case);
- `NotComparable` - dataset, split or model pin differs.

`metricDiff` reports per-metric a/b/delta for numeric leaves present in
both runs. The CLI surfaces all of it via `experiment compare --a --b`.
