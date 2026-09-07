# ADR 0137: Experiment/Run Identity, Metrics & Comparison

- Status: Accepted (2026-09-07)
- Scope: `src/experiment` (new `sicnu_experiment` library), metric contracts
- Depends on: ADR 0134–0136, ADR 0062 (unified execution seam), ADR 0123/0124 (workflow v2, determinism grades), ADR 0132 (unified task result surface)
- Ownership: Track D

## Context

Governance has `ExperimentRecord` (objective + variant list + run ids) and
`RunRecord` (workflow mirror), but nothing binds a run to the dataset version,
split manifest, model digest, canonical parameters, seed and environment it
actually used. Metrics are free `QJsonObject`s on `ResultRecord`. "Are runs A
and B comparable?" is therefore unanswerable, and metric numbers from
different protocols are visually identical.

## Decisions

1. **Experiment vs Run.** `Experiment` = one research question / comparison
   family (owns variants and member runs). `ExperimentRun` = one concrete
   execution binding: dataset version id + fingerprint, split manifest id +
   fingerprint, operator/workflow id, model identity (`id@version` + content
   digest, reusing the model catalog's anchor), canonical parameters, seed,
   software revision (platform version + plugin manifest versions),
   `RunEnvironment`, inputs, outputs, metrics, artifacts, status lifecycle
   (`Created→Running→Completed|Failed|Cancelled`, truthful states only —
   ADR 0130's no-fake-success rule applies verbatim).
   - Rejected: making ExperimentRun a wrapper that executes anything. Runs
     record executions performed through TaskCenter/JobEngine/workflow — the
     experiment layer is plan/identity/bookkeeping/comparison only. There is
     **no second scheduler**.

2. **Config canonicalization.** `run_config_hash` = SHA-256 over
   `canonicalizeJsonRfc8785` of the parameter object — key order and
   insignificant formatting never split identity. Distinct from
   `execution_fingerprint` (reuses `makeExecutionFingerprintV2`: adds
   algorithm identity + input revisions) and `result_fingerprint` (hash of
   the run's output artifact digests + metrics). Three hashes, three
   meanings, documented and tested.
   - Rejected: hashing raw JSON strings. `{"a":1,"b":2}` vs `{"b":2,"a":1}`
     are the same configuration and must collide.

3. **Typed metrics + EvaluationProtocol.** Metric structs:
   `ConfusionMatrix` (k×k counts + label order) with derived overall/balanced
   accuracy, per-class and macro/micro/weighted precision/recall/F1, IoU,
   kappa, MCC; segmentation pixel accuracy/mIoU/Dice/boundary F-score;
   regression MAE/MSE/RMSE/R²/bias (MAPE only where mathematically valid —
   zero-denominator policy explicit); detection AP/mAP via IoU matching
   (consumes existing detection decode output; no new detection runtime).
   Every metric set is persisted WITH its `EvaluationProtocol`: dataset
   version, split, subset, mask, ignore labels, thresholds, matching policy,
   aggregation/weighting, confidence. Same model + different protocol =
   different, non-comparable metric records.
   - Rejected: one `QVariantMap`-of-numbers metric bag. Untyped metrics are
     how kappa ends up silently averaged with mIoU.

4. **Comparison is comparability-first.** `RunComparison` diffs, in order:
   dataset version (id+fingerprint), split manifest, model identity/digest,
   operator/workflow identity, canonical config, seed, environment — and
   only then metrics and artifacts. The verdict is typed
   (`Comparable|ComparableWithDifferences|NotComparable` + reason list), so
   the answer to "is this a fair comparison" is data, not vibes.

5. **Storage.** `ExperimentStore` (same SQLite playbook: WAL, schema
   version, paged, checked writes) for experiments, runs, metrics,
   artifacts, environments. Asset/run/model references are loose string ids
   (the artifact and workflow stores stay authoritative); lineage joins are
   queries, not foreign keys across stores.

## Compatibility

- Governance `ExperimentRecord`/`RunRecord` unchanged; a bridging sync can
  come later and is out of scope. TaskCenter results keep flowing as today;
  runs may reference task ids / workflow run ids.
- No changes to existing operator result payloads; typed metrics can be
  produced FROM them by adapters.

## Resources

- Metric computation is single-pass over counts arrays (confusion matrix) or
  sorted per-match lists (AP) — bounded by prediction count, streamed.
- Run listings are paged; metric blobs load on demand.
