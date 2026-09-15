# CAPABILITY_MATRIX — terrain-hydrology-11

Legend: ✅ implemented + local known-answer evidence; 🧪 implemented, evidence at
phase gate; ❌ not supported (documented); 🟡 degraded (documented).

| Capability | Before (master a5b11b7f10) | After (this track) |
|---|---|---|
| slope/aspect/hillshade (iso/aniso/multidirectional) | ✅ existing | unchanged |
| roughness/TRI/TPI(3×3)/curvature×3/local relief | ✅ existing | unchanged |
| depression fill (priority-flood, NoData barriers) | ✅ existing | unchanged (byte-compat) |
| **flat resolution** | ❌ documented debt (flats = sinks) | 🧪 `resolveFlats` epsilon variant; product `flat_resolve` |
| D8 flow directions / accumulation | ✅ existing | unchanged |
| **D∞ flow (Tarboton)** | ❌ | 🧪 `flowDirectionInf` + single-receiver accumulation |
| watershed labels (manual pour points) | ✅ existing | unchanged |
| **automatic outlet detection** | ❌ | 🧪 `detectOutlets` (sinks + rim outflow cells) |
| **stream network (threshold + Strahler)** | ❌ | 🧪 `streamNetwork` mask/order/segments + connectivity invariants |
| **stream/basin vectorization** | ❌ | 🧪 segment polylines in map coords (JSON artifact) |
| **viewshed (single observer)** | ❌ | 🧪 R3 sweep, observer/target height, radius, curvature/refraction optional |
| **multi-observer cumulative viewshed** | ❌ | 🧪 cumulative counts + per-observer binary viewsheds |
| **horizon angles** | ❌ | 🧪 HorizonGrid in N azimuth sectors |
| hillshade series | partial (single sun + multidirectional) | 🧪 multi-sun series (operator loop, multi-band output) |
| **shadow mask / shadow duration** | ❌ | 🧪 per sun track; low-precision sun position generator (declared ±0.3°) |
| multiscale TPI + Weiss landform classes | ❌ (TPI only 3×3) | 🧪 radius-based TPI scale set + 6-class product |
| **geomorphons** | ❌ | 🧪 10-class Jasiewicz–Stepinski with flat/flat-radius params |
| DEM inspect (agent) | partial (generic raster_inspect) | 🧪 `spatial:terrain_profile` |
| viewshed quick-check (agent) | ❌ | 🧪 `spatial:terrain_viewshed_inspect` |
| dialog: hydrology products | ❌ (6 local products only) | 🧪 flow/viewshed/solar/landform entries |
| large-DEM bounded path | 🟡 full-frame contract + dynamic estimate | 🟡 + explicit cell-cap guard + cancel polling; tiled external-memory flow = follow-up (documented) |
| unit/datum warnings | ❌ | 🧪 degree-CRS cellsize + vertical-datum provenance warnings in operator results |

Explicit non-goals (stay ❌, documented): fraction-weighted D∞ accumulation
splitting, external-memory tiled D8, full SPA solar ephemeris, breach-based
depression removal, hydrological conditioning with DEM burning.
