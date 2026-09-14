// src/agent/harness/workflow_planner.h
#pragma once

//
// Scientific Workflow Compiler 10.0 (ADR 0149): the staged planner.
//
//   intent → grounding → candidates → IR → analysis → repair → lower → plan
//
// Every stage is a pure, checkable step reported to the caller; alternatives
// carry why/why-not with deterministic ranking; missing facts and limitations
// are first-class outputs. The LLM reasons upstream (it authors the IR or
// picks an alternative); everything deterministic in the pipeline is code.
//
// `compileWorkflow` is the PURE core: it consumes precomputed fact documents
// and never touches files or registries with side effects. The
// `harness:compile_workflow` tool wraps it with live grounding through the
// existing `spatial:understand` seam — GUI/CLI/MCP/Pi all see one compiler.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "agent_plan.h"
#include "harness_error.h"
#include "workflow_analysis.h"
#include "workflow_ir.h"
#include "workflow_repair.h"

namespace sicnu::agent::harness {

struct PlannerStageReport
{
    std::string stage;    ///< "parse" | "ground" | "candidates" | "analysis" | "repair" | "lower"
    std::string status;   ///< "ok" | "skipped" | "fail"
    std::string summary;
    Json::Value details{Json::objectValue};

    Json::Value toJson() const;
};

struct CompiledWorkflow
{
    WorkflowIr ir;                            ///< normalized (post-repair) IR
    IrAnalysis analysis;                      ///< the authoritative (final) analysis
    std::vector<IrRepairRecord> repairs;
    std::vector<IrRefusal> refusals;
    AgentPlan plan;                           ///< lowered plan (meaningful on success)
    HarnessError planError;                   ///< set when lowering/compilation failed
    std::string workflowJson;                 ///< engine JSON ("" when invalid or blocked)
    bool executionBlocked = false;            ///< analysis verdict != ok — do not execute
    std::vector<PlannerStageReport> stages;
    Json::Value alternatives{Json::arrayValue}; ///< candidate methods with why/why-not
    Json::Value missingFacts{Json::arrayValue}; ///< what grounding could not supply
    Json::Value limitations{Json::arrayValue};  ///< union of serving-capability limits

    std::string verdict() const { return analysis.verdict; }
    Json::Value stagesJson() const;
};

struct CompileWorkflowRequest
{
    std::string goal;
    std::string intent;
    /// Optional recipe path: instantiate the recipe, convert its plan to IR,
    /// then compile. `recipeBindings` = {slots, params, output_dir, outputs,
    /// preset} (RecipeCatalog::instantiateRecipe contract).
    std::string recipeId;
    Json::Value recipeBindings{Json::objectValue};
    /// Optional agent-authored IR document (takes precedence over recipeId).
    Json::Value irDoc;
    /// Deterministic fact seam: slot name -> DatasetUnderstanding document.
    /// Slots without facts here degrade the analysis to warnings (documented
    /// per check); the TOOL wrapper grounds live through spatial:understand.
    Json::Value inputFacts{Json::objectValue};
    /// Optional model contracts: model id -> contract document.
    Json::Value modelContracts{Json::objectValue};
    bool applyRepairs = true;
};

/// The staged compiler. Deterministic: same request -> byte-identical result.
CompiledWorkflow compileWorkflow( const CompileWorkflowRequest &request, HarnessError &error );

/// Converts an AgentPlan v2 document (the recipe/LLM surface) into an IR.
/// Params/wiring map 1:1; plan inputs become slots; verification and role are
/// preserved. Returns false with a typed error when the plan is unreadable.
bool planToWorkflowIr( const Json::Value &planDoc, WorkflowIr &ir, HarnessError &error );

/// Lowers a normalized IR to an AgentPlan v2. `resolvedSlotPaths` maps slot
/// names to concrete dataset paths (from grounding facts) — slot wiring needs
/// them at execution time. The plan's raw document carries a `workflow_ir`
/// provenance block (ir id, fingerprint, repairs, refusals) that
/// execute_plan binds into the run context.
bool lowerIrToAgentPlan( const WorkflowIr &ir, const Json::Value &resolvedSlotPaths,
                         AgentPlan &plan, HarnessError &error );

/// Serializes an AgentPlan v2 back to its wire document — the executable
/// artifact callers feed to harness:execute_plan (with the compiler
/// provenance block preserved verbatim).
Json::Value agentPlanToDocument( const AgentPlan &plan );

/// Registers `harness:compile_workflow` on the SpatialToolRegistry. Idempotent.
void registerWorkflowPlannerTools();

} // namespace sicnu::agent::harness
