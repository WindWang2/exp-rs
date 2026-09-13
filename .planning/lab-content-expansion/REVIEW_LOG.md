# REVIEW_LOG — lab-content-expansion

Phase 6 review not yet run. Format per finding: [P0|P1|P2] lens / file:line / issue / fix / status.

## Phase 6 review — lens 1: pedagogy + documentation consistency (read-only subagent)

Verdict: PASS, no P0. 1×P1, 5×P2. Full report archived in session log; remediation below.

- [P1] lab9 labspec operators digest `band:1` contradicted pipeline `band:0` (#803 step) → fixed to per-step digest with 0=全波段 note.
- [P2] lab8 intent T7 (summary count=12) missing from md/labspec expected results → added T7 row.
- [P2] lab8 R² vs r² wording normalized to r² (operator band name).
- [P2] lab10 PPI missing from 术语表 → 像元纯度指数 entry added (md + labspec).
- [P2] lab11 intent K2 typo 「植被害元占比」+ missing bracket → 植被像元占比 ∈ [25%, 55%].
- [P2] lab11 "MapSpec 3.0" note reworded (平台历史文档品牌名); md capability header + README now list cartography:validate/preflight; README headless claim scoped to data-chain with lab11 exception.
- Zero-drift md↔labspec independently verified for all four labs (counts + tolerances); H6 machine-verified.
- Disclosure: review agent's py_compile wrote untracked scripts/__pycache__/*.pyc (gitignored pattern *.pyc; no tracked file touched).

Remediation committed: (Phase 7 commit, see git log)

## Phase 6 review — lens 2: scientific correctness (read-only subagent)

Verdict: NO P0 physics errors — every operator-semantics claim verified against C++ source
(trend per-day units, phenology bands, anomaly baseline rules incl. target exclusion,
SAR σ0=(DN²−noise)/A², Lee b=max(0,1−Cu²/Cl²) mean-preservation, #785 heading±90/right=+90,
#803 per-band sentinel, MNF noise-whitening SNR order, PPI mt19937(42), SAM/SID semantics +
radian angleOut, unmixing LS+clip+renorm, library JSON format, MapSpec v5 governance, Otsu
formula, Erlang(4)/4 speckle). 6×P1 = quantitative calibration failures (intents were not
computed from the committed fixture model):

- [P1] T3 disturbance slope is −4.8e-4/day (not ≤−8e-4), r²≈0.036 (not ≥0.5)
- [P1] T5 z ≈ −1.56 with Jan–Aug baseline (season variance dilutes z)
- [P1] T6 control z ≈ +1.15 at seasonal peak (|z|≤1 wrong predicate)
- [P1] T4 SOS=DOY75 < 80, LOS=245 > 240 (no-interpolation crossing, thresholds)
- [P1] K2 Otsu cut ≈ −0.10 separates water → mask-above ≈ 90% (not 25–55%)
- [P1] H2 water endmember SAM angle ~4–16° (norm 0.066 vs σ 0.0042) → ≤5° fails

P2: platform calibration formula order (noise subtracted before scaling); 裸土 class absent
from fixture (dead code branch + unsatisfiable area fractions); quantization wording
(0.004 ≈ one 1/255 step); angleOut radians; phenology degenerate values not NoData.

Remediation (Phase 7):
- T5/T6 pipeline baseline narrowed to 2024-03-01..2024-08-31 (pre-disturbance growing
  season) → z ≈ −2.51 for disturbed target; control re-predicated as "no coherent negative
  anomaly" (mean ≥ −1.0), positive z documented as season effect.
- T3 recalibrated to slope ≤ −3.0e-4 + contrast ≥ 2e-4; r² gate replaced by explicit
  teaching note (seasonal-variance dominance = 思考题2 考点).
- T4 widened to [70,170]/[230,330]/[100,250]; amplitude < 0.15 + degenerate-value wording.
- K2 recast as vegetated-fraction ∈ [80%,95%] with threshold ≈ −0.1 (water vs vegetated)
  and explicit "二值=覆盖/非覆盖" semantics; K3 window [−0.3,0.3].
- H2 recalibrated to mean ≤ 10° / max ≤ 20° with SNR explanation.
- P2s: platform formula stated; generator soil branch removed + zone codes now 1..4 only;
  area fractions aligned (0.35/0.50/0.10/0.05); 量化步长 wording; angleOut radians noted;
  phenology degenerate-value wording everywhere.
- verify_lab_outputs.py updated to mirror all recalibrated tolerances.
