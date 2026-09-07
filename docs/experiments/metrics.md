# Metrics & evaluation protocols

Formulas live exactly once (src/experiment/evaluation.cpp) and are pinned
by known-answer tests:

- `ConfusionMatrix` (labels = class CODES): overall/balanced accuracy,
  per-class precision/recall/F1/IoU, macro precision/recall/F1/IoU, micro
  (= accuracy, documented), weighted F1, Cohen kappa, Gorodkin multivariate
  MCC.
- Regression: MAE/MSE/RMSE/R2/bias; MAPE skips AND COUNTS zero-truth rows
  (never divides by zero); R2 on constant truth = 0 by policy.
- Segmentation: pixel metrics ride the confusion matrix; boundary F-score
  via tolerance-bucketed matching.
- Detection: per-class AP (all-point interpolation over the PR curve,
  confidence-sorted with deterministic tie-break) and mAP.

`EvaluationProtocol` binds every metric set: dataset version, split,
subset (train/validation/test/fold:n), ignore labels, mask ref, IoU +
confidence thresholds, aggregation. Different protocol = non-comparable
record - enforced by construction, not convention.
