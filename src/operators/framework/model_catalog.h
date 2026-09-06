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
 * Detection decode contract — manifest v4 `output.detection` section
 * (Platform 4.0). Declares how the engine decodes a raw detection head
 * tensor into georeferenced vector output; its presence is what marks a
 * model as detection-executable (enabling NMS/threshold vocabulary that
 * stays rejected for non-detection models).
 */
struct ModelDetectionContract
{
  /// Head layout vocabulary:
  ///  - "xywh_objectness": per candidate (cx, cy, w, h, obj, cls0..clsK-1)
  ///    (YOLOv5-style export; final score = obj * max(cls)).
  ///  - "xywh_class_scores": per candidate (cx, cy, w, h, cls0..clsK-1)
  ///    (YOLOv8-style export; final score = max(cls)).
  std::string layout = "xywh_objectness";
  /// Tensor shape vocabulary: "channels_first" (1, C, N), "channels_last"
  /// (1, N, C), or "auto" (C <= N heuristic, documented, deterministic).
  std::string tensorLayout = "auto";
  double confThreshold = 0.25;  ///< score gate (detection kept when >=)
  double nmsIou = 0.45;         ///< whole-raster NMS / tile-dedup IoU, (0, 1]
  int maxDetections = 100000;   ///< bounded accumulation guard across tiles
  std::vector<std::string> classes;

  /// Vocabulary + range validation (empty = ok).
  std::string validate() const;
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
  /// Platform 4.0 output format for RASTER tasks (segmentation/regression/
  /// embedding); empty = the historical probability-stack behavior:
  ///  - "" | "probability": float32 per-class stack (default)
  ///  - "labels": argmax → byte label raster + palette
  ///  - "mask": threshold → binary 0/1 raster
  ///  - "confidence": top-1 probability band (argmax tasks)
  std::string format;
  bool detectionDeclared = false;    ///< true when `output.detection` is present
  ModelDetectionContract detection;  ///< meaningful only when detectionDeclared
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
  /// Platform 4.0 device token: "cpu" | "cuda" | "cuda:N" | "auto".
  /// Empty = "auto" (legacy behavior: cuda when gpu && available, else cpu).
  std::string device;
};

/**
 * One registered model runtime entry (ADR 0122). Deserialized from
 * models/<name>/model.json manifests; weight files are referenced by path
 * and are not shipped with the repository.
 */
struct ModelInfo {
  std::string name;        ///< Unique id, e.g. "sam-building"
  // --- Platform 4.0 identity (manifest `id` / `model_version` / `license` /
  // `source` / `manifest_version`). All optional; absent fields keep the
  // documented defaults, so every v1/v2/v3 manifest parses unchanged.
  std::string id;          ///< Stable catalog identity. Empty = @p name. Callers
                           ///< (GUI/CLI/Workflow/Pi/SDK) reference models by this id,
                           ///< never by weight file path. Unique within the catalog.
  std::string modelVersion;///< Model version string. Empty = "0".
  std::string license;     ///< SPDX expression or license name. Empty = unspecified.
  std::string source;      ///< Provenance origin (download source / URL).
  int manifestVersion = 0; ///< Declared `manifest_version` (1..4); 0 = not declared,
                           ///< the effective version is inferred from manifest shape.
  std::string contentDigest; ///< SHA-256 hex of the resolved artifact BYTES, computed
                             ///< at catalog load (or acquire for ad-hoc models) whether
                             ///< or not a checksum is declared. "" = no artifact. This
                             ///< is the session-identity anchor: same path with different
                             ///< bytes yields a different digest and never shares a session.
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

  /// Effective stable identity: the declared `id`, falling back to `name`
  /// (manifests without an id keep their historical identity).
  std::string stableId() const { return id.empty() ? name : id; }
  /// "id@version" identity tag for payloads and logs (version defaults to "0").
  std::string identityTag() const;

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

    // --- Platform 4.0: authoritative registry surface -------------------------
    // The file scan (models/<name>/model.json) stays the DISCOVERY channel;
    // these calls make the catalog a full registry: programmatic registration
    // (plugins/tests), de-registration, inspection, pure validation, version
    // resolution and health. GUI/CLI/Workflow/Pi/SDK reference models by
    // stable id through find()/resolve() — never by weight path.

    /// Register a manifest document programmatically (session-scoped, not
    /// written to disk). The entry shadows scanned manifests with the same
    /// id/name until unregister() or process end. @a error receives the parse
    /// or validation failure. Returns false without registering on error.
    bool registerManifestJson( const std::string &json, const std::string &source,
                               std::string *error = nullptr );

    /// Remove a registered entry (programmatic or scanned). For scanned
    /// manifests the removal lasts until the next reload(). Returns false
    /// when no such model exists.
    bool unregister( const std::string &idOrName );

    /// Full registry record for one model: manifest JSON (as parsed, with
    /// identity/readiness/digest), plus health block. Empty Json on miss.
    Json::Value inspect( const std::string &idOrName ) const;

    /// Pure manifest validation WITHOUT registering: returns the issue list
    /// (empty = valid). Checks JSON well-formedness, required fields and the
    /// full contract sanity (the same checks a scan applies).
    std::vector<std::string> validateManifestJson( const std::string &json ) const;

    /// Resolve "id" or "id@version" (Platform 4.0 identity reference).
    /// "id" matches the sole entry with that id (nullopt with @a error set
    /// when several versions exist); "id@version" matches exactly; an empty
    /// version part ("id@") resolves the lexicographically latest version.
    std::optional<ModelInfo> resolve( const std::string &idVersionRef,
                                      std::string *error = nullptr ) const;

    /// Runtime health for one model: readiness, content digest availability,
    /// provider presence and the device verdict. Never throws; "ok" mirrors
    /// the overall verdict. Empty Json on unknown id.
    Json::Value health( const std::string &idOrName ) const;

  private:
    ModelCatalog() = default;
    void ensureLoadedLocked() const;
    /// find() helper over registered-then-scanned entries honoring
    /// unregister(). Caller holds the catalog mutex.
    std::optional<ModelInfo> findLocked( const std::string &idOrName ) const;
    struct VerifiedArtifact;
    bool verifyArtifactLocked( ModelInfo &info ) const;

    std::string mDirectory;
    mutable bool mLoaded = false;
    mutable std::vector<ModelInfo> mModels;
    mutable std::vector<ModelCatalogIssue> mIssues;
    mutable std::vector<VerifiedArtifact> mVerified; ///< checksum cache (path, size, mtime)
    /// Platform 4.0 registry overlay: programmatic entries (registerManifestJson)
    /// shadow scanned manifests with the same id/name for this session.
    std::vector<ModelInfo> mRegistered;
    /// Ids/names removed via unregister() (scanned entries reappear on reload).
    std::vector<std::string> mUnregistered;
};

} // namespace sicnu::operators
