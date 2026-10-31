// src/operators/runtime/inference_plan.h — explainable inference plan and
// post-run quality summary (R25-B feature 2, schema sicnu.inference.plan/1
// and sicnu.inference.quality/1).
//
// planModelInference() is the read-only twin of runModelInference(): it runs
// the SAME gate sequence (input existence → model resolution → task-intent
// gate → runtime readiness → lane incompatibilities → contract wiring →
// feature cube → device token → head/output-mode contract → EO preflight →
// blend contract) and then computes the tile grid, batch plan and a working-
// set estimate with the engine's own static planners. Nothing executes, no
// session is acquired, no output is written: every typed refusal the run
// would raise appears in the plan as an "error" finding instead.
//
// summarizeInferenceQuality() projects a finished ModelExecutionResult into
// one typed summary: tile coverage and grid completeness, NoData skips, OOM
// batch reductions, class distribution (labels), detection suppression and
// EO preflight presence — the numbers a consumer needs to judge whether an
// inference product is trustworthy, instead of re-deriving them from the
// raw payload.
#pragma once

#include "operators/runtime/model_execution_service.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::operators::runtime
{

/// One typed plan/quality observation. Same shape as the import preflight
/// findings so consumers render one grammar everywhere.
struct InferencePlanFinding
{
  std::string code;       ///< stable machine code ("plan.*" / "quality.*")
  std::string severity;   ///< "error" | "warning" | "info"
  std::string message;
  std::string remediation; ///< actionable next step; empty when none exists

  Json::Value toJson() const;
};

/// Grid facts for one fed input.
struct InferencePlanInput
{
  std::string path;    ///< primary path (feed name for multi-input models)
  std::string name;    ///< feed name; "" on the single-input path
  int width = 0;
  int height = 0;
  int bandCount = 0;   ///< bands the raster carries
  int fedBands = 0;    ///< bands the request actually feeds (selection applied)
  bool crsDeclared = false;
  int frames = 1;      ///< temporal feed length (paths per feed)

  Json::Value toJson() const;
};

struct InferencePlan
{
  inline static constexpr int kSchemaVersion = 1;

  enum class Verdict
  {
    Ready,
    Refused
  };

  Verdict verdict = Verdict::Refused;

  // --- model identity -------------------------------------------------------
  std::string modelIdentityTag;  ///< "id@version"
  std::string modelStableId;
  std::string modelTask;         ///< manifest task token
  std::string canonicalTask;     ///< canonical EO task vocabulary
  std::string framework;
  std::string contentDigest;
  bool isEnsemble = false;
  int ensembleMembers = 0;

  // --- runtime / device -----------------------------------------------------
  bool runtimeReady = false;
  std::string runtimeReadinessName; ///< ModelReadiness name ("ready", ...)
  std::string runtimeReason;        ///< refusal/diagnostic reason ("" when ready)
  std::string requestedDevice;      ///< "" = manifest default (auto semantics)
  std::string manifestDevice;

  // --- inputs ---------------------------------------------------------------
  std::vector<InferencePlanInput> inputs;

  // --- tiling ---------------------------------------------------------------
  int tileSize = 0;
  int halo = 0;
  int gridX = 0;         ///< tiles per row
  int gridY = 0;         ///< tiles per column
  int tilesPlanned = 0;
  int batchSize = 1;
  int batchesEstimated = 0;    ///< ceil(tiles / batch)
  /// Channels the batch budget accounts (single input: fed bands; multi
  /// input: max over feeds of bands × DECLARED temporal length — exactly the
  /// engine's admission accounting).
  int fedChannels = 0;
  /// Working-set estimate for ONE batch: batchSize × tile² × fedChannels ×
  /// float32 in+out. Labeled estimate — actual peak depends on the provider.
  double batchWorkingSetMiB = 0.0;

  // --- output ---------------------------------------------------------------
  std::string outputPath;
  RasterOutputMode outputMode = RasterOutputMode::Probability;
  bool headContractOk = true;
  std::string headContractNote; ///< "" or the refusal the run would raise
  bool detectionLane = false;   ///< vector output via the detection engine
  bool sceneClassification = false;

  // --- EO contract ----------------------------------------------------------
  bool eoApplicable = false;
  bool eoEnforcedOk = true;     ///< false when the run WILL raise the refusal
  int eoAdvisoryCount = 0;

  std::vector<InferencePlanFinding> findings;

  std::string verdictString() const;
  Json::Value toJson() const;
  /// First finding with this code, or nullptr.
  const InferencePlanFinding *findingCode( const std::string &code ) const;
};

/// Read-only planning pass over one execution request. Never throws: every
/// refusal lands in the findings list and the verdict is Refused.
InferencePlan planModelInference( const ModelExecutionRequest &request );

/// Same planning pass for callers that already resolved the model (the
/// ensemble engine, tests with injected providers): skips reference
/// resolution and plans @p resolvedModel directly. The request's input/output
/// fields still drive the grid and output checks.
InferencePlan planModelInference( const ModelExecutionRequest &request,
                                  const ModelInfo &resolvedModel );

// ---------------------------------------------------------------------------
/// Post-run quality summary over a finished execution result.
struct InferenceQualitySummary
{
  inline static constexpr int kSchemaVersion = 1;

  // --- tile coverage --------------------------------------------------------
  int tilesPlanned = 0;
  int tilesProcessed = 0;
  int tilesSkippedNoData = 0;
  double tileCoverageRatio = 0.0;  ///< processed / planned (planned>0)
  /// processed + skippedNoData == planned — the tail-batch invariant
  /// (r24-B WP-B2). False means tiles were lost, not skipped.
  bool gridComplete = false;
  int batchReductions = 0;         ///< OOM ladder splits that fired

  // --- product --------------------------------------------------------------
  int outBands = 0;
  int outWidth = 0;
  int outHeight = 0;
  bool isDetection = false;
  int rawDetections = 0;
  int detectionsKept = 0;
  double suppressionRatio = 0.0;   ///< 1 - kept/raw (raw>0), else 0

  // --- class distribution (Labels/Mask products; empty otherwise) -----------
  bool hasClassDistribution = false;
  std::vector<long long> classPixelCounts;
  double classifiedPixelRatio = 0.0; ///< sum(counts) / (width×height)
  int dominantClass = -1;

  // --- EO contract ----------------------------------------------------------
  bool eoPreflightPresent = false;
  int eoAdvisoryCount = 0;

  std::vector<InferencePlanFinding> notes; ///< observations ("quality.*")

  Json::Value toJson() const;
};

/// Project a finished result. Throws RSOperatorError(InvalidInputData) when
/// the result carries neither raster nor detection stats (nothing to
/// summarize — refusing beats fabricating a zero summary).
InferenceQualitySummary summarizeInferenceQuality( const ModelExecutionResult &result );

} // namespace sicnu::operators::runtime
