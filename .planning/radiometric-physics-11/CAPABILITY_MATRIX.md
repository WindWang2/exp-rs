# CAPABILITY_MATRIX — radiometric domain before/after

| Capability | Before (master a5b11b7f10) | After (this track) |
|---|---|---|
| DN→radiance/TOA/BT | implemented (`RadiometricCalibration`) | unchanged (PR #1008 owns typed seam) |
| State vocabulary + file metadata | implemented (ADR 0114) | unchanged |
| Transition legality + required-input binding | **not-supported** | **implemented** (`RadiometricTransition`, provenance record) |
| Sun position / earth-sun distance from time | **not-supported** (angles only from metadata) | **implemented** (`SolarGeometry`) |
| DOS/QUAC | implemented | unchanged; also exposed as built-in providers |
| LUT/6S provider seam | **not-supported** | **implemented** (`AtmosphericCorrectionProvider` registry; unregistered ids typed-refuse); concrete 6S LUT = follow-up (PR #1008 owns one) |
| Terrain illumination (Cosine/CC/Minnaert) | implemented | unchanged (consumes SolarGeometry when caller lacks metadata angles) |
| BRDF normalization | **not-supported** | **implemented** (Ross-Li kernel + empirical c-factor; angle-metadata preconditions) |
| Radiometric QA flags | **not-supported** (binary masks only) | **implemented** (`RadiometricQa` flag vocabulary + propagation + summary) |
| Operator surface for new modules | n/a | `rs:solar_geometry`, `rs:brdf_normalization`, `rs:radiometric_qa` |
| Known-answer tests (closed-form, independent oracle) | partial (calibration/topographic) | extended: solar geometry, BRDF kernels, transitions, provider refusal, QA flags |
