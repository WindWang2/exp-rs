# NODATA_SEMANTIC_MATRIX.md — R4 NoData/掩膜传播语义审计表

Track 7 (R4). Evidence: three read-only sweeps over `src/operators/rs/` +
their `src/processing/algorithms/` kernels (2026-09-27, worktree
`hardening/r4-operator-oracles` @ `15e5c66b5`). Line numbers are master
baselines; audit quotes preserved in the sweep transcripts archived in
EVIDENCE.md.

Classification vocabulary (brief section 使命): 参与统计 = declared sentinel
ingested as data (defect) · 置NoData = invalid input propagates to declared
output NoData · 排除统计 = sentinel/NaN excluded from statistics · 报错 =
typed refusal · 未定义 = no handling · 不适用 = no raster pixels touched.

Disposition vocabulary: **固化** = behavior correct, fixation test added this
track · **修复** = defect fixed this track (commit map at bottom) ·
**声明修复** = output NoData declaration added, pixels unchanged ·
**backlog** = documented, deferred · **正确** = correct, no test added.

Legend: ND = declared nodata sentinel; NaN = non-finite.

## A. 统计/掩膜类（brief P0 审计对象）

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 1 | rs:zonal_stats | per-band `bandNoDataValue` (rs_zonal_stats_operator.cpp:171) | sentinel/NaN → `++acc.nodata`, excluded from min/max/mean/stddev/median (Welford, :302) | 排除统计 | 固化 (test_operator_nodata_semantics) |
| 2 | rs:focal_stats | :347 via runWindowOp | sentinel→NaN in halo buffer (:378-381), excluded from window stats (:404-405); non-finite center → NaN out | 排除统计 + 置NoData | 固化 |
| 3 | rs:temporal_summary | via temporal_stream.cpp:166 normalizeAndMask | sentinel/NaN → NaN (:172-173), accumulator skips (:252-254); outputs declare NaN | 排除统计 + 置NoData | 固化 (D6) |
| 4 | rs:segment_stats | rs_segment_stats_operator.cpp:131 | pixel skipped if any band NaN/Inf/==ND (:148-155); CSV output | 排除统计 | 固化 |
| 5 | rs:sar_temporal_stats | per-scene :264-268 | `isfinite && !sentinel` else NaN (:305-310) before sarTemporalStats; outputs NaN-declared | 排除统计 + 置NoData | 固化 |
| 6 | rs:regress | engine tile_inference_engine.cpp:1224 | sentinel→NaN (:1926-1934); all-NoData tiles skip forward pass; output NaN-declared | 排除统计 + 置NoData | 固化 |
| 7 | rs:segment | engine (same as #6) | same engine path; labels/masks get domain-validated sentinels (:1050-1082) | 排除统计 + 置NoData | 固化 |
| 8 | rs:apply_mask | per-band :271; undeclared+no param → typed refusal (:276-281) | mask>0 → pixel overwritten with band ND (:398-428); pre-existing sentinels pass through by design | 置NoData | 固化 (WP-B closed-form) |
| 9 | rs:qa_mask | :278 | fail-closed: ND/NaN/Inf/negative/out-of-range → `unknown` → mask=1 (:290-312, :438-439) | 排除统计(保守) | 固化 (WP-B) |
| 10 | rs:sar_terrain_masks | DEM :410/:242 | sentinel→NaN in halo; any non-finite in 3×3 → NaN/255 out (:437-449) | 置NoData | 固化 |
| 11 | rs:obia_segment | band-1 only :132-135 | sentinel/NaN masked before labeling in rs_simple_segmenter.cpp:59-62; label 0 = ND declared | 排除统计 + 置NoData | 正确 (band-1-only caveat noted; OTB engine exemption → backlog) |
| 12 | rs:rasterize | n/a (vector→raster burn) | unburned pixels NaN, declared (:149, :69) | 置NoData | 正确 |

## B. 光谱/辐射/大气/变化族

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 13 | rs:spectral_index | seam SI :478 | sentinel/non-finite → NaN before kernels (:491-496); output NaN (:516); probe stats exclude | 置NoData | 固化 |
| 14 | rs:ndvi | seam SI | via #13; zero denominator → NaN (safeDiv) | 置NoData | 固化 (chain WP-C) |
| 15 | rs:evi | seam SI | via #13 | 置NoData | 正确 |
| 16 | rs:ndwi | seam SI | via #13 | 置NoData | 正确 |
| 17 | rs:savi | seam SI | via #13 | 置NoData | 正确 |
| 18 | rs:ndbi | seam SI | via #13 | 置NoData | 正确 |
| 19 | rs:mndwi | seam SI | via #13 | 置NoData | 固化 (WP-F) |
| 20 | rs:band_math | band_math.cpp:144 | per-tile NaN-ize sentinel/non-finite before expression (:183-188); output NaN (:160) | 置NoData | 固化 (digest) |
| 21 | rs:band_ratio | band_tools.cpp:27-32 | ratio: masked to NaN (image_enhancement_streaming.cpp:781-785), declared :68; **IHS mode: NaN holes, NO declaration** (:126-131) | 置NoData(输出未声明) | 声明修复 (WP-B) |
| 22 | rs:extract_bands | band_tools.cpp:222 | verbatim tile copy; sentinel re-declared per band (:218-224) | 置NoData | 正确 |
| 23 | rs:contrast_stretch | band_tools.cpp:277 | stats exclude sentinel (:879,:906,:963) **but output holes rewritten with input sentinel, never declared** (:1013-1016) | 排除统计(输出未声明) | 声明修复 (WP-B) |
| 24 | rs:image_enhancement | :214-220 | stretch ok; **ratio path: sentinel pair → ratio 1.0 (counted as data)** (:293 → 3-arg kernel :765-769); IHS ok; **no output declaration anywhere** | 参与统计(ratio)/未声明 | 修复 + 声明修复 (WP-B); review P2-4: filter/speckle windowed kernels still ingest sentinels as data (their new NaN declaration writes no holes for sentinel-only inputs) — same defect class as ratio, masked-in-window repair → **backlog (D4)**
| 25 | rs:continuum_removal | :102 | whole-spectrum rejected if any band sentinel/NaN (spectral_classification.cpp:294-301) → ND written all bands; output declares | 报错(逐像素)+置NoData | 固化 (WP-F) |
| 26 | rs:spectral_resample | :226 | sentinel neighbor → NaN out (#445 :286-294); outputs NaN | 置NoData | 固化 (WP-F) |
| 27 | rs:atmospheric_correction | seam AT (atmospheric_correction.cpp:623-624) | sentinel→NaN pre-transform (:653-654,:693-694); outputs NaN | 置NoData | 固化 (chain WP-C) |
| 28 | rs:dn_to_radiance | seam AT | via #27 | 置NoData | 固化 (chain WP-C) |
| 29 | rs:atmospheric_dos1 | :797-798 | dark-object histogram masks sentinel BEFORE stats (:803-826) | 排除统计 + 置NoData | 正确 |
| 30 | rs:atmospheric_dos2 | :797-798 | as #29 | 排除统计 + 置NoData | 正确 |
| 31 | rs:atmospheric_quac | :474-475 | per-band mask (:480-484); scene stats skip non-finite; outputs NaN | 排除统计 + 置NoData | 正确 |
| 32 | rs:radiometric_calibration | radiometric_calibration.cpp:810-811 | streaming NaN-izes (:839-842); BT: non-physical → ND (:981-982); outputs NaN | 置NoData | 固化 (chain WP-C) |
| 33 | rs:change_detection | seam CH :127/:152 | sentinel→NaN both epochs; magnitude NaN; mask declares 255 | 置NoData | 固化 (chain WP-C) |
| 34 | rs:change_difference | seam CH | NaN propagates through |a−b| (:549-562) | 置NoData | 固化 |
| 35 | rs:change_normalized_difference | seam CH | safeDiv NaN | 置NoData | 正确 |
| 36 | rs:change_ratio | seam CH | before≤0 → NaN (#700) | 置NoData | 正确 |
| 37 | rs:change_cva | seam CH | any-band NaN → NaN magnitude | 置NoData | 固化 (chain WP-C) |
| 38 | rs:change_cva_angle | seam CH | NaN; quadrant mode declares 255 (:788-794) | 置NoData | 正确 |
| 39 | rs:change_sam | seam CH | NaN buffers; result stats skip non-finite | 置NoData | 正确 |
| 40 | rs:change_log_ratio | seam CH | explicit non-finite → NaN (:721-725); negatives clamped pre-ln | 置NoData | 正确 |
| 41 | rs:change_mad | seam CH | NaN pixels excluded from covariance fit (change_detection.cpp:459-471) | 排除统计 + 置NoData | 正确 |
| 42 | rs:change_irmad | seam CH | `pixelValid` all-bands-both-dates (:1037-1044); degenerate → typed error | 排除统计 + 置NoData | 正确 |
| 43 | rs:threshold_raster | rs_change_streaming.cpp:1337 | NaN-ized pre-histogram (#612); mask declares 255 | 排除统计 | 固化 (WP-F) |
| 44 | rs:topographic_correction | :273 | sentinel→NaN pre-regression; OLS accumulates finite pairs only; NaN out declared | 排除统计 + 置NoData | 正确 |
| 45 | rs:solar_geometry | n/a (metadata only) | no pixels touched | 不适用 | 正确 |
| 46 | rs:brdf_normalization | :269 | sentinel→NaN (:317-319); counts corrected vs nonFinite; NaN declared | 置NoData | 固化 (WP-F) |
| 47 | rs:radiometric_qa | **none** (by design) | exhaustive flag classification: non-finite→bit3, negative→bit4 (radiometric_qa.cpp:42-53); no output ND | 不适用(按设计逐像素分类) | 正确 (D5) |
| 48 | rs:spectral_derivative | **none** | declared sentinel participates in finite differences; only NaN propagates; output NaN declared | 参与统计 | 修复 (WP-A) |

## C. 地形/水文族

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 49 | rs:terrain_analysis | :185 (param default −9999) | kernels write nodata for invalid centers/neighbors (terrain_analysis.cpp:54,60); output declared only when sentinel resolved | 置NoData | 固化 (chain WP-C) |
| 50 | rs:terrain_flow | :172 | NoData = barriers, never filled (:80); excluded from stats; re-masked products | 排除统计 + 置NoData | 正确 |
| 51 | rs:terrain_viewshed | :242 | NoData opaque (rays stop); observer-on-NoData typed refusal; 255/65535 declared | 报错 + 置NoData | 正确 |
| 52 | rs:terrain_solar | :232 | excluded from shadowDuration | 置NoData | 正确 |
| 53 | rs:terrain_landform | :236 | kernels take nodata; class 255 remapped to sentinel | 置NoData | 正确 |

## D. SAR 族

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 54 | rs:sar_calibrate | :315 (bandNoDataSentinel) | invalid → NaN (sar_calibration.cpp:87-90); nonpositive power NaN; NaN declared | 置NoData | 固化 (WP-F closed form) |
| 55 | rs:sar_backscatter | :251 | invalid → NaN (:196-199); incidence sentinel→NaN (:158-187); NaN declared | 置NoData | 正确 |
| 56 | rs:sar_terrain_flatten | :245 + DEM in-kernel | SAR sentinel→gamma NaN + validity 255; DEM hole → NaN + validity 0 (sar_terrain.cpp:160-215) | 置NoData | 正确 |
| 57 | rs:sar_terrain_correction | :225 | same kernel as #56 | 置NoData | 正确 |
| 58 | rs:sar_speckle | :237 per band | spatial filters pre-mask to NaN (sar_speckle.cpp:418-424); multitemporal skips invalid samples; NaN halo padding (no edge contamination) | 排除统计 + 置NoData | 固化 (digest) |
| 59 | rs:sar_ratio | :292-293 | va/vb sentinel → NaN (sar_ratio.cpp:116-118); B==0 → NaN | 置NoData | 正确 |
| 60 | rs:sar_texture | :232 | any sentinel in window → NaN measures (:313-329), documented | 置NoData | 正确 |
| 61 | rs:sar_change | :216-217 | both scenes sentinel → NaN | 置NoData | 正确 |
| 62 | rs:sar_dualpol_features | :158,:164 | sentinel/non-finite → NaN (:190-200); NaN declared | 置NoData | 正确 |
| 63 | rs:sar_geocode | DEM :266-270, SAR :277-281 | cell status 1=DEM-ND 2=geometry-unresolved; per-class counters reported | 排除统计 + 置NoData | 正确 |
| 64 | rs:sar_polsar_decompose | **none** | sentinels accumulated as complex samples in covariance windows (:330-334); output NaN only for unfinalizable ensembles; **no declaration** | 参与统计 | 修复 + 声明修复 (WP-A) |
| 65 | rs:sar_interferogram | finiteness only | requires finite phase/amp/corr (:314-315); coherence declares NaN, **CFloat32 band undeclared** | 置NoData(部分) | backlog (complex-band declaration needs product-contract decision) |
| 66 | rs:sar_phase_filter | **none** | non-finite → NaN cos/sin (:161-165); **outputs undeclared** | 置NoData(未声明) | 声明修复 (WP-B) |
| 67 | rs:sar_unwrap | finiteness only | fail-closed when nothing seedable (:351-361); NaN declared :239/:373 | 报错 + 置NoData | 正确 |
| 68 | rs:sar_displacement | finiteness only | accumulates isfinite neighbors only; NaN declared | 排除统计 + 置NoData | 正确 |
| 69 | rs:sar_coregister | **none** | bilinear complex resample, no validity concept; sentinel-contaminated slave interpolated as signal | 参与统计 | backlog (requires resampling-contract design; documented) |
| 70 | rs:sar_coregister_local | **none** | no sentinel test in correlation windows; NaN declared on output | 参与统计(局部窗口) | backlog |
| 71 | rs:sar_temporal_events | :308-312 | same per-scene policy as #5 | 排除统计 + 置NoData | 正确 |
| 72 | rs:sar_remove_topographic_phase | finiteness only | per-pixel finiteness "authoritative" (:400-402); NaN declared | 置NoData | 正确 |
| 73 | rs:sar_pair_network | n/a (JSON) | no pixels | 不适用 | 正确 |
| 74 | rs:sar_network_inversion | finiteness only | non-finite skipped from inversion (:415); NaN declared all outputs | 排除统计 + 置NoData | 正确 |

## E. 几何/融合/镶嵌族

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 75 | rs:image_fusion | kernel G-FUSION image_fusion.cpp:783-802 | ms/pan sentinel or NaN → out nodata (:106-108,:147,:166); declared per band; "never a fabricated −9999" | 置NoData | 正确 |
| 76 | rs:fusion_linear | via #75 | identical | 置NoData | 正确 |
| 77 | rs:fusion_brovey | via #75 | identical | 置NoData | 正确 |
| 78 | rs:fusion_pca | via #75 | identical | 置NoData | 正确 |
| 79 | rs:fusion_ihs | via #75 | identical | 置NoData | 正确 |
| 80 | rs:fusion_gram_schmidt | via #75 | identical | 置NoData | 正确 |
| 81 | rs:resample | **none** (SEAM G-WARP) | warp argv carries no nodata options (raster_convert.cpp:206-253); sentinels resampled as data; output declaration = GDAL default | 未定义 | backlog (D4) |
| 82 | rs:align | **none** | as #81; exact-grid path is lossless copy | 未定义 | backlog (D4) |
| 83 | rs:mosaic | band-1 per input :206-208 | valid = finite && ≠ND (:417-423); invalid never written; tiles init to outNodata | 排除统计 + 置NoData | 正确 |
| 84 | rs:quality_mosaic | per scene+band :534-547 | ND→NaN in sampler (:102-110); feather blends use ND fallback | 排除统计 + 置NoData | 正确 |
| 85 | rs:register_images | **hardcoded −9999** (resampler.h:26) | declared sentinel ≠ −9999 warped as signal; −9999 treated ND regardless; unmapped output = 0.0f **undeclared** | 参与统计(契约错位) | 声明修复(输出) + backlog(重采样契约) (D4) |
| 86 | rs:stack_register | n/a (JSON) | no pixels | 不适用 | 正确 |

## F. 导入族

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 87 | rs:landsat_import | seam I satellite_products.cpp:1654-1655 | verbatim copy; sentinel re-declared (:1680-1681); never synthesized | 置NoData | 正确 |
| 88 | rs:sentinel2_import | seam I | as #87 | 置NoData | 正确 |
| 89 | rs:modis_import | seam I | as #87 | 置NoData | 正确 |
| 90 | rs:gaofen_import | seam I | as #87 | 置NoData | 正确 |
| 91 | rs:zy3_import | seam I | as #87 | 置NoData | 正确 |
| 92 | rs:hj_import | seam I | as #87 | 置NoData | 正确 |
| 93 | rs:cn_product_import | seam I | as #87 (plan-driven) | 置NoData | 正确 |
| 94 | rs:modis_georeference | copy path only | assign-georef path re-declares; **warp path has no nodata options** | 未定义(warp) | backlog (D4) |

## G. 分类/检测/特征族

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 95 | rs:sam_classify | band-0 :144-145 | invalid → label −9999 (:199-207); label ND declared, score NaN | 置NoData | 正确 |
| 96 | rs:spectral_unmixing | per band :131 | sentinel→NaN in buffer (:193-198); non-finite spectrum → NaN abundances | 置NoData | 正确 |
| 97 | rs:sparse_unmixing | same seam C-UNMIX | identical policy | 置NoData | 正确 |
| 98 | rs:rx_anomaly | :114-127 (never fabricated) | stats skip invalid; invalid → NaN; NaN declared | 排除统计 + 置NoData | 正确 |
| 99 | rs:local_rx_anomaly | :156-162 | same policy | 排除统计 + 置NoData | 正确 |
| 100 | rs:matched_filter | seam C-DETECT :144-149 | background stats skip invalid; per-pixel invalid → NaN; NaN declared | 排除统计 + 置NoData | 正确 |
| 101 | rs:ace | seam C-DETECT | as #100 | 排除统计 + 置NoData | 正确 |
| 102 | rs:cem_detection | seam C-DETECT | as #100 | 排除统计 + 置NoData | 正确 |
| 103 | rs:tcimf_detection | seam C-DETECT | as #100 | 排除统计 + 置NoData | 正确 |
| 104 | rs:osp_detection | seam C-DETECT | as #100 | 排除统计 + 置NoData | 正确 |
| 105 | rs:spectral_similarity | **hardcoded −9999** (:176, kernel constant) | declared sentinel ignored; only exact −9999/NaN excluded | 参与统计(契约错位) | 修复 (WP-A); review P3-9: kernel carries a single sentinel — inputs with differing per-band sentinels get the first declared one applied to all bands (documented in metadata); per-band sentinel interface → **backlog** |review P3-9: kernel carries a single sentinel — inputs with differing per-band sentinels get the first declared one applied to all bands (documented in metadata); per-band sentinel interface → **backlog**
| 106 | rs:endmember_analysis | n/a (table) | kernel uses hardcoded kNoDataSentinel in comparisons | 不适用 | 正确 |
| 107 | rs:endmember_extraction | :201-205 | isPixelValid gates PPI passes | 排除统计 | 正确 |
| 108 | rs:spectral_spatial_fuse | band-1 :180-187 | invalid never fused, never in stats | 排除统计 + 置NoData | 正确 |
| 109 | rs:feature_stack | :318-321 | sentinels copied verbatim + declared per band | 置NoData | 正确 |
| 110 | rs:feature_normalize | :196-202 | isValid gates stats | 排除统计 + 置NoData | 正确 |
| 111 | rs:feature_select | :291-301 | band-copy selector, declared | 置NoData | 正确 |
| 112 | rs:pca | per-band :1598-1601 (isPixelValid #444 float-space compare) | R4 re-verification: processPcaFile DOES read declared sentinels and excludes them from the mean AND covariance passes (validPixelCount gate, image_enhancement.cpp:1597-1689); the initial sweep claim was wrong and is corrected here | 排除统计 | 固化 |

## H. 其余（模型/推断/OBIA/时间辅助等）

| # | operator | 声明语义读取 | 实测行为 | 分类 | 处置 |
|---|---|---|---|---|---|
| 113 | rs:detect / rs:embedding / rs:classify / rs:change (model-task family) | engine tile_inference_engine.cpp:1224 | same engine contract as #6/#7 | 排除统计 + 置NoData | 固化 (same engine as regress) |
| 114 | rs:local_extrema | shared runWindowOp (as #2) | same window policy | 排除统计 + 置NoData | 正确 |
| 115 | rs:obia_hierarchy / rs:obia_label | rs_obia_* (no mention) | label-domain operators over segment maps; label 0 reserved nodata by segment contract (#11) | 置NoData | 正确 |

## Commit map (filled at close, review pass 1)

| disposition | files | commit |
|---|---|---|
| 修复 rs:spectral_derivative | src/operators/rs/rs_spectral_derivative_operator.cpp | be1b0f8ee |
| 修复 rs:spectral_similarity | src/operators/rs/rs_spectral_similarity_operator.cpp | b6225cf35 |
| 修复 rs:sar_polsar_decompose | src/operators/rs/rs_sar_polsar_decompose_operator.cpp | b1e33b126 |
| 修复 rs:image_enhancement ratio + declarations | src/operators/rs/rs_image_enhancement_operator.cpp | bbf5e7ee0 |
| 声明修复 band_ratio IHS / contrast_stretch | src/processing/algorithms/band_tools.cpp | c6e90edea |
| 声明修复 sar_phase_filter | src/operators/rs/rs_sar_phase_filter_operator.cpp | f4a19336e |
| 声明修复 register_images (output ND) | src/operators/rs/rs_register_images_operator.cpp | 9003c4279 |

## Coverage accounting

- Rows: **115** (≥60 required). Registered operators not individually listed
  are pure JSON/metadata/registry entries whose contract excludes raster
  pixels (e.g. capability/help surfaces), or aliases resolved above.
- Defects fixed this track: 7 (3 "counted as data" repairs:
  spectral_derivative, spectral_similarity, sar_polsar_decompose; 4
  output-declaration repairs: band_ratio IHS, contrast_stretch,
  sar_phase_filter, register_images — image_enhancement counts in the
  counted-as-data group via its unmasked ratio path and also gains output
  declarations). The initially-suspected rs:pca defect was refuted on
  direct code re-verification (exclusion present at the kernel).
- Backlog (documented, out of scope by D4/risk): warp-family nodata
  contract (rs:resample/rs:align/rs:modis_georeference full fix),
  sar coregister family resampling contract, interferogram CFloat32
  declaration, obia OTB engine sentinel contract.
