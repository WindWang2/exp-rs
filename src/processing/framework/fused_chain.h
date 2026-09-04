// fused_chain.h — Intermediate materialization elimination (Data Plane 3.0,
// Phase C). Plans and executes linear chains of elementwise operators as ONE
// streaming tile pipeline (tile → tile → tile → final) instead of writing an
// intermediate raster per step.
//
// Eligibility is deliberately conservative and FAIL-CLOSED:
//   - every step is an Operator kind whose operator id has a fused adapter
//     (bit-exact elementwise math, explicit parameter subset — adapters
//     return nullptr for anything outside the replicated semantics);
//   - the chain is linear: each step consumes exactly the previous step's
//     "output" port as its raster input, and no other step consumes any
//     intermediate output (fan-out ⇒ split ⇒ materialize that boundary);
//   - all steps share one raster grid (the executor validates).
//
// Identity/cache semantics: every logical step keeps its TaskCenter task and
// submission-time fingerprint. The head task dispatches the fused executor;
// when it completes, member steps are marked Completed with the TAIL payload,
// and only the TAIL step's fingerprint is eligible for the execution cache
// (its declared output exists; intermediate declared outputs deliberately do
// not, and the cache store refuses entries with no statable artifacts —
// fail-closed on both sides).
//
// Off by default; enable with SICNU_FUSED_CHAIN=1 until equivalence is
// proven on a deployment (see tests/test_fused_chain.cpp).
#pragma once

#include "operators/framework/rs_operator_context.h"

#include <json/json.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::workflow
{
struct WorkflowDefinition;
}

namespace sicnu::processing
{

/// One elementwise stage of a fused chain.
struct FusedStage
{
    /// 1-based bands of the SOURCE raster read for the HEAD stage; ignored
    /// for later stages (their input is the previous stage's output planes).
    std::vector<int> headInputBands;
    int outputBands = 1;
    /// GDAL output data type of the TAIL stage's raster (GDT_* as int);
    /// float32 for intermediate stages.
    int tailOutputDtype = 1; // GDT_Float32
    /// Kernel over one tile: inPlanes[s] is a width*height float plane
    /// (row-major); returns outputBands planes of the same extent.
    std::function<std::vector<std::vector<float>>( const std::vector<const float *> &inPlanes,
                                                   int width, int height )> kernel;
    /// Extra payload entries merged into the tail result (e.g.
    /// "thresholdUsed"). Recorded on the head task's result payload.
    Json::Value resultExtras;
};

/// Adapter lookup: operator id + canonical params → stage, or nullopt when
/// the operator/parameter combination is outside the replicated subset.
/// Adapters must not throw; unsupported ⇒ nullopt (chain simply not fused).
std::optional<FusedStage> makeFusedStageFor( const std::string &operatorId,
                                             const Json::Value &params );

/// A fusable linear run of definition steps (in execution order).
struct FusedChainPlan
{
    std::vector<std::string> stepIds;   ///< execution order, size >= 2
    std::string headStepId;             ///< dispatches the fused executor
    std::string tailStepId;             ///< owns the final output path
    std::vector<FusedStage> stages;     ///< one per step, execution order
    std::vector<Json::Value> stageParams; ///< one per step, execution order
    Json::Value tailParams;             ///< tail step's params (output path etc.)
    std::string inputPath;              ///< head step's raster input (resolved path)
};

/// Detect the maximal fusable linear chain inside @p def (the FIRST one found
/// in topological order; remaining steps execute normally — repeated
/// submission fuses the next chain). Returns a plan with an empty stepIds
/// when nothing is fusable.
FusedChainPlan planFusedChain( const sicnu::workflow::WorkflowDefinition &def );

/// Execute a fused chain as one streaming pipeline. Throws
/// RSOperatorError-shaped std::runtime_error subclasses on failure; honors
/// cooperative cancellation via @p context. Returns the tail result payload:
/// {"output": tailOutputPath, "fused": [stepIds...], ...resultExtras}.
Json::Value executeFusedChain( const FusedChainPlan &plan,
                               sicnu::operators::RSOperatorContext &context );

} // namespace sicnu::processing
