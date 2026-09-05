// src/operators/framework/model_catalog.h
#pragma once

#include "operators/framework/model_readiness.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators {

/**
 * Artifact (weight file) contract — manifest v2 `artifact` section. All fields
 * optional; `path` falls back to the legacy top-level `path` string.
 */
struct ModelArtifactContract
{
  std::string path;        ///< Weight file, relative to the manifest or absolute
  std::string checksum;    ///< Hex digest; "sha256:<hex>" or bare hex (algorithm inferred)
  unsigned long long sizeBytes = 0; ///< Declared size; 0 = unchecked
};

/**
 * Model input contract — manifest v2 `input` section (object form) and, since
 * v3, one entry of the `inputs` array. The legacy string form
 * ("input": "raster") only fills ModelInfo::inputType.
 */
struct ModelInputContract
{
  std::string name;        ///< Blob/input name for multi-input models ("" = default single input)
  std::string dataType;    ///< "raster" (only supported kind today)
  std::string dtype;       ///< Expected tensor dtype, e.g. "float32" ("" = unspecified)
  std::string layout;      ///< "NCHW" (default) — the blob layout fed to the model
  std::vector<std::string> bandRoles; ///< e.g. ["Red","Green","Blue","NIR"]
  int width = 0;           ///< Fixed input width when the graph requires one (0 = dynamic)
  int height = 0;          ///< Fixed input height (0 = dynamic)
  int temporalLength = 0;  ///< Frames per inference for THIS input (0 = single frame)
  std::string temporalCollapse = "channels"; ///< How T frames collapse: "channels" feeds N,(T·C),H,W
};

/**
 * Preprocessing contract — manifest v2 `preprocess` section. Executed by the
 * tile inference engine between the GDAL window read and the model blob.
 */
struct ModelPreprocessContract
{
  std::string normalize;   ///< "none" (default) | "linear" (x*scale) | "mean_std" ((x-mean)/std*scale)
  std::vector<double> mean;   ///< Per-channel means (mean_std)
  std::vector<double> stdv;   ///< Per-channel standard deviations (mean_std)
  double scale = 1.0;         ///< Multiplicative scale applied last (linear & mean_std)
  std::string resize;         ///< "none" (default) | "to_input" (resize each tile to input.width/height)
  std::string interpolation;  ///< "bilinear" (default) | "nearest"
  std::string nodataPolicy;   ///< "zero" (default): non-finite input pixels become 0 before the model
};

/**
 * Tiling contract — manifest v2 `tiling` section. Drives the tile inference
 * engine geometry; `supported` also mirrors the legacy supportsTiling field.
 */
struct ModelTilingContract
{
  bool supported = true;
  int tileSize = 0;    ///< Preferred tile size in px (0 = engine default)
  int overlap = 0;     ///< Adjacent-tile overlap in px (engine reads halo = overlap/2 each side)
  int halo = 0;        ///< Explicit halo radius in px (takes precedence over overlap/2)
  int batchSize = 1;   ///< Tiles batched into one forward pass
};

/**
 * Output contract — manifest v2 `output` section (object form). The legacy
 * string form only fills type.
 */
struct ModelOutputContract
{
  std::string type;         ///< "raster" | "polygon" | "vector" | ...
  std::vector<std::string> tensorNames;
  std::vector<std::string> classes;
  double threshold = -1.0;  ///< Detection/confidence threshold (<0 = none)
  std::string uncertainty = "none"; ///< "none" | "entropy" | "margin" — adds a confidence band computed from that head's channels
};

/**
 * Postprocessing contract — manifest v2 `postprocess` section.
 */
struct ModelPostprocessContract
{
  bool nms = false;             ///< Non-maximum suppression (detection models)
  double maskThreshold = -1.0;  ///< Probability→binary mask threshold (<0 = keep probabilities)
  bool polygonize = false;      ///< Chain mask→polygon conversion (gdal:polygonize)
  double simplify = 0.0;        ///< Geometry simplification tolerance (map units)
};

/**
 * Runtime contract — manifest v2 `runtime` section; the legacy flat fields
 * (`gpu`, `estimated_vram_mb`, ...) parse into the same structure.
 */
struct ModelRuntimeContract
{
  bool gpu = false;
  bool cpuFallback = true;
  int estimatedRamMb = 0;
  int estimatedVramMb = 0;
};

/**
 * One registered model runtime entry (ADR 0122). Deserialized from
 * models/<name>/model.json manifests; weight files are referenced by path
 * and are not shipped with the repository.
 */
struct ModelInfo {
  std::string name;        ///< Unique id, e.g. "sam-building"
  std::string task;        ///< Task family: segmentation | classification | detection | ...
  std::string inputType;   ///< Input contract, e.g. "raster"
  std::string outputType;  ///< Output contract, e.g. "polygon" | "raster"
  std::string framework;   ///< Runtime, e.g. "onnx"
  std::string path;        ///< Local weight file path as written in the manifest (optional until downloaded)
  bool gpu = false;        ///< Whether a GPU is expected/required
  double accuracy = -1.0;  ///< Optional benchmark accuracy in [0, 1] (<0 = unreported)
  std::string description;
  std::vector<std::string> tags;
  std::string sourceManifest; ///< Manifest file this entry was loaded from

  // Extended domain and runtime capabilities (ADR 0122 / Harness deepening)
  std::vector<std::string> sensors;            ///< e.g. ["Sentinel-2", "Landsat-8", "GF-2"]
  std::vector<std::string> supportedBandRoles; ///< e.g. ["Red", "Green", "Blue", "NIR"]
  // --- Multimodal / temporal data contract (goal §9, aligned with the
  // TemporalSceneRef forward seam §11). Optional; empty = optical/unknown,
  // single-scene. Lets "inspect dataset → derive contract → rank model"
  // filter SAR / time-series models without memorizing names.
  std::vector<std::string> modalities;        ///< ["optical"] (default) | "sar" | "dem" | "auxiliary"
  std::vector<std::string> polarizations;     ///< SAR models: ["VV","VH","HH","HV"]
  int temporalLength = 0;                     ///< Frames per inference (0 = single-scene model)
  std::string radiometricState;               ///< Expected radiometry: "dn" | "toa_reflectance" | ...
  double minResolutionMeters = -1.0;          ///< Min recommended spatial resolution (m)
  double maxResolutionMeters = -1.0;          ///< Max recommended spatial resolution (m)
  int estimatedVramMb = 0;                     ///< VRAM required/recommended when GPU=true
  bool supportsTiling = true;                  ///< Whether model supports sliding-window tiling
  bool cpuFallback = true;                     ///< Whether CPU inference fallback is supported

  // Manifest v2 inference contracts (all optional; absent sections keep defaults)
  ModelArtifactContract artifact;
  ModelInputContract input;  ///< Legacy single-input mirror — always kept in sync with inputs[0]
  /// Manifest v3: ALL declared inputs in order. v1/v2 manifests parse their
  /// `input` section into inputs[0]; when both `inputs` and `input` are
  /// declared, `inputs` wins and the mirror above is filled from inputs[0].
  std::vector<ModelInputContract> inputs;
  ModelPreprocessContract preprocess;
  ModelTilingContract tiling;
  ModelOutputContract output;
  ModelPostprocessContract postprocess;
  ModelRuntimeContract runtime;

  // Real availability state computed at load time (catalog-static half: the
  // runtime layer adds UnsupportedRuntime/IncompatibleHardware on top).
  // Default Ready is the "not yet verified" sentinel for parseManifest: load
  // bumps it to InvalidManifest only when a contract check fires, otherwise
  // ensureLoadedLocked delegates to verifyArtifactLocked which sets Ready /
  // MissingArtifact / ChecksumMismatch.
  ModelReadiness readiness = ModelReadiness::Ready;
  std::string readinessReason;      ///< Human-readable explanation when not Ready
  std::string resolvedArtifactPath; ///< Absolute artifact path (manifest-dir resolved)

  Json::Value toJson() const;
};

/**
 * Criteria used by the Agent Harness / Pi to rank candidate models.
 */
struct ModelQueryCriteria {
  std::string task;
  std::string sensor;
  std::vector<std::string> bandRoles;
  double resolutionMeters = -1.0;
  bool gpuAvailable = false;
  int maxVramMb = 0;
};

/**
 * Model candidate evaluated against specific task/data criteria.
 */
struct ModelCandidate {
  ModelInfo model;
  double score = 0.0;           ///< Composite compatibility score in [0.0, 1.0]
  bool compatible = true;       ///< Whether the model satisfies hard criteria
  std::vector<std::string> matchReasons;
  std::vector<std::string> incompatibilityReasons;

  Json::Value toJson() const;
};

/**
 * A catalog-level diagnostic: a manifest that could not be indexed (bad JSON,
 * missing name, duplicate id). Surfaced via ModelCatalog::issues() so callers
 * can explain why an expected model is absent.
 */
struct ModelCatalogIssue {
  std::string manifestPath;
  std::string message;
};

/**
 * ModelCatalog — scans a directory of model manifests (models/<name>/
 * model.json) so agents and the inference operator can discover available
 * model runtimes by task and contract (ADR 0122 step 7). Resolution order
 * for the default directory: $SICNU_MODELS_DIR, <cwd>/models,
 * <application dir>/../models.
 */
class ModelCatalog {
  public:
    static ModelCatalog &instance();

    /// Explicitly overrides the scanned directory and reloads.
    void setDirectory( const std::string &dir );

    /// Currently scanned directory (defaultModelsDirectory() until overridden).
    std::string directory() const;

    /// Default models directory resolution (env → cwd → app-relative).
    static std::string defaultModelsDirectory();

    /// (Re)reads every models/<name>/model.json under the directory.
    /// Missing directory yields an empty catalog, not an error. Artifact
    /// checksums are (re)verified here; digests are cached per
    /// (path, size, mtime) so unchanged weights are not re-hashed.
    void reload();

    std::vector<ModelInfo> models() const;
    std::vector<ModelInfo> modelsByTask( const std::string &task ) const;
    std::optional<ModelInfo> find( const std::string &name ) const;
    std::vector<ModelCandidate> rankModels( const ModelQueryCriteria &criteria ) const;

    /// Diagnostics from the last load: unparseable manifests, missing names,
    /// duplicate model ids. Empty when the catalog loaded cleanly.
    std::vector<ModelCatalogIssue> issues() const;

    /// Resolve a model reference (catalog name or direct file path) to a
    /// ready-to-use artifact path. Returns nullopt when the reference is
    /// neither an existing file nor a catalog entry; @a error receives the
    /// readiness explanation for catalog entries that are not ready.
    static std::optional<std::string> resolveArtifactPath( const std::string &modelReference,
                                                           std::string *error = nullptr );

  private:
    ModelCatalog() = default;
    void ensureLoadedLocked() const;
    struct VerifiedArtifact;
    bool verifyArtifactLocked( ModelInfo &info ) const;

    std::string mDirectory;
    mutable bool mLoaded = false;
    mutable std::vector<ModelInfo> mModels;
    mutable std::vector<ModelCatalogIssue> mIssues;
    mutable std::vector<VerifiedArtifact> mVerified; ///< checksum cache (path, size, mtime)
};

} // namespace sicnu::operators
