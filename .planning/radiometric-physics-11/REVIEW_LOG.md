# REVIEW_LOG — F14 radiometric-physics-11

Format: `<reviewer> | <id> | severity | <finding> | <disposition> | <commit/test evidence>`

## Round 1 — independent science/API audit (read-only subagent #1, 2026-09-15)

Full-diff review `origin/master...HEAD` against the published literature
(Lucht/Schaaf/Strahler 2000, MODIS BRDF ATBD, Schaaf et al. 2002), the house
kernels and the test oracles. Sources cited in the review: Ren et al. 2015
(Sensors 15(4):7537, Li-Sparse-R equations), MODIS ATBD, MCD43 user guides.

- subagent#1 | P0-1 | P0 | Li-Sparse-Reciprocal third term written as
  `½(D + D′)`; canonical form is `½(1 + cos ξ)·secθs·secθv`, so k_geo(0,0) = 0
  (not −1) and the sign of the geometric term flips near hotspots |
  **FIXED** — kernel rewritten to the canonical form; header/tests/docs
  updated; reference values re-derived with an independent scripted
  implementation: k_geo(30,0,·) = −0.69822, k_geo(45,45,0) = +0.58579,
  k_geo(0,0) = 0. Note: the reviewer's own quoted value −0.96541 inherited a
  hand-arithmetic error from the original test comment (overlap O = 0.38007,
  not 0.11194); the scripted independent computation governs |
  test_brdf_normalization all green (49/49).
- subagent#1 | P1-1 | P1 | E₀ placed on the wrong side of the ESUN TOA
  formula in header/provenance-token/docs (`ρ = π·L·E₀/…` instead of
  `ρ = π·L·d²/(ESUN·cosθz) = π·L/(E₀·ESUN·cosθz)`) | **FIXED** in all four
  locations (solar_geometry.h, radiometric_transition.h/.cpp provenance
  token `rho = pi*L*d^2/…`, docs). No numeric path multiplied E₀ — strings
  only.
- subagent#1 | P1-2a | P1 | test_atmospheric_provider asserted NaN for a
  negative DOS1 result, contradicting the house kernel (finite pass-through)
  | **FIXED** — test now pins the finite −0.11 pass-through; provider header
  documents that negatives flow to RadiometricQa::FlagNegative instead of
  being NaN-ed.
- subagent#1 | P1-2b | P1 | solar longitude test had inverted inequality and
  a wrong 120° expectation (minutes-of-time vs degrees conflation) |
  **FIXED** — east site has the larger hour angle; difference is exactly 30°
  (production code was correct; test was wrong).
- subagent#1 | P1-2c | P1 | QA E2E expected FlagSaturated alone for a 2.5
  value with saturation level 2.0; the module composes
  Saturated|OverRange | **FIXED** expectation.
- subagent#1 | P1-2 (process) | P1 | suites had never been run when the
  audit landed | **ADDRESSED** — targeted gate now runs: 49/49 pass after
  remediation (first run surfaced 14 failures + 2 segfaults, all
  root-caused below).
- subagent#1 | P1-3 | P1 | QA operator silently no-ops QA_RADSAT when band
  is set but bit masks are omitted | **FIXED** — defaults to the Landsat
  convention (bit b−1 for band b, ≤16 bands) and records
  `qa_radsat_bits_applied` + `qa_radsat_bits_explicit` in the result.
- subagent#1 | P2-1 | P2 | Summary::fraction loops forever on FlagNone /
  multi-bit words | **FIXED** — guard returns 0.0 and documents the
  single-bit precondition.
- subagent#1 | P2-2 | P2 | BRDF operator treated explicit negative angles as
  "absent" and fell through to metadata | **FIXED** — any provided numeric
  value is used and the validators range-check it; metadata only consulted
  when the parameter is absent.
- subagent#1 | P2-3 | P2 | RAM estimates ignored the BIP band-count factor |
  **FIXED** — band-aware `estimateExecution(params)` override added to
  rs:brdf_normalization; nominal estimate updated to a 4-band-equivalent
  disclosure.
- subagent#1 | P2-4 | P2 | checkRequirements accepted out-of-range sun
  azimuth | **FIXED** — [0, 360) enforced for providers declaring
  needsSunGeometry.
- subagent#1 | P3-1 | P3 | below-horizon suns stamped though the limitation
  text claimed otherwise | **FIXED** wording — stamping is intentional for
  traceability; downstream validators refuse.
- subagent#1 | P3-2 | P3 | resolveWeights accepted non-numeric JSON entries
  as 0.0 | **FIXED** — per-entry isNumeric check with typed message.
- subagent#1 | P3-3 | P3 | qaWords float→uint16 cast had no range guard |
  **FIXED** — out-of-range/non-finite QA words claim no bits.
- subagent#1 | P3-4 | P3 | solar operator reported missing lat/lon as range
  errors | **FIXED** — hasNumber() pre-checks emit
  MissingRequiredParameter.

### Additional defects found while remediating (own findings)

- self | R1-a | P1 | `RadiometricTransition::plan` gated multi-edge chains
  (DN→BT, DN→SR, L→SR) on the *direct-edge* predicate, rendering lawful
  chains unlawful | **FIXED** — the chain table decides lawfulness;
  isLawfulEdge remains the public single-edge predicate |
  test_radiometric_transition green.
- self | R1-b | P1 | `registerProvider` took ownership; tests registered
  stack objects → dangling registry pointers (SEGFAULT in the first run) |
  **FIXED** — registry is non-owning with an explicit process-lifetime
  contract; built-ins are intentionally-leaked statics | segfaults gone.
- self | R1-c | P1 | empirical c-factor semantics were wrong as documented:
  `c = a/b` with `ρ₂' = c·ρ₂` does not level the means | **FIXED** —
  redefined as the mean-preserving factor `c = mean(y)/mean(x)`
  (exact identity mean(c·x) = mean(y)); validity conditions restated
  (min pairs, positive means); class renamed PairStatistics | green.
- self | R1-d | P2 | QA operator passed a uint8 buffer to readBandWindow
  (float contract) — buffer overrun | **FIXED** pre-run (commit 419f32c5d3);
  float mask tile + quantization.
- self | R1-e | P2 | rs_radiometric_qa wrote uint16 flags through
  writeTile(float*) — hard link error | **FIXED** — writeTileRaw(GDT_UInt16).
- self | R1-f | P3 | solar mirror-symmetry test asserted exact zenith
  equality across ±30° hour angle; the declination drift between the two
  instants (~0.05°) is real modeled physics | tolerance widened with an
  explanatory comment.

## Round 2 — final adversarial review (subagent #2)

(pending — dispatched after the Phase 5/6 gates)
