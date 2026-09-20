# ORACLES — flash-temporal-phenology-12

Objective completion conditions. Each maps to a reproducible check.

## O1 — Time-axis contract (WP1)

Irregular / duplicate / missing / cross-year / leap-year fixtures produce stable
results; malformed inputs fail with *typed* errors.

- [ ] Duplicate acquisitions on identical timestamps: deterministic documented
  policy, not UB.
- [ ] Missing/unparseable timestamp: typed error naming the offending item.
- [ ] Irregular cadence: processed on true time values.
- [ ] Leap-year + cross-year boundary fixtures: correct DOY math.
- [ ] Ambiguous filename-derived dates: rejected, never guessed.

## O2 — Synthetic seasonal recovery (WP3+WP4)

Synthetic series with known seasonal curve recovers expected peak / SOS / EOS
within stated tolerance vs an independent reference in the test.

- [ ] Harmonic fit on irregular timestamps returns expected amplitude/phase.
- [ ] Phenology on synthetic curve: SOS/EOS/peak within tolerance.
- [ ] Typed failure paths: too-few-samples / flat signal / double season.

## O3 — QA-mask correctness (WP2)

Changing QA mask changes interpolation correctly; observed vs interpolated vs
unavailable distinguishable; NoData never leaks.

- [ ] Per-output-sample provenance: observed | interpolated | unavailable.
- [ ] Flipping a mask bit flips provenance + value.
- [ ] NoData never yields finite output at masked positions.

## O4 — Streaming memory bound (WP7)

Peak memory scales with tile/chunk, not scenes x pixels.

- [ ] Evidence: same tile size over much larger scene does not grow peak RSS
  proportionally, or allocation sites asserted bounded by tile w*h*T.

## O5 — Suite stability

- [ ] `ctest -R temporal` + new targets pass **twice in a row**, offscreen, -j1.

## Gatekeeping

- `git diff --check` clean; no unrelated churn.
- Independent review P0/P1 = 0 before PR.
