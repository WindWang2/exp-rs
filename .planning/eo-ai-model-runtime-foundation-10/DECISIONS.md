# DECISIONS — eo-ai-model-runtime-foundation-10

- D1 (slug): track slug `eo-ai-model-runtime-foundation-10` used verbatim for
  .planning dir, branch, worktree (prompt Track ID).
- D2 (scope): super-resolution + vision-language EO task families are NOT implemented
  as operators (no in-repo model class or scientific driver); recorded in
  CAPABILITY_MATRIX; the task vocabulary reserves their tokens so manifests can
  declare intent today and adapters can land later without schema break.
- D3 (F-OPS fixes): F-OPS-5/1/2 fixed in this track (in-ownership); F-OPS-3/4
  explicitly left to owning tracks (qa_mask / io families).
- D4 (instance segmentation): implemented as decode postprocess over segmentation
  probability/mask heads (connected components, per-instance class/area/confidence)
  + vector artifact — not a new runtime path.
- D5 (promptable): SAM-like geo-prompt lands as a documented contract on rs:segment
  (window hint parameter), not a new model class; full SAM promptable pipelines are
  a follow-up when an in-repo SAM graph exists.
- D6 (providers): TensorRT/OpenVINO adapters are add-on TUs guarded by CMake options;
  default build unchanged; absence yields typed refusal at provider resolution.
- D7 (resume): checkpoint sidecar opt-in via `tiling.resume`; fingerprint = (output
  path, plan geometry, package digest) so stale checkpoints can never resume a
  different plan.
