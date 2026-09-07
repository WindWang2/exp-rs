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

## Final round — (pending: before PR)
