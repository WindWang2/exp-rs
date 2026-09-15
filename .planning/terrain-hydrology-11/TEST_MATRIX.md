# TEST_MATRIX — terrain-hydrology-11

Format: capability → independent oracle → command → exit → evidence.
`wt` = worktree root. All tests: `QT_QPA_PLATFORM=offscreen`, `ctest -j1` or
direct binary run. Oracles are closed-form values computed by hand/derived in
the test file — never by calling the implementation under test.

| # | Capability | Independent oracle | Command | Exit | Evidence |
|---|---|---|---|---|---|
| T1 | synthetic DEM factory | closed-form plane z=ax+by+c; cone z=h−k·r; pit/ridge/channel analytic | (used by all suites) | — | tests/synthetic_terrain_dem.h |
| T2 | epsilon flat resolution | flat plateau cell must gain direction≠0 after resolve; monotone non-decrease vs input; byte-equal fill when disabled | `ctest -R test_terrain_hydrology -j1` | pending | EVIDENCE P2 |
| T3 | D∞ direction | plane z=−x: D∞ = 270° (west); slope magnitude vs atan formula | same | pending | EVIDENCE P2 |
| T4 | D∞ accumulation mass | sum(acc) over valid cells = Σ self-inclusive paths = N + Σ(path lengths) identity computed independently by DFS in test | same | pending | EVIDENCE P2 |
| T5 | outlets detection | cone DEM: single outlet at cone base low point; pit DEM: pit cell | same | pending | EVIDENCE P2 |
| T6 | Strahler order | Y-network built by hand: main stem order 2, tributary order 1; order invariant at junctions | same | pending | EVIDENCE P2 |
| T7 | stream connectivity | every stream cell drains along-stream to exactly one outlet; segment count == source count | same | pending | EVIDENCE P2 |
| T8 | viewshed: plane | flat plane, observer h=2: all cells within radius visible, none beyond radius | `ctest -R test_terrain_viewshed -j1` | pending | EVIDENCE P3 |
| T9 | viewshed: ridge | synthetic ridge: far-side cells hidden, near-side visible | same | pending | EVIDENCE P3 |
| T10 | viewshed curvature/refraction | plane with curvature on: horizon distance shifts per 6/7-R formula computed by hand in test | same | pending | EVIDENCE P3 |
| T11 | horizon angles | cone: horizon angle at distance d equals closed-form atan((h_cone−z_obs)/d) | same | pending | EVIDENCE P3 |
| T12 | shadow duration | vertical pole on plane: shadow fraction == fraction of sun track below closed-form horizon | `ctest -R test_terrain_solar -j1` | pending | EVIDENCE P3 |
| T13 | sun position helper | tabulated values (equinox/ solstice, 3 latitudes) within declared ±0.3° (tolerance 0.5°) | same | pending | EVIDENCE P3 |
| T14 | multiscale TPI | plane → TPI≈0 all scales; ridge crest → negative-centered pattern inverted by hand | `ctest -R test_terrain_landform -j1` | pending | EVIDENCE P3 |
| T15 | geomorphon classes | hand-built patterns: peak/valley/ridge/slope/flat closed cases | same | pending | EVIDENCE P3 |
| T16 | operator E2E (new products) | registry run vs direct kernel on same synthetic file | `ctest -R test_terrain_analytics_e2e -j1` | pending | EVIDENCE P4 |
| T17 | cancel semantics | cancel flag set mid-run → Cancelled error, no output file | same | pending | EVIDENCE P4 |
| T18 | size guard | oversized DEM (tiny fake dims) → InvalidInputData fail-closed; env override documented | same | pending | EVIDENCE P4 |
| T19 | agent tools | JSON contract, no-crash on degenerate inputs, Unicode path | `ctest -R test_terrain_agent_tools -j1` | pending | EVIDENCE P4 |
| T20 | capability drift | sidecars match live descriptors after product additions | `ctest -R test_capability_knowledge -j1` | pending | EVIDENCE P4 |
| T21 | regression: legacy terrain | existing suites stay green | `ctest -R "test_terrain" -j1` | pending | EVIDENCE P8 |

Oracle 3 (large-DEM memory bound + cancel): covered by T17/T18 +
PERFORMANCE.md frame-count model; the env-opt-in scale test (large logical
grid through resolveFlats+D∞) runs only with SICNU_TERRAIN_SCALE_TESTS=ON.
