# DECISIONS — terrain-hydrology-11

## D1 — Rescope: deepen instead of rebuild (Phase 0)

Prompt's "why" assumed only slope/aspect/hillshade/roughness/TRI/TPI exist.
Audit found master already has priority-flood fill + D8 + accumulation +
watershed labels (`terrain_flow.*`) and curvatures/MD-hillshade/local-relief
(`terrain_analysis.*`). Per mission rule ("已覆盖的 package 不重复造轮子"),
packages A–C were re-scoped from "introduce hydrology" to "close the documented
next gaps": flat resolution (explicitly declared debt in
`rs_terrain_flow_operator.cpp:70` and `foundation-5.md:49`), D∞, stream network,
auto outlets; D/E/F/G/H are wholly new. No reimplementation of existing kernels.

## D2 — Flat resolution algorithm choice

Candidates: (a) epsilon-gradient priority-flood (Barnes et al. 2014 variant —
fill with tiny monotone increments so every flat drains), (b) Garbrecht–Martz
two-gradient approach, (c) leave flats as sinks (status quo).
Chosen: **(a) epsilon variant exposed as an opt-in `resolve_flats` stage**,
because it reuses the proven priority-flood structure (single code path, same
NoData-barrier semantics), is O(N log N), and epsilon ≥ 0 preserves the existing
fill behavior bit-for-bit when disabled (backward compatible: product=fill stays
byte-identical). Garbrecht–Martz needs distance transforms + two passes with
more edge cases; not worth the extra failure surface for the same contract.
Epsilon is scaled from the DEM's own elevation magnitude (max |z|, machine
epsilon floor) so it works for metre DEMs and microrelief DEMs alike, and the
fill output stays monotone non-decreasing vs the input.

## D3 — D∞ convention

Tarboton (1997) D∞: flow direction is the steepest downward slope on the eight
triangular facets centered on the cell; output encoded as **angle in degrees
clockwise from north** (0–360, consistent with the repo's aspect convention).
Special values: NoData passthrough; undecided (flat/pit) → −1.
Rationale for angle-over-ESRI-code encoding: D∞ is not a code-based method;
angles preserve the facet geometry of the published definition and the repo
already uses clockwise-from-north angles for aspect.
Accumulation (v1): each D∞ cell routes to the single downslope neighbour
underlying its steepest facet, so accumulation stays a deterministic
single-receiver forest peel with exact mass conservation (every cell counts
itself and every cell has exactly one receiver). Fraction-weighted
two-receiver splitting is recorded as a follow-up, not silently claimed.

## D4 — Stream network definition

Strahler order on the D8 graph over an optional filled surface; streams = cells
with `accumulation ≥ threshold` (self-inclusive counts, so a threshold of 1
selects every cell). Stream vectorization v1 = polyline extraction per Strahler
segment (order-1 heads → junctions), emitted as GeoJSON-Like JSON result (not
written via OGR) to avoid new dependencies and because the operator surface for
vector output already uses JSON artifacts in this repo. Coordinates emitted in
map space using the geotransform. Cross-check invariant: every stream cell
belongs to exactly one segment; junction cells end one segment per incoming
higher-order branch.

## D5 — Viewshed algorithm

Candidates: (a) classic R3/radar sweep (Franklin et al.) per-viewpoint
polar sweep with interpolation-free nearest-cell rays, (b) R2 per-cell line
(drawing every target cell's line back to the observer, Bresenham), (c) Xdraw.
Chosen: **(a) R3 sweep** — O(N α) with exact same-cell visibility rules,
naturally yields per-cell horizon for reuse by the solar package (shared
`HorizonGrid` seam), and handles Earth-curvature/refraction correction
(standard k=0.13, 6/7 Earth radius effective radius convention; optional).
R2 is simpler but recomputes O(N) cell lines redundantly and gives no horizon
by-product; Xdraw is approximate on diagonals. Deterministic: azimuth sweep in
fixed 1° steps → no parallel-order dependence.

## D6 — Solar horizon/duration without an ephemeris dependency

Candidates: (a) full solar-position model (NOAA/SPA) inside this track,
(b) caller-provided sun track (list of (azimuth, elevation)), (c) both.
Chosen: **(c) with (b) as the authority**: the kernel takes an explicit
azimuth/elevation series (shadow fraction/duration = fraction of series with
elevation ≤ horizon, weighted by provided weights or uniform). Additionally a
**low-precision solar position helper** (NOAA solar equations, declared
±0.3° accuracy) is provided to *generate* tracks for a date/latitude so
shadow-duration is usable out of the box; it is clearly documented as
approximate (no refraction/leap-second refinements) and tested against a
handful of tabulated values with generous tolerance. This avoids adding a
dependency while keeping the physics honest: the horizon math (exact) is
separate from the sun-position math (approximate, declared).

## D7 — Landform products

Multiscale TPI: circular/neighbourhood mean elevation minus center at radius r
(scale set), with standardization (deviation / neighbourhood SD) and the
Weiss (2001) 6-class classification exposed as a separate product so the raw
and classified layers don't conflate. Geomorphons (Jasiewicz & Stepinski 2013):
8 ternary codes from zenith/nadir line-of-sight comparison over
`search_radius` with `flatness_threshold` (degrees) and `flat_radius` skip —
implemented on the same line-of-sight sweep seam as viewshed (shared code, one
authority for LoS math).

## D8 — Surface integration minimality

Extend `rs:terrain_flow` (hydrology products). New operators: three, not two —
`rs:terrain_viewshed`, `rs:terrain_solar`, and `rs:terrain_landform`. The third
addition is deliberate: `rs:terrain_analysis` is hard-wired to the tiled
3×3-halo streaming path (GdalBlockStream); its schema, memory policy and
halo semantics are wrong for full-frame landform kernels (integral images,
line-of-sight sweeps), and forking the execution model inside one operator
would trade a small registration cost for a much larger correctness surface.
New operators get generated v2 capability sidecars via
`capability_knowledge_tool gen-meta` so test_capability_knowledge stays green
without pin edits (coverage set equality, monotone floor unchanged).

## D9 — Large-DEM path

Keep the repo's documented memory contract (flow = full-frame O(N·k) frames;
terrain_analysis = tiled streaming). For the new full-frame kernels add:
(1) an explicit cell-count guard (fail-closed `InvalidInputData` above a cap,
env `SICNU_TERRAIN_MAX_CELLS` to raise, documented), (2) `throwIfCancelled`
polling in operators between stages and inside the sweep loops (every row),
(3) honest `estimatedRamBytes` per frame count. A tiled/external-memory
flow router remains follow-up (recorded), not silently claimed.

## D10 — Agent tool scope

Two tools only, both read-only helpers (no raster writing):
`spatial:terrain_profile` (elevation profile along a 2-point or polyline path
over a DEM, with per-vertex distance/height) and `spatial:terrain_viewshed_inspect`
(single observer quick viewshed returning JSON summary: visible-cell count,
horizon stats for requested directions). They delegate to the same kernels —
no second implementation.

## D11 — No new dependencies

Everything above is std C++17 + Qt types already used by the terrain family +
GDAL via existing wrappers. No PROJ/GEOS/OpenCV additions needed.
