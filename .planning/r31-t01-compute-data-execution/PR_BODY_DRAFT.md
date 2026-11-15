# PR body skeleton (R31-T01) — fill verification numbers after the test run

## Problem and impact

(To be written per WP after verification. Candidate framing:)

- WP02: TaskCenter mapped arbitrary parameter strings to output assets; value-only
  reports (inspect/doctor/subdatasets/verify) could enter commit/cache/autoLoad and
  lose their identity when several explicit outputs existed.
- WP03: IO report tools had output shapes that let metadata/report fields flow into
  output publication.
- WP04: `OutputCommitter::reconcilePublished` returned the existing Asset ID for any
  registered path, so a cache hit could hand back a record an external-content event
  had left non-Ready (deleted then restored) — the hit never converged to the usable
  state a full run produces; a kind-changed path could be silently reused; a missing
  derivation record was never repaired; a replaced artifact with the same shape could
  pass as an unchanged hit.
- WP05: resume stored the checkpoint path verbatim, so a CWD change after start could
  persist to a different file.
- WP07: direct VRT wrote the output fill as the SOURCE NoData and warp set both
  source and destination NoData to the fill, so a source's own NoData semantics were
  lost and valid values equal to the fill could not be distinguished.
- WP08: warp intermediate reuse checked only size/CRS/partial transform, so a
  same-grid but different-band/dtype/NoData/content intermediate was reused.
- WP11: the ROI candidate budget was checked after the owner hash and the full
  candidate set were materialized.
- WP15: directed unload matched the identity by the text after the last `|`, which
  never matched load-contracted cache keys.
- WP16: the digest memo could store a fresh sample under another snapshot's digest
  (a mixed entry later probes trust) and pair a digest with a sample read across a
  same-state rewrite.
- WP17: the VRAM batch gate and the plan's working-set estimate used fixed input-
  channel multiples and did not scale with the declared output classes, while the
  TaskCenter RAM admission already did.

## Baseline

Base: `85ce917bea0ece4df6548a677b97a72ecde8659b` (origin/master at execution start).
Head: <branch HEAD at PR time>.

## What this PR changes

(Per-WP summary with file paths after verification.)

## Boundaries

- No GUI, Agent, Pi, or hosted-Python changes. `python_environment.*`,
  `python_script_operator.*`, `src/sdk/exprs/external_process.*`, `src/agent/runtime/**`
  untouched. #2061's active-exclude business paths untouched.

## Verification

(Commands, exit codes, CTest case names and counts, ENV_LIMITED items.)

## Review

(R1/R2 independent review SHAs and conclusions.)
