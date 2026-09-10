# Cartography Gallery & Catalog Index (Design System 4.0)

Generated from the shipped descriptors — the catalog drift test
(`test_cartography_templates.cpp`) checks that every template and
component id below exists in the registries and in the machine index
`data/cartography/index.json`. Regenerate the machine index with
`SICNU_CARTOGRAPHY_REGENERATE_INDEX=1` in the test environment; keep
this document in sync when you add descriptors.

> `extends` children are listed with their own page and slots; product
> type, tasks and style are **inherited from their base** (shown as `—`
> here). The machine index and `cartography:list_templates` operate on
> the *resolved* descriptors, so children match their base's tasks.

Shipped: **56 templates**, **57 components** (with variants), 2 token sets.

## Design token sets

| id | mediums | description |
|----|---------|-------------|
| `scientific-light` | print (default), screen | Light scientific style, colorblind-safe palettes. |
| `scientific-dark` | screen (base), print variant (experimental) | Dark style for dashboards and slides. |

## Templates (task-oriented)

| id | page (mm) | product | extends | tasks | description |
|----|-----------|---------|---------|-------|-------------|
| `accuracy-assessment-a4l` | 297×210 | accuracy | — | accuracy, validation | Accuracy assessment page: map, confusion matrix, metric cards and legend. |
| `atlas-series-a4l` | 297×210 | atlas | — | atlas, series | Map-series sheet with the atlas hook enabled (per-feature coverage and file names). |
| `burned-area-a4l` | 297×210 | burned_area | — | fire, burned-area | Burned-area product with severity colorbar and class chart. |
| `change-before-after-a4l` | 297×210 | change | — | change, before-after | Before/after synchronized dual map page with transition legend, change matrix and uncertainty note. |
| `change-before-after-a4p` | 210×297 | — | change-before-after-a4l |  | Before/after synchronized dual map page with transition legend, change matrix and uncertainty note. |
| `change-detection-a4l` | 297×210 | change | — | change, change-detection | Post-classification change map with transition legend, change matrix and uncertainty note. |
| `choropleth-a4l` | 297×210 | choropleth | — | choropleth, statistics | Choropleth of region statistics with diverging colorbar. |
| `classification-a3l` | 420×297 | — | classification-a4l |  | Categorical classification result map with legend, class-area chart, scale bar, north arrow and source note. |
| `classification-a4l` | 297×210 | classification | — | classification, obia | Categorical classification result map with legend, class-area chart, scale bar, north arrow and source note. |
| `classification-a4p` | 210×297 | — | classification-a4l |  | Categorical classification result map with legend, class-area chart, scale bar, north arrow and source note. |
| `cloud-cover-a4l` | 297×210 | qa | — | qa, cloud, mask | QA cloud/shadow mask overview with class legend and coverage chart. |
| `crop-monitoring-a4l` | 297×210 | crop | — | crop, agriculture | Crop monitoring map with phenology time-series chart and class legend. |
| `forest-cover-a4l` | 297×210 | forest | — | forest | Forest cover / change map with sequential-green colorbar and class chart. |
| `heatmap-density-a4l` | 297×210 | density | — | heatmap, density | Kernel-density / heatmap product with heat colorbar. |
| `inset-locator-a4l` | 297×210 | locator | — | locator, overview | Main map with locator inset, class-area chart and full furniture. |
| `land-cover-a3l` | 420×297 | — | land-cover-a4l |  | Land-cover product map with locator inset, class-area chart and standard furniture. |
| `land-cover-a4l` | 297×210 | land_cover | — | land-cover, land-use | Land-cover product map with locator inset, class-area chart and standard furniture. |
| `land-use-a4l` | 297×210 | land_use | — | land-use, zoning | Land-use / zoning map with multi-column legend for wide class sets. |
| `land-use-a4p` | 210×297 | — | land-use-a4l |  | Land-use / zoning map with multi-column legend for wide class sets. |
| `model-uncertainty-a4l` | 297×210 | uncertainty | — | uncertainty, quality | Model uncertainty map with uncertainty palette colorbar and accuracy metric cards. |
| `multi-panel-a3l` | 420×297 | — | multi-panel-a4l |  | Four-panel comparison sheet (small multiples) with shared legend. |
| `multi-panel-a4l` | 297×210 | multi_panel | — | multi-panel, comparison | Four-panel comparison sheet (small multiples) with shared legend. |
| `night-lights-a4l` | 297×210 | night_lights | — | urban, night-lights | Nighttime lights continuous product with diverging colorbar (change years). |
| `operational-sheet-a3l` | 420×297 | — | operational-sheet-a4l |  | Operational map sheet: metric grid, dual-unit scale bar, navigation arrow and disclaimer. |
| `operational-sheet-a4l` | 297×210 | operational | — | operational, mission | Operational map sheet: metric grid, dual-unit scale bar, navigation arrow and disclaimer. |
| `presentation-screen` | 338.7×190.5 | presentation | — | presentation, screen | 16:9 presentation map for screen delivery (screen tokens, larger type). |
| `presentation-screen-dark` | 338.7×190.5 | presentation | — | presentation, dashboard, dark | 16:9 presentation map on the dark token set for dashboards. |
| `remote-sensing-result-a4l` | 297×210 | continuous | — | continuous, result | Generic continuous remote-sensing result with sequential colorbar. |
| `remote-sensing-result-a4p` | 210×297 | — | remote-sensing-result-a4l |  | Generic continuous remote-sensing result with sequential colorbar. |
| `report-page-a4l` | 297×210 | — | report-page-a4p |  | A4 portrait report page with section label, map, charts, metadata and disclaimer. |
| `report-page-a4p` | 210×297 | report | — | report | A4 portrait report page with section label, map, charts, metadata and disclaimer. |
| `road-transport-a4l` | 297×210 | transport | — | road, transport | Road / transport network map with section label and furniture. |
| `sar-backscatter-a3l` | 420×297 | — | sar-backscatter-a4l |  | SAR backscatter (dB) continuous map with dB colorbar and histogram. |
| `sar-backscatter-a4l` | 297×210 | sar | — | sar, backscatter | SAR backscatter (dB) continuous map with dB colorbar and histogram. |
| `sar-change-a4l` | 297×210 | sar | — | sar, change | SAR ratio/log-ratio change page: before/after frames with transition legend and uncertainty note. |
| `scientific-publication-a3l` | 420×297 | — | scientific-publication-a4l |  | Two-panel scientific publication page with captions, confusion matrix, metric cards and metadata. |
| `scientific-publication-a4l` | 297×210 | publication | — | publication | Two-panel scientific publication page with captions, confusion matrix, metric cards and metadata. |
| `ship-detection-a4l` | 297×210 | detection | — | sar, ship, detection | SAR ship-detection result with detection chart and navigation furniture. |
| `slope-aspect-a4l` | 297×210 | terrain | — | terrain, slope | Slope/aspect paired small multiples with shared legend and distribution chart. |
| `snow-cover-a4l` | 297×210 | snow | — | snow, cryosphere | Snow-cover extent map with probability colorbar and furniture. |
| `soil-moisture-a4l` | 297×210 | soil_moisture | — | soil, agriculture, sar | Soil-moisture product (SAR-derived) with sequential colorbar and series chart. |
| `terrain-dem-a4l` | 297×210 | terrain | — | terrain, dem | Terrain DEM product with elevation colorbar and furniture. |
| `terrain-dem-a4p` | 210×297 | — | terrain-dem-a4l |  | Terrain DEM product with elevation colorbar and furniture. |
| `time-series-phenology-a4l` | 297×210 | time_series | — | time-series, phenology | Phenology small multiples (2×2 temporal steps) with shared legend and series chart. |
| `time-series-phenology-a4p` | 210×297 | — | time-series-phenology-a4l |  | Phenology small multiples (2×2 temporal steps) with shared legend and series chart. |
| `urban-building-a4l` | 297×210 | urban | — | urban, building | Urban / building footprint density map with class chart. |
| `vegetation-index-a3l` | 420×297 | — | vegetation-index-a4l |  | Vegetation index (NDVI/EVI) continuous map with sequential colorbar and profile chart. |
| `vegetation-index-a4l` | 297×210 | vegetation | — | vegetation, ndvi, index | Vegetation index (NDVI/EVI) continuous map with sequential colorbar and profile chart. |
| `water-flood-a3p` | 297×420 | — | water-flood-a4l |  | Water / flood extent map with probability colorbar and uncertainty note. |
| `water-flood-a4l` | 297×210 | water | — | water, flood | Water / flood extent map with probability colorbar and uncertainty note. |

| `poster-a0-foundation` | 841×1189 | poster | — | poster, publication | A0 portrait poster foundation: hero map, section rails for charts/tables/methodology (content slots intentionally unconstrained). |
| `report-atlas-appendix-a4l` | 297×210 | report | — | atlas, report | Atlas appendix page: per-feature statistics table, top-N ranking and trend sparkline; designed for the atlas page sequence. |
| `report-multipage-a4l` | 297×210 | report | — | report, operations, monitoring | Three-page operational report: cover with title block, main map page, and a statistics page with tables, metrics and methodology notes. Page roles drive conditional assembly. |
| `report-multipage-a4p` | 210×297 | report | — | report, operations | Portrait three-page report variant for document-style deliverables (cover, map, statistics). |
| `report-scientific-a3l` | 420×297 | publication | — | publication, scientific | A3 landscape scientific paper page: large map panel with figure caption, provenance and uncertainty note for journal submissions. |
| `report-screen-16x9` | 297×167 | report | — | report, presentation | 16:9 screen briefing page (dark tokens): map with headline metric cards for presentations. |

## Components (categorized, with variants)

| id | category | variants | description |
|----|----------|----------|-------------|
| `accuracy/report` | accuracy | — | Accuracy report block (overall accuracy, kappa, per-class precision/recall) for classification products. |
| `annotation/callout` | annotation | — | Free annotation with optional leader hint; anchored anywhere. |
| `chart/area` | chart | — | Area chart (cumulative composition). |
| `chart/bar` | chart | `grouped`, `stacked` | Category bar chart (class areas, counts). |
| `chart/class-area` | chart | — | Class-area bar chart bound to classification statistics (hectares or percent). |
| `chart/class-composition` | chart | — | Class composition chart (stacked share of classes) for categorical products. |
| `chart/confusion-matrix` | chart | `confusion`, `change-matrix` | Confusion/change matrix rendered as a labeled grid; diagonal emphasized. |
| `chart/histogram` | chart | — | Histogram of values (band statistics, index distribution). |
| `chart/line` | chart | `single-series`, `multi-series` | Line chart (profiles, trends). |
| `chart/metric-card` | chart | — | Compact metric card (OA, Kappa, F1) with big value + label, for accuracy strips. |
| `chart/pie` | chart | `pie`, `donut` | Share-of-total chart. |
| `chart/scatter` | chart | — | Scatter plot (feature relationships). |
| `chart/time-series` | chart | — | Time-anchored line chart with date labels (phenology, NDVI trajectory, water level). |
| `colorbar/asymmetric` | color-bar | — | Asymmetric diverging strip where the neutral point is off-center (declared min/mid/max). |
| `colorbar/discrete` | color-bar | — | Discrete class strip for qualitative rasters; uses an explicit color stop list. |
| `colorbar/diverging` | color-bar | — | Diverging ramp centered on a neutral midpoint (anomalies, change magnitude, NDVI anomalies). |
| `colorbar/logarithmic` | color-bar | — | Log-scaled strip for skewed variables (backscatter proxies, density); label ticks are decades. |
| `colorbar/probability` | color-bar | — | Probability/confidence strip fixed to [0,1] with percent labels. |
| `colorbar/sar-db` | color-bar | — | SAR backscatter strip in dB with negative-value ticks (typically -25…0 dB). |
| `colorbar/sequential` | color-bar | `blue`, `green`, `heat` | Sequential gradient strip with min/max labels; ramp resolves through the token palette table. |
| `frame/classic` | frame | `thin`, `scientific` | Map frame border style applied to the primary map frame. |
| `grid/graticule` | grid | `degree-1`, `degree-05`, `degree-01` | Latitude/longitude graticule over the map frame. |
| `grid/metric` | grid | — | Metric coordinate grid (meters) for projected frames (UTM/Gauss-Krüger sheets). |
| `inset-map/locator` | inset-map | — | Overview locator: small-frame extent outline of the main map's coverage. |
| `legend/categorical` | legend | `single-column`, `multi-column`, `compact` | Class-based legend for categorical rasters and vectors. |
| `legend/change-transition` | legend | — | From→To transition legend for change products (paired rows colored by transition class). |
| `legend/continuous` | legend | — | Gradient legend body for continuous rasters (paired with a colorbar). |
| `legend/grouped` | legend | `flat`, `hierarchical` | Legend with grouped headings (e.g. land cover groups, scenario groups). |
| `legend/raster-class` | legend | — | Fixed raster class legend (class id, label, color) that does not depend on the live renderer. |
| `legend/uncertainty` | legend | — | Uncertainty/hatching legend for probability or confidence products; uses the token uncertainty palette. |
| `map-frame/comparison` | map-frame | — | Paired before/after frames declared with equal size and aligned tops for change products. |
| `map-frame/primary` | map-frame | — | Primary map frame with declared extent, scale, rotation, and layer set. |
| `map-frame/small-multiples` | map-frame | — | N synchronized small-multiple frames (temporal steps, scenarios) declared with a count and grid. |
| `north-arrow/minimal` | north-arrow | — | Simple grid-north arrow for clean thematic layouts. |
| `north-arrow/navigation` | north-arrow | — | Operational/navigation north arrow for mission sheets and quick looks. |
| `north-arrow/scientific` | north-arrow | — | Publication-grade true-north arrow. |
| `publication/figure-label` | publication | — | Figure label ('图1', 'Figure 2') in the token figure_label style. |
| `publication/footer` | publication | — | Product footer (logo placeholder, production date, contact/licence line) pinned to the page edge. |
| `publication/logo-placeholder` | publication | — | Institution logo placeholder box; swap the picture path at composition time. |
| `publication/panel-image` | publication | — | Photo/panel image frame for report pages (scene thumbnail, field photo). |
| `scale-bar/dual-unit` | scale-bar | — | Metric scale bar paired with a secondary unit label line (km + miles) for international reports. |
| `scale-bar/single` | scale-bar | `single-box`, `double-box`, `line-ticks-up`, `line-ticks-down`, `stepped-line` | Scale bar for projected maps; style variants map to QGIS scale bar renderers. |
| `source-note/default` | source-note | — | Data source and processing provenance note (token style 'source_note'). |
| `statistics/panel` | statistics | — | Compact statistics panel (min/max/mean or per-class counts) for numeric result layers. |
| `subtitle/standard` | subtitle | — | Secondary line under the main title: product, region, or date context. |
| `text/disclaimer` | text | — | Legal or interpretation disclaimer rendered in the secondary text color. |
| `text/metadata-block` | text | — | Multi-line metadata block (sensor, date, CRS, processing level) for the map margin. |
| `text/scientific-caption` | text | — | Figure caption with number, e.g. 'Fig. 1 — Land cover 2024'. Styled from token 'caption'. |
| `text/section-label` | text | — | Short section label ('A', 'Study area', '方法') introducing a map panel or figure group. |
| `text/uncertainty-note` | text | — | Standalone uncertainty note (kind, level, source) required when the product declares uncertain data. |
| `title/bilingual` | title | — | Two-line bilingual title (CJK primary line, Latin secondary line) rendered as one label block. |
| `title/main` | title | `standard`, `compact` | Primary map title. Token style 'title' (20pt bold by default); one per page. |

| `chart/grouped-bar` | chart.statistics, chart.trend | `paired`, `multi` | Grouped bar chart: side-by-side series per label with a compact series legend (real multi-series rendering). |
| `chart/sparkline` | chart.trend | — | Axes-free compact trend line with a dashed baseline; the series-extraction companion for report blocks. |
| `chart/summary-table` | chart.statistics | — | Summary table with an n/mean and sum/min-max header block above the rows. |
| `chart/table` | chart.statistics | `zebra`, `plain` | Two-column label/value table (class areas, counts, metrics); deterministic 64-row cap with an overflow row. |
| `chart/topn-table` | chart.statistics | `top-5`, `top-10` | Top-N table: rows sorted by value descending, capped at style.top_n (default 10) with a rank column. |

## Agent workflow

`cartography:list_templates` → `cartography:instantiate_template` →
`cartography:compose` → `cartography:preflight` → `cartography:repair` →
`layout:export`. See the authoring guides and ADR 0130/0131 in `docs/adr/`.
