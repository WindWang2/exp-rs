// src/operators/runtime/tile_inference_engine.h — bounded tiled model inference.
//
// Raster → TilePlanner → windowed reads (halo/overlap-aware) → preprocessing
// (manifest contract) → batched forward passes on a shared runtime session →
// postprocessing (mask threshold, nodata reconstruction) → streaming tile
// writes → georeferenced result. Memory is O(batch × tile × bands), never
// O(raster): the input is never materialized whole and the output is written
// tile-by-tile through GdalStreamingOutput. Progress and cancellation are
// checked per tile batch.
#pragma once

#include "operators/runtime/model_runtime.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_context.h"

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// One fed input's verified grid identity (Platform 8.0 WP-C): recorded
/// AFTER the co-registration check passed, for result payloads and the
/// provenance sidecar. Evidence of what was CHECKED — never a claim that
/// warping happened (the runtime never warps implicitly).
struct GridProvenance
{
  std::string name;                 ///< feed/input contract name
  std::string path;                 ///< the raster that was actually fed
  std::vector<std::string> preparedFrom; ///< original sources when the caller
                                    ///< pre-aligned through the geospatial
                                    ///< seam, parallel to the fed frames
                                    ///< (empty = fed as-is)
  std::string crs;           ///< CRS authority string ("" = undeclared)
  bool crsVerified = false;  ///< true when the CRS was CHECKED equal to the
                             ///< primary feed's CRS; for the primary feed
                             ///< itself (the reference), true when declared
  int width = 0;
  int height = 0;
  int frames = 1;            ///< temporal frames fed under this grid identity
  // --- Platform 9.0 (M3) ------------------------------------------------------
  /// The EFFECTIVE preprocessing contract applied to this feed (per-input
  /// override when declared, else the global contract): normalize/scale/pad
  /// summary for payload + sidecar. Empty string = nothing recorded.
  std::string preprocessNote;
  /// Deterministic identity fingerprint of the fed raster (structure always;
  /// content digest when the file fits the size bound). Null when disabled.
  Json::Value fingerprint;
};

/// Geometry + counters describing one engine run (also feeds estimates).
struct TileInferenceStats
{
  int tileSize = 0;       ///< core tile edge used (px)
  int halo = 0;           ///< halo radius per side (px)
  int batchSize = 1;
  int tilesPlanned = 0;
  int tilesProcessed = 0;
  int tilesSkippedNoData = 0; ///< tiles whose forward pass was skipped (all core pixels nodata, #705)
  int batchReductions = 0;    ///< OOM ladder splits (batch halved, same tiles retried)
  int outBands = 0;       ///< model output channels written (all heads + uncertainty)
  int outWidth = 0;       ///< output raster width (== input width)
  int outHeight = 0;      ///< output raster height (== input height)
  /// Channel count per output head in band order (Platform 3.0 multi-head
  /// layout; the uncertainty band, when any, is counted in its head's entry).
  std::vector<int> headChannels;
  /// Grid provenance, one entry per fed input (Platform 8.0 WP-C): what the
  /// engine actually verified about each feed's grid before inference.
  /// Truthful — fields stay empty when the raster does not declare them.
  std::vector<GridProvenance> inputGrids;
};

/// Raster-task output mode (Platform 4.0, manifest `output.format`).
/// Probability keeps the historical float32 per-class stack; the derived
/// modes collapse the class planes of the FIRST head into ONE band.
enum class RasterOutputMode
{
  Probability, ///< "" | "probability": float32 class stack (default; the regression/embedding path)
  Labels,      ///< "labels": argmax → label raster (Byte ≤255 classes, else UInt16) + palette metadata
  Mask,        ///< "mask": binary 0/1 Byte (C==1: plane ≥ threshold; C>1: argmax ≠ 0)
  Confidence   ///< "confidence": float32 top-1 probability band
};

/// Optional knobs for one engine run (Platform 3.0, goal §10).
enum class TtaMode
{
  None,    ///< single forward pass per tile
  HFlip,   ///< average logits with the horizontal flip
  HVFlip,  ///< average logits with horizontal + vertical flips
};

/// Tile output blend mode (Platform 9.0 M5). Unset defers to the manifest's
/// `tiling.blend` (itself defaulting to None = the historical hard-edge
/// stitch). Feather averages overlapping tile windows with a cosine weight
/// ramp across the halo — seams between disagreeing tiles fade instead of
/// stepping. Detection vectors never blend (NMS owns overlap).
enum class TileBlend
{
  Unset,   ///< follow the manifest contract
  None,    ///< hard-edge stitch (historical default)
  Feather, ///< cosine-weighted overlap blending
};

struct TileInferenceRunOptions
{
  TtaMode tta = TtaMode::None;
  /// Hard cap on the batch size (0 = budget-aware auto sizing). Tests and the
  /// operator surface use this to pin memory behavior.
  int batchSizeOverride = 0;
  /// Derived-output selection; Probability = historical manifest behavior.
  RasterOutputMode outputMode = RasterOutputMode::Probability;
  /// Tile output blending (see TileBlend).
  TileBlend blend = TileBlend::Unset;
  // --- Platform 9.0 (M3) ------------------------------------------------------
  /// Compute a per-feed identity fingerprint (structure + bounded content
  /// digest) into GridProvenance. On by default — the provenance value is the
  /// point; disable only for pathological many-feed loops.
  bool computeFeedFingerprints = true;
  /// Content digest size bound per feed (bytes). Files above it record
  /// size+mtime with reason "file-too-large" instead of a digest — the
  /// fingerprint stays honest about what it verified, and cost stays bounded.
  std::int64_t fingerprintContentMaxBytes = 256LL * 1024 * 1024;
};

/// One named raster feed for multimodal / temporal models (Platform 7.0).
/// The name matches a manifest `inputs[]` contract entry; the paths are the
/// temporal frames in time order (exactly one for non-temporal inputs).
struct NamedRasterFeed
{
  std::string name;                ///< manifest input contract name ("" = positional)
  std::vector<std::string> paths;  ///< temporal frames, time-ordered (1 = static)
  std::vector<int> bands;          ///< 1-based band selection (empty = all)
  /// Platform 8.0 alignment provenance: when the caller pre-aligned these
  /// frames through the geospatial seam (raster_convert warp), the ORIGINAL
  /// source paths, parallel to @p paths (empty = fed as-is). Purely
  /// informational — the engine still verifies the fed grids and records
  /// both in the provenance sidecar.
  std::vector<std::string> preparedFrom;
  /// Platform 8.0 acquisition times (WP-D), parallel to @p paths, ISO 8601.
  /// Optional; when declared the engine enforces STRICTLY INCREASING time
  /// order (a misordered series is a typed refusal, never a silent sort).
  std::vector<std::string> timestamps;
  /// Platform 8.0 per-frame quality masks (WP-D), parallel to @p paths:
  /// single-band rasters where 0 = invalid pixel. Invalid pixels follow the
  /// input's NoData semantics (zeroed for the forward, excluded from the
  /// valid-coverage gate). Optional; every mask is grid+CRS-verified against
  /// the primary feed like any other input.
  std::vector<std::string> qualityMasks;
};

class TileInferenceEngine
{
  public:
    /**
     * @param model    catalog model (contracts: preprocess, tiling, postprocess)
     * @param runtime  loaded session (from ModelRuntimeRegistry — reused
     *                 across tiles AND across runs)
     */
    TileInferenceEngine( ModelInfo model, ModelRuntimePtr runtime );

    /**
     * Run tiled inference over the input raster.
     * @param bands  1-based band numbers to feed (empty = all bands)
     * @throws RSOperatorError on read/forward/write failure or cancellation.
     */
    TileInferenceStats run( const std::string &inputPath, const std::vector<int> &bands,
                            const std::string &outputPath, RSOperatorContext &context );

    /// Same contract with Platform-3.0 knobs (TTA, batch cap).
    TileInferenceStats run( const std::string &inputPath, const std::vector<int> &bands,
                            const std::string &outputPath, RSOperatorContext &context,
                            const TileInferenceRunOptions &options );

    // --- Platform 7.0: multimodal / temporal tiled inference -----------------
    /**
     * Run tiled inference over SEVERAL named raster feeds on ONE common grid.
     * The FIRST feed is the grid authority (tile grid + output geometry); every
     * other feed must be co-registered (same size and geotransform within
     * 1e-6) — misaligned feeds are a typed refusal pointing at the manifest's
     * input.alignment contract, never a silent warp (reprojection stays a
     * geospatial seam). Temporal feeds (contract temporalLength > 0) stack
     * their frames along the channel axis (N,(T·C),H,W — "channels" collapse).
     * Missing frames follow the contract's missing_timestep: refuse (typed) or
     * explicit zero-fill. The forward pass goes through inferNamed (named
     * tensor bind) and the OOM ladder keeps its exact batch-halving semantics.
     * Output: float32 probability stack (+ optional uncertainty band), atomic
     * publish — identical band semantics to run().
     * @throws RSOperatorError on any contract/read/forward/write failure.
     */
    TileInferenceStats runMultiInput( const std::vector<NamedRasterFeed> &feeds,
                                      const std::string &outputPath, RSOperatorContext &context,
                                      const TileInferenceRunOptions &options = {} );

    /// Same-grid contract check shared by validation and tests: empty string
    /// when both rasters carry the geometry, else a typed refusal naming both
    /// paths and the offending property (size / geotransform).
    static std::string gridMismatch( const std::string &primaryPath, int primaryW, int primaryH,
                                     const double *primaryGeoTransform, const std::string &otherPath,
                                     int otherW, int otherH, const double *otherGeoTransform );

    // --- Platform 8.0 WP-C: CRS-aware grid authority -------------------------
    /**
     * Co-registration verdicts are geodetic, not numeric: two rasters with
     * IDENTICAL geotransform numbers under DIFFERENT CRS describe different
     * places. This check compares the two CRS semantically (GDAL
     * OGRSpatialReference::IsSame) and returns an empty string when the pair
     * is acceptable, else a typed refusal naming both CRS.
     *
     * Policy (documented, deterministic):
     *  - both undeclared → acceptable (nothing to compare; historical data);
     *  - exactly one declared → acceptable when @p strictAlignment is false,
     *    refusal when true (alignment=reference demands VERIFIED
     *    co-registration — an unverifiable feed is a misaligned feed);
     *  - both declared and !IsSame() → refusal always (the 1e-6 geotransform
     *    check alone would silently pass cross-CRS feeds).
     * CRS strings may be WKT or any GDAL-parseable authority string; "" =
     * undeclared.
     */
    static std::string crsMismatch( const std::string &primaryPath, const std::string &primaryCrs,
                                    const std::string &otherPath, const std::string &otherCrs,
                                    bool strictAlignment );

    /// Effective tile geometry for a raster (manifest tiling contract +
    /// fixed graph input size fallback, engine floor of 16 px).
    static int effectiveTileSize( const ModelInfo &model );
    static int effectiveHalo( const ModelInfo &model );

    /// Platform 3.0: budget-aware batch size. Clamps the manifest batch by the
    /// VRAM budget (GPU) / a conservative RAM share (CPU) given the per-sample
    /// working set; never returns < 1.
    static int effectiveBatchSize( const ModelInfo &model,
                                   const ModelHardwareCapabilities &hw,
                                   int tilePx, int fedChannels );

    /// The declared uncertainty method for output heads ("" = none); one of
    /// "entropy" | "margin" (manifest output.uncertainty).
    static std::string uncertaintyMethod( const ModelInfo &model );

    /// Platform 4.0: manifest output.format → RasterOutputMode. Conflicts
    /// (uncertainty + derived mode, mask_threshold + labels, multi-head +
    /// derived mode) throw RSOperatorError with a loud reason — a silently
    /// ignored knob is the #646 failure class.
    static RasterOutputMode rasterOutputMode( const ModelInfo &model );

    /// Platform 3.0: uncertainty band for one tile's class-probability head.
    /// @a classPlanes are the head's C channel planes (already stitched to the
    /// core tile, logits or probabilities — softmax is applied here for
    /// entropy). "entropy" → softmax entropy in [0, ln C]; "margin" →
    /// top1 − top2 probability gap. Returns an empty Mat for C < 2.
    static cv::Mat headUncertainty( const std::vector<cv::Mat> &classPlanes,
                                    const std::string &method );

    // --- Manifest contract validators (#690 / #705) ---------------------------
    // Pure functions shared by run() and the unit tests: they return an empty
    // string when the contract holds, else a human-readable failure naming the
    // offending band / tensor / shape.

    /// input.dtype must match the actual GDAL type of EVERY band fed to the
    /// model (band 1 alone misses mixed-type rasters). @a bands are 1-based;
    /// @a bandDataType maps a 1-based band number to its GDAL data type.
    static std::string inputDTypeMismatch( const ModelInfo &model, const std::vector<int> &bands,
                                           const std::function<int( int )> &bandDataType );

    /// Every declared output.tensor_names entry must exist in the loaded
    /// graph. Skipped (returns empty) when nothing is declared or when the
    /// runtime cannot enumerate outputs (empty @a graphOutputNames).
    static std::string missingOutputTensor( const ModelInfo &model,
                                            const std::vector<std::string> &graphOutputNames );

    /// The writer emits float32 — a non-CV_32F output tensor would be
    /// bit-cast into garbage on disk (#690).
    static std::string outputTypeMismatch( int outputCvType, const std::string &tensorName );

    /// The raster head writes one channel per class: the declared classes
    /// count must match the probability tensor's channel count.
    static std::string classesChannelMismatch( const ModelInfo &model, int outputChannels,
                                               const std::string &tensorName );

    /// True when the pending batch holds at least one tile and every tile has
    /// zero valid (finite in all bands) core pixels — the forward pass can be
    /// skipped and NoData written directly (#705).
    static bool batchIsAllNoData( const std::vector<int> &validPixelCounts );

    // --- Platform 9.0 (M3): feed identity fingerprint ------------------------
    /**
     * Deterministic identity document for one fed raster:
     *   { width, height, band_count, band_dtypes[], geotransform[6],
     *     crs (display), selected_bands[], content {...} }
     * `content` is { "sha256", "bytes" } when the file fits
     * @p contentMaxBytes, else { "bytes", "mtime_utc", "reason":
     * "file-too-large" } — the fingerprint states EXACTLY what it verified
     * and never implies byte identity it did not check.
     * Throws RSOperatorError when the raster cannot be opened.
     */
    static Json::Value feedFingerprint( const std::string &path, const std::vector<int> &bands,
                                        std::int64_t contentMaxBytes );

  private:
    /// Resolved GDAL type of the manifest's input.dtype (-1 = undeclared);
    /// checked against every FED band after selection (#705.3).
    int m_declaredDtype = -1;
    ModelInfo m_model;
    ModelRuntimePtr m_runtime;
};

} // namespace sicnu::operators::runtime
