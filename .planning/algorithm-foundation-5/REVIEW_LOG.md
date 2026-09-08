# Review Log — Foundation 5.0

Lenses (phased, main agent + ≤2 fixed subagents, no per-lens agents):
1. Scientific correctness 2. Numerical stability/edge cases
3. Architecture/duplication 4. Performance/memory/cancellation
5. API/schema/docs claims (+ cross-platform C++, test adequacy as extras)

Format per finding:

```
ID: R<n>-L<lens>-<seq>
Severity: P0|P1|P2|P3
Location: path:line
Evidence: ...
Fix/Accepted-debt: ...
Verification: ...
```

Rules: P0/P1 must be fixed before PR (verification = failing-then-passing
test or diff re-review). P2 fixed or documented debt. Re-review the diff
after each remediation round — one pass is never enough.

## Round 1 — implementation-time findings (main agent, continuous)

```
ID: R1-L1-001
Severity: P0 (baseline)
Location: src/workflow/workflow_run_coordinator.h/cpp @ 93a7fb0bbd
Evidence: master does not compile locally — the .cpp calls
          WorkflowRunCoordinator::notifyRunStateLocked(run, qint64, qint64)
          and emits runStateChanged(...) while the header declares neither
          (no notifyRunStateLocked declaration, no signals: section). The
          baseline build stopped at 2826/5001 (sicnu_task_center), hiding
          every downstream target.
Fix: added the private notifyRunStateLocked declaration (matching the
     *_Locked convention and issue #754 comment) and a signals: section
     declaring runStateChanged(runId, workflowId, state, startedMs,
     finishedMs). Minimal, additive; no behaviour change.
Verification: sicnu_task_center and dependent targets compile.

ID: R1-L8-001
Severity: P1 (portability)
Location: src/agent/harness/harness_verification.cpp:269 @ 93a7fb0bbd
Evidence: `fractions["sampled"] = total;` with qint64 (long long) is
          ambiguous for the local jsoncpp build (Int/UInt/Int64/double
          conversions); surfaced after the workflow fix unblocked the
          agent harness target.
Fix: static_cast<Json::Int64>(total) — explicit and portable.
Verification: harness target compiles.

ID: R1-L2-001
Severity: P1 (own code, caught by the certification tests)
Location: primitives/distance_transform.cpp (removed after fix)
Evidence: the second EDT pass seeded columns with f + y^2, corrupting
          source cells (distance 2.0 at a source pixel); caught by the
          closed-form 5x5 suite.
Fix: pass 2 consumes the row-transformed values as-is (standard F-H form).
Verification: test_primitives5 green (147 assertions).

ID: R1-L4-001
Severity: P1 (own code, caught by the E2E)
Location: rs_topographic_correction_operator.cpp readHaloWindow call
Evidence: the DEM core window was requested at (xOffset - halo, y - halo),
          shifting every illumination sample by (-1,-1) and clamping the
          far edges; the C-correction E2E regression decorrelated
          (fitted b ~= -0.006 against an exact a + b*cos_i scene).
Fix: request the core at the tile origin; the replicate rings cover the
     halo.
Verification: known-answer E2E green (fit recovers (a,b) to 1e-6; every
              valid pixel maps to a + b*cos(theta_z)).
```

## Audit outcomes folded into the capability matrix

- SID: already callable (rs:sam_classify metric=sid, hand-derived cases) —
  no new operator needed (C.1).
- MNF noise estimation: shift-difference method with the #700 row-wrap
  guard; contract sound (C.4).
- Unmixing: UCLS + unit-sum renormalization documented as an approximate
  fully-constrained mode (C.5).
- Spectral resampling: linear interpolation between band centers, #445
  sentinel contract (C.6).
- SAR domain contract (sigma0/gamma0/beta0 x linear/dB, 10*log10, output
  stamps) already covers the hardening list (D.1).
- Duplicate timestamps: enforced by the temporal collection machinery
  (twin exclusion); QA weighting stays best-pixel-only as documented
  (E.6/E.7).
- Max-likelihood == NormalBayes coverage; logistic regression and ISODATA
  documented as deferrals (H).

## Round 2 — (pending: after Milestones C–I)

## Final round — independent adversarial review (subagent B, read-only) + remediation

Reviewer scope: full branch diff vs 93a7fb0bbd, all five lenses, formulas
re-derived independently; dual-pol RVI cross-checked against the Copernicus
openEO / Sentinel Hub references. 10 findings, all verified real, all fixed
in this round (commit "fix(review)"): P0=0, P1=2, P2=3, P3=5 -> P0=0, P1=0,
P2=0, P3=0 (all fixed; none accepted as debt).

| ID | Sev | Summary | Fix verification |
|---|---|---|---|
| R-F1 | P1 | dual-pol RVI numerator inverted (4VV/(VV+VH); the S1 convention is 4VH/(VV+VH)) | kernel+header+operator text+tests updated; 0.4/0.1 -> 0.8, VH=0 -> 0 |
| R-F2 | P1 | hillshadeMultidirectional accumulated into a non-zeroed buffer reused across streaming tiles | std::fill at entry; tile-multiple DEMs now safe |
| R-F3 | P2 | sar_terrain_masks schema claimed a scene-metadata fallback that the code never reads | schema/header reworded to "required; no fallback consulted" |
| R-F4 | P2 | Minnaert with a degenerate fit silently applied k=1 | refusal extended to Minnaert (matches the declared kernel contract) |
| R-F5 | P2 | temporal_monitor working-set estimate ignored the per-tile scene stack | working set now includes 5 B/px/scene; tile size auto-clamped with a warning |
| R-F6 | P3 | profile-curvature citation: directional-derivative normalization, not ZT's (zx²+zy²)^1.5 | header states the Esri-style normalization explicitly |
| R-F7 | P3 | morphology erodeN/dilateN comment described a non-existent odd-iteration rejection | comment corrected (negative rejected) |
| R-F8 | P3 | histogram quantile doc overclaimed "p == 100 yields maxVal" | reworded (approaches maxVal, in-bin interpolation) |
| R-F9 | P3 | extrema/focal estimates scaled with a tile_size that run() ignored | runWindowOp now consumes the clamped tile_size |
| R-F10 | P3 | dual-pol span output domain stamped "dimensionless" for dB inputs | stamped linear_power for span in both input domains |

Reviewer-confirmed non-issues (no action): the ChangeDetection -> primitives
delegation is bit-identical to baseline; FH-EDT, Otsu tie averaging,
quantile interpolation, priority-flood, D8 codes, Kahn accumulation, Horn
gradients, illumination cosine, Minnaert/C fits, MF/ACE, seasonal-MK
variance/ties, Mahalanobis df all correct per their declared conventions;
both baseline repairs match the pre-existing .cpp usages.

Parallel-sweep note: 3 of 2581 ctest cases failed at -j4 —
ebench terrain_curvature (real, fixed above: bench DEM geotransform placed
the scene-centre latitude at -256 deg, so the #612 metres conversion
produced a negative cell size and the kernel correctly refused), plus two
timing-sensitive cases (crash-resume lineage #727, toolbar DnD empirical)
that pass consistently when run alone and are unrelated to this branch.
