# PERFORMANCE — radiometric-physics-11

Resource model (logical, not wall-clock):

- `SolarGeometry`: O(1) per call; no buffers.
- `RadiometricTransition`: O(#edges ≤ 3) per band; JSON record O(#steps).
- `AtmosphericProvider`: O(count) per band kernel (DOS family), QUAC O(bands·pixels·sort)
  inherited from the house kernel; registry O(#providers) lookups under mutex.
- `BrdfNormalization`: O(1) kernels (≈ 20 trig calls); streaming operator memory =
  2×(256×256×4 B) tiles + O(bands) accumulators. Single-threaded fixed order (bit-exact grade).
- `RadiometricQa`: O(count) single pass per band; streaming operator memory ≈ float tile +
  uint16 flag tile + float/uint8 mask tiles (256×256). NoData/sentinel normalization through
  NaN domain, never clamping.
- Empirical c-factor accumulation: O(pairs) sums in double, O(1) state — no pair storage.

Hard limits honored: build -j2 (fallback -j1), tests -j1, CTEST_PARALLEL_LEVEL=1,
QT_QPA_PLATFORM=offscreen. Measured host sampling during the full build: 16-core host,
load ≤ 14.7 (< 1.5×16 threshold), cc1plus RSS ≈ 0.6–1.0 GB each — well under the 70% RSS
trigger; `-j2` kept throughout (see EVIDENCE.md).
