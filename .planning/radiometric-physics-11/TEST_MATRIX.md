# TEST_MATRIX — F14 radiometric-physics-11

Every row maps one capability to an independent oracle and a command. Exits and
evidence lines are appended in EVIDENCE.md as suites actually run (Phase 5).

| Capability | Independent oracle | Test target / case |
|---|---|---|
| Earth-sun factor/distance | published perihelion/aphelion envelope (1.033..1.037 / 0.9645..0.9685) | test_solar_geometry: "earth-sun factor matches published perihelion/aphelion" |
| Declination series | solstice/equinox bounds ±0.6°; Cooper cross-formula < 1° | test_solar_geometry: "declination hits published seasonal extrema" / "agrees with Cooper" |
| Equation of time | published extrema +16.4/−14.2 min; ±17 min hard bound | test_solar_geometry: "equation of time matches published extrema" |
| Solar position identities | noon elevation = 90 − \|φ − δ\|; AM/PM mirror symmetry; London reference | test_solar_geometry: three identity/reference cases |
| Solar refusals | typed errors, no defaults | test_solar_geometry: validators + fail-closed refusals |
| rs:solar_geometry operator | JSON schema fields; SICNU_SUN_* metadata round-trip via GDAL | test_solar_geometry: E2E cases |
| Transition DAG | independent adjacency table in test | test_radiometric_transition: "lawful DAG matches" |
| Edge requirements | per-sensor coefficient shapes (Landsat/S2/generic) | test_radiometric_transition: requirement cases |
| Provenance record | structural checks (schema id, steps, scale round-trip) | test_radiometric_transition: provenance assertions |
| DOS1/DOS2 built-ins | hand-computed Chavez algebra in test | test_atmospheric_provider: known answers |
| Provider seam | unknown-id + missing-aux typed refusals; stub provider dispatch | test_atmospheric_provider: resolve/registry cases |
| QUAC adapter | parity with wrapped house kernel (science owned by test_atmospheric) | test_atmospheric_provider: adapter dispatch |
| Ross-Thick/Li-Sparse kernels | published special values (0/−1), reciprocity, hand-worked references | test_brdf_normalization: kernel cases |
| Kernel normalization | out = v·f(ref)/f(obs) recomputed independently in test | test_brdf_normalization: ratio semantics |
| Empirical c-factor | linear-pair identity c = a/b; mean-leveling identity | test_brdf_normalization: c-factor cases |
| rs:brdf_normalization operator | constant-scene E2E; angle-metadata refusal naming SICNU_SUN_ZENITH; state preservation | test_brdf_normalization: E2E cases |
| QA flag vocabulary | frozen bit values pinned in test | test_radiometric_qa: "flag vocabulary values are frozen" |
| QA evaluation/summary | hand-classified buffers; fractions with explicit denominators | test_radiometric_qa: evaluate/propagate cases |
| Uncertainty propagation | hand-computed σy = sqrt((g·σx)² + (x·σg)² + σb²) | test_radiometric_qa: linear uncertainty cases |
| rs:radiometric_qa operator | flag-band readback vs hand-placed anomalies; mask propagation; grid-mismatch refusal | test_radiometric_qa: E2E cases |

## Run commands (targeted, -j1)

```bash
ctest --test-dir build-dev -R "test_solar_geometry|test_radiometric_transition|test_atmospheric_provider|test_brdf_normalization|test_radiometric_qa" -j1 --output-on-failure
```
