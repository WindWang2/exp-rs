// src/agent/harness/plan_tools.cpp
#include "plan_tools.h"

#include "agent_plan.h"
#include "contracts/spatial_contracts.h"
#include "harness_verification.h"
#include "scientific_preflight.h"
#include "spatial_tools/spatial_tool.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QFileInfo>

#include <algorithm>
#include <optional>
#include <string>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

Json::Value refsProperty()
{
  Json::Value refs( Json::objectValue );
  refs["type"] = "array";
  refs["description"] = "Named inputs: [{\"name\": \"primary\", \"ref\": \"<asset ref>\"}].";
  Json::Value items( Json::objectValue );
  items["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value name( Json::objectValue );
  name["type"] = "string";
  props["name"] = name;
  Json::Value ref( Json::objectValue );
  ref["type"] = "string";
  ref["description"] = "Dataset reference: asset-N id, UUID, path, or display name.";
  props["ref"] = ref;
  items["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "name" );
  required.append( "ref" );
  items["required"] = required;
  refs["items"] = items;
  return refs;
}

/// Reads run state + verifies declared/final outputs (Phase 9). Returns the
/// bounded structured result document shared by harness:run_status and
/// harness:execute_plan.
Json::Value runResultDocument( const std::shared_ptr<sicnu::workflow::WorkflowRun> &run,
                               const AgentPlan *plan );

/// Phase 10: final map confirmation. Verifies the composed map output when the
/// plan declares one: target layout presence, MapSpec preflight/repair loop,
/// and export gating. Bounded and deterministic — reuses the cartography
/// tools through the registry, no second layout engine.
Json::Value confirmMapOutput( const AgentPlan &plan, const Json::Value &stepsDoc );

class PreflightTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:preflight"; }
    std::string displayName() const override { return "Scientific Preflight"; }
    std::string description() const override
    {
      return "Deterministic scientific preflight (Phase 5): validates that the "
             "resolved inputs are scientifically fit for the intent BEFORE any "
             "execution — NDVI (NIR/Red roles, radiometry, NoData), change "
             "(grid/CRS/resolution/radiometry pair checks), sar_change "
             "(modality, polarization, calibration domain, grid), classify "
             "(training samples), phenology (ordering, band roles). Blocked "
             "means the plan is refused — fix the inputs, never re-try prose.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "preflight", "science", "validation" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value intent( Json::objectValue );
      intent["type"] = "string";
      intent["description"] = "ndvi | change | sar_change | classify | phenology (or empty).";
      props["intent"] = intent;
      props["inputs"] = refsProperty();
      Json::Value required( Json::arrayValue );
      required.append( "intent" );
      required.append( "inputs" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["preflight"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string intent = input.get( "intent", "" ).asString();
      if ( !isKnownIntent( intent ) )
        return SpatialToolResult::failure( "Unknown intent: " + intent,
                                           error_codes::kInvalidParameter, "validation" );
      if ( !input.isMember( "inputs" ) || !input["inputs"].isArray() )
        return SpatialToolResult::failure( "missing array parameter 'inputs'",
                                           error_codes::kInvalidParameter, "validation" );
      const PreflightOutcome outcome = preflightIntent( intent, input["inputs"] );
      Json::Value out( Json::objectValue );
      out["preflight"] = outcome.toJson( intent );
      Json::Value summaries( Json::arrayValue );
      // preflightIntent re-resolves internally; re-run resolution here only to
      // echo typed errors per slot without re-inspecting.
      for ( const auto &entry : input["inputs"] )
      {
        Json::Value summary( Json::objectValue );
        summary["name"] = entry.get( "name", "" ).asString();
        summaries.append( summary );
      }
      out["inputs"] = summaries;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class PlanTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:plan"; }
    std::string displayName() const override { return "Plan Validate & Compile"; }
    std::string description() const override
    {
      return "Validates an AgentPlan v2 document and compiles it to the "
             "authoritative WorkflowDefinition JSON (single execution engine). "
             "Returns typed validation issues, the compiled workflow JSON, and "
             "resource estimates (per-step and aggregate RAM). Does not run "
             "anything — use harness:execute_plan for that.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "plan", "workflow", "compile", "estimates" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "AgentPlan v2 document (kind: execution_plan).";
      props["plan"] = plan;
      Json::Value required( Json::arrayValue );
      required.append( "plan" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["plan"] = Json::Value( Json::objectValue );
      props["compilable"] = Json::Value( Json::objectValue );
      props["workflow_json"] = Json::Value( Json::objectValue );
      props["estimates"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "plan" ) || !input["plan"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'plan'",
                                           error_codes::kInvalidParameter, "validation" );
      AgentPlan plan;
      HarnessError error;
      if ( !readAgentPlan( input["plan"], plan, error ) )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      Json::Value out( Json::objectValue );
      out["plan"] = planSummary( plan );
      const std::vector<AgentPlanIssue> issues = validateAgentPlan( plan );
      if ( !issues.empty() )
      {
        Json::Value arr( Json::arrayValue );
        for ( const AgentPlanIssue &issue : issues )
        {
          Json::Value entry( Json::objectValue );
          entry["code"] = issue.error.code;
          entry["summary"] = issue.error.summary;
          if ( !issue.stepId.empty() )
            entry["step_id"] = issue.stepId;
          entry["repairable"] = issue.repairable;
          arr.append( entry );
        }
        out["issues"] = arr;
        out["compilable"] = false;
        return SpatialToolResult::ok( std::move( out ) );
      }
      out["compilable"] = true;
      HarnessError compileError;
      const std::string workflowJson = compilePlanToWorkflowJson( plan, compileError );
      if ( workflowJson.empty() )
        return SpatialToolResult::failure( compileError.summary, compileError.code, "validation" );
      out["workflow_json"] = workflowJson;
      out["estimates"] = estimatePlanResources( plan );
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class ExecutePlanTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:execute_plan"; }
    std::string displayName() const override { return "Execute Plan"; }
    std::string description() const override
    {
      return "Executes an AgentPlan through the authoritative workflow engine "
             "(WorkflowRunCoordinator -> TaskCenter). Runs deterministic "
             "scientific preflight first (when intent is set) — a blocked "
             "preflight refuses execution. Returns run_id, pipeline_id, and "
             "the immediate run state; observe with harness:run_status. "
             "Transient failures are retried via bounded resume (never "
             "unlimited); a FAIL verification can never surface as success.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "execute", "plan", "workflow", "run" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      props["plan"] = plan;
      Json::Value skipPreflight( Json::objectValue );
      skipPreflight["type"] = "boolean";
      skipPreflight["description"] = "Skip intent preflight (only for custom plans).";
      props["skip_preflight"] = skipPreflight;
      Json::Value required( Json::arrayValue );
      required.append( "plan" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["executed"] = Json::Value( Json::objectValue );
      props["run_id"] = Json::Value( Json::objectValue );
      props["pipeline_id"] = Json::Value( Json::objectValue );
      props["preflight"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "plan" ) || !input["plan"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'plan'",
                                           error_codes::kInvalidParameter, "validation" );
      AgentPlan plan;
      HarnessError error;
      if ( !readAgentPlan( input["plan"], plan, error ) )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      // 1. Deterministic preflight gate (Phase 5): blocked -> refuse.
      const bool skip = input.get( "skip_preflight", false ).asBool();
      if ( !skip && !plan.intent.empty() && plan.inputs.isArray() && !plan.inputs.empty() )
      {
        const PreflightOutcome outcome = preflightIntent( plan.intent, plan.inputs );
        if ( outcome.verdict == "blocked" )
        {
          Json::Value out( Json::objectValue );
          out["preflight"] = outcome.toJson( plan.intent );
          out["executed"] = false;
          return SpatialToolResult::ok( std::move( out ) );
        }
      }

      // 2. Compile to the authoritative WorkflowDefinition.
      const std::string workflowJson = compilePlanToWorkflowJson( plan, error );
      if ( workflowJson.empty() )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      // 3. Submit through the single engine seam.
      const long pipelineId =
        sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipelineJson(
          workflowJson, false );
      if ( pipelineId < 0 )
        return SpatialToolResult::failure( "Workflow engine rejected the compiled plan",
                                           error_codes::kExecutionFailed, "runtime" );

      Json::Value out( Json::objectValue );
      out["executed"] = true;
      out["pipeline_id"] = static_cast<Json::Int64>( pipelineId );
      if ( auto run = sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline(
             pipelineId ) )
      {
        out["run_id"] = run->runId();
        out["status"] = run->state() == sicnu::workflow::WorkflowRunState::Running
                          ? "running"
                          : sicnu::workflow::workflowRunStateToString( run->state() );
      }
      out["next"] = "harness:run_status {run_id} — poll until terminal";
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class RunStatusTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:run_status"; }
    std::string displayName() const override { return "Run Status & Verification"; }
    std::string description() const override
    {
      return "Observes the REAL workflow run state (no fictional progress): "
             "state, progress, per-step status with cache-hit and output "
             "paths, and — once terminal — automatic output verification with "
             "PASS / PASS_WITH_WARNINGS / FAIL verdicts per artifact and "
             "overall. Status 'failed' is authoritative: never report success "
             "for it. Supports bounded transient retry via resume when the "
             "failure was transient and attempts remain.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "run", "status", "verification", "resume" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value runId( Json::objectValue );
      runId["type"] = "string";
      runId["description"] = "Workflow run id (from harness:execute_plan).";
      props["run_id"] = runId;
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "Optional original plan (enables map confirmation + retry policy).";
      props["plan"] = plan;
      Json::Value resume( Json::objectValue );
      resume["type"] = "boolean";
      resume["description"] = "Resume once if the run failed transiently (default true, "
                              "bounded to one automatic resume).";
      props["auto_resume_transient"] = resume;
      Json::Value required( Json::arrayValue );
      required.append( "run_id" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["run_id"] = Json::Value( Json::objectValue );
      props["state"] = Json::Value( Json::objectValue );
      props["status"] = Json::Value( Json::objectValue );
      props["steps"] = Json::Value( Json::arrayValue );
      props["verification"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string runId = input.get( "run_id", "" ).asString();
      if ( runId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'run_id'",
                                           error_codes::kInvalidParameter, "validation" );
      auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
      const long pipelineId = coordinator.pipelineIdForRun( runId );
      if ( pipelineId < 0 )
        return SpatialToolResult::failure( "Unknown run id: " + runId,
                                           error_codes::kWorkflowNotFound, "validation" );
      auto run = coordinator.runForPipeline( pipelineId );
      if ( !run )
        return SpatialToolResult::failure( "Run is no longer tracked: " + runId,
                                           error_codes::kWorkflowNotFound, "validation" );

      std::optional<AgentPlan> plan;
      if ( input.isMember( "plan" ) && input["plan"].isObject() )
      {
        AgentPlan parsed;
        HarnessError error;
        if ( readAgentPlan( input["plan"], parsed, error ) )
          plan = parsed;
      }

      Json::Value doc = runResultDocument( run, plan ? &*plan : nullptr );

      // Bounded transient retry (Phase 13): a failed run whose recorded error
      // is transient-class may be resumed exactly once per status call, and
      // only when the caller still allows it (default true). Resume re-runs
      // only steps without valid outputs — completed work is never redone.
      if ( run->state() == sicnu::workflow::WorkflowRunState::Failed &&
           input.get( "auto_resume_transient", true ).asBool() )
      {
        const std::string message = run->errorMessage();
        const HarnessError normalized = normalizeLegacyError( "EXECUTION_FAILED", message );
        const bool transient = message.find( "timed out" ) != std::string::npos ||
                               message.find( "Transient" ) != std::string::npos ||
                               message.find( "transient" ) != std::string::npos ||
                               normalized.code == error_codes::kTransientFailure;
        if ( transient && !doc.isMember( "auto_resumed" ) )
        {
          QString resumeError;
          const long resumed = coordinator.resumeRun( runId, &resumeError );
          Json::Value retry( Json::objectValue );
          retry["attempted"] = resumed > 0;
          retry["policy"] = "single_bounded_resume";
          if ( resumed > 0 )
          {
            retry["resumed_pipeline_id"] = static_cast<Json::Int64>( resumed );
            doc["auto_resume"] = retry;
            if ( auto resumedRun = coordinator.runForPipeline( resumed ) )
            {
              doc = runResultDocument( resumedRun, plan ? &*plan : nullptr );
              doc["auto_resumed"] = true;
            }
          }
          else
          {
            retry["error"] = resumeError.toStdString();
            doc["auto_resume"] = retry;
          }
        }
      }
      return SpatialToolResult::ok( std::move( doc ) );
    }
};

Json::Value runResultDocument( const std::shared_ptr<sicnu::workflow::WorkflowRun> &run,
                               const AgentPlan *plan )
{
  Json::Value doc( Json::objectValue );
  doc["run_id"] = run->runId();
  doc["state"] = sicnu::workflow::workflowRunStateToString( run->state() );
  doc["progress"] = run->progress();
  const bool terminal = sicnu::workflow::isTerminalRunState( run->state() );

  Json::Value steps( Json::arrayValue );
  std::vector<std::string> outputPaths;
  for ( const auto &step : run->stepPlans() )
  {
    Json::Value entry( Json::objectValue );
    entry["step_id"] = step.stepId;
    entry["status"] = step.status;
    entry["cache_hit"] = step.cacheHit;
    if ( !step.outputLayerPath.empty() )
    {
      entry["output"] = step.outputLayerPath;
      if ( step.status == "Completed" )
        outputPaths.push_back( step.outputLayerPath );
    }
    if ( !step.errorMessage.empty() )
      entry["error"] = step.errorMessage;
    if ( step.taskId > 0 )
      entry["execution_id"] = "task-" + std::to_string( step.taskId );
    steps.append( entry );
  }
  doc["steps"] = steps;

  Json::Value verificationDoc( Json::objectValue );
  if ( terminal && run->state() == sicnu::workflow::WorkflowRunState::Completed )
  {
    std::vector<ArtifactVerification> verifications;
    Json::Value artifacts( Json::arrayValue );
    for ( const std::string &path : outputPaths )
    {
      VerificationExpectations expectations;
      // Workflow-run outputs are plain files today; provenance presence is a
      // warning-class check on the copilot path, not a run gate here.
      expectations.requireProvenance = false;
      const ArtifactVerification artifact = verifyArtifact( path, expectations );
      verifications.push_back( artifact );
      artifacts.append( artifact.toJson() );
    }
    const Verdict overall = aggregateVerdict( verifications );
    verificationDoc["artifacts"] = artifacts;
    verificationDoc["verdict"] = verdictToStringWire( overall );
    doc["verification"] = verificationDoc;
    // A FAIL verification forces status failed — no false success path.
    doc["status"] = overall == Verdict::Fail ? "failed" : "completed";
    if ( overall != Verdict::Fail && plan && plan->mapOutput.isObject() )
      doc["map_confirmation"] = confirmMapOutput( *plan, doc["steps"] );
  }
  else if ( terminal && run->state() == sicnu::workflow::WorkflowRunState::Failed )
  {
    verificationDoc["verdict"] = "FAIL";
    verificationDoc["reason"] = "run failed before outputs could be verified";
    doc["verification"] = verificationDoc;
    doc["status"] = "failed";
    doc["error"] = run->errorMessage();
  }
  else
  {
    doc["status"] = "running";
  }
  return doc;
}

Json::Value confirmMapOutput( const AgentPlan &plan, const Json::Value &stepsDoc )
{
  Json::Value confirmation( Json::objectValue );
  const Json::Value mapOutput = plan.mapOutput;
  const std::string layout = mapOutput.get( "layout_name", "" ).asString();
  confirmation["layout"] = layout;

  // 1. The plan step that produced the map output must have completed.
  std::string fromStep = mapOutput.get( "from_step", "" ).asString();
  if ( fromStep.empty() && plan.outputs.isArray() && plan.outputs.size() > 0 )
    fromStep = plan.outputs[0].get( "from_step", "" ).asString();
  bool stepCompleted = false;
  for ( const auto &step : stepsDoc )
  {
    if ( step.get( "step_id", "" ).asString() == fromStep &&
         step.get( "status", "" ).asString() == "Completed" )
      stepCompleted = true;
  }
  if ( !stepCompleted && !fromStep.empty() )
  {
    confirmation["verdict"] = "FAIL";
    confirmation["reason"] = "map-producing step did not complete: " + fromStep;
    return confirmation;
  }

  // 2. MapSpec path: compose -> preflight -> bounded repair loop through the
  // existing cartography tools (registry), then export gating.
  const std::string verdictFail = verdictToStringWire( Verdict::Fail );
  if ( mapOutput.isMember( "mapspec" ) )
  {
    auto compose = SpatialToolRegistry::instance().find( "cartography:compose" );
    auto preflight = SpatialToolRegistry::instance().find( "cartography:preflight" );
    auto repair = SpatialToolRegistry::instance().find( "cartography:repair" );
    if ( !compose || !preflight )
    {
      confirmation["verdict"] = "FAIL";
      confirmation["reason"] = "cartography tools unavailable";
      return confirmation;
    }
    Json::Value composeInput;
    composeInput["mapspec"] = mapOutput["mapspec"];
    if ( !layout.empty() )
      composeInput["layout_name"] = layout;
    const SpatialToolResult composed = ( *compose )->execute( composeInput );
    if ( !composed.success )
    {
      confirmation["verdict"] = verdictFail;
      confirmation["reason"] = composed.error;
      return confirmation;
    }
    if ( composed.output.isMember( "quality" ) )
      confirmation["compose_quality"] = composed.output["quality"];

    Json::Value preflightInput;
    preflightInput["mapspec"] = mapOutput["mapspec"];
    SpatialToolResult checked = ( *preflight )->execute( preflightInput );
    int repairs = 0;
    while ( checked.success && checked.output.isObject() &&
            checked.output.get( "passed", true ).asBool() == false && repairs < 3 && repair )
    {
      Json::Value repairInput;
      repairInput["mapspec"] = mapOutput["mapspec"];
      repairInput["issues"] = checked.output.get( "issues", Json::Value( Json::arrayValue ) );
      const SpatialToolResult repaired = ( *repair )->execute( repairInput );
      if ( !repaired.success )
        break;
      checked = ( *preflight )->execute( preflightInput );
      ++repairs;
    }
    confirmation["repair_passes"] = repairs;
    const bool passed = checked.success && checked.output.isObject() &&
                        checked.output.get( "passed", false ).asBool();
    confirmation["verdict"] = passed ? verdictToStringWire( Verdict::Pass ) : verdictFail;
    if ( checked.output.isObject() && checked.output.isMember( "quality_score" ) )
      confirmation["quality_score"] = checked.output["quality_score"];
    return confirmation;
  }

  // 3. Layout-only path: the referenced layout must exist and export must
  // have been requested explicitly — presence checks only, no guessing.
  if ( !layout.empty() )
  {
    auto listLayouts = SpatialToolRegistry::instance().find( "layout:list" );
    bool layoutExists = false;
    if ( listLayouts )
    {
      const SpatialToolResult listed = ( *listLayouts )->execute( Json::Value() );
      if ( listed.success && listed.output.isObject() )
      {
        for ( const auto &entry : listed.output.get( "layouts", Json::Value( Json::arrayValue ) ) )
        {
          if ( entry.get( "name", "" ).asString() == layout )
            layoutExists = true;
        }
      }
    }
    if ( !layoutExists )
    {
      confirmation["verdict"] = verdictFail;
      confirmation["reason"] = "declared map layout does not exist: " + layout;
      return confirmation;
    }
    confirmation["verdict"] = verdictToStringWire( Verdict::Pass );
    confirmation["note"] = "layout exists; run cartography:preflight for full map QA";
    return confirmation;
  }

  confirmation["verdict"] = verdictToStringWire( Verdict::PassWithWarnings );
  confirmation["reason"] = "map_output declared without layout/mapspec — nothing to confirm";
  return confirmation;
}

} // namespace

void registerPlanTools()
{
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<PreflightTool>() );
  registry.registerTool( std::make_shared<PlanTool>() );
  registry.registerTool( std::make_shared<ExecutePlanTool>() );
  registry.registerTool( std::make_shared<RunStatusTool>() );
}

} // namespace sicnu::agent::harness
