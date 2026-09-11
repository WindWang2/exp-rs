// src/agent/harness/run_loop.cpp
#include "run_loop.h"

#include "agent_plan.h"
#include "evidence.h"
#include "harness_actions.h"
#include "harness_error.h"
#include "harness_verification.h"
#include "spatial_tools/spatial_tool.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QFileInfo>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

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

/// Per-run diagnose budget. Each harness:diagnose_run call that returns
/// actionable proposals consumes one unit; an exhausted budget flips
/// `stop.stop` to true — the explicit end of the repair loop.
constexpr int kMaxDiagnoseAttempts = 5;

int &diagnoseAttempts( const std::string &runId )
{
  // Same ownership/threading model as the resume ledger in plan_tools.cpp:
  // tool execution is serialized by the surfaces that drive it (MCP stdio
  // loop, copilot UI thread). Bounded by eviction — a pathological stream
  // of distinct run ids cannot grow the map without end.
  static std::map<std::string, int> kLedger;
  constexpr size_t kMaxTrackedRuns = 256;
  if ( kLedger.size() > kMaxTrackedRuns )
    kLedger.clear();
  return kLedger[ runId ];
}

/// Proposal kinds (closed vocabulary):
///   retry_transient      — bounded resume of a transient-class failure;
///   fix_input            — re-ground or replace an input dataset;
///   alternative_recipe   — a safe preparation/class change via recipe search;
///   verify_environment   — resources/IO the agent cannot fix by re-planning;
///   manual               — explicit human decision required.
const char *kProposalKinds[] = {
  "retry_transient", "fix_input", "alternative_recipe", "verify_environment", "manual",
};

bool isKnownProposalKind( const std::string &kind )
{
  for ( const char *k : kProposalKinds )
    if ( kind == k )
      return true;
  return false;
}

Json::Value makeProposal( const std::string &kind, const std::string &summary,
                          const std::string &risk, const std::string &rationale,
                          Json::Value action, const std::string &targetStep = "" )
{
  Json::Value proposal( Json::objectValue );
  proposal["kind"] = isKnownProposalKind( kind ) ? kind : "manual";
  proposal["summary"] = summary;
  proposal["risk"] = risk;
  proposal["rationale"] = rationale;
  if ( !targetStep.empty() )
    proposal["target_step"] = targetStep;
  proposal["action"] = action.isObject()
                         ? std::move( action )
                         : resolvedSuggestedAction( "reinspect_dataset", Json::Value() );
  return proposal;
}

/// Collects failed checks (error severity) from a persisted verification
/// sidecar. Read-only: the sidecar is authoritative, never recomputed here.
std::vector<VerificationCheck> failedErrorChecks( const ArtifactVerification &verification )
{
  std::vector<VerificationCheck> failed;
  for ( const VerificationCheck &check : verification.checks )
    if ( !check.passed && check.severity == "error" )
      failed.push_back( check );
  return failed;
}

/// Maps one failed verification check to a deterministic, science-safe
/// proposal. Only structurally safe preparations are proposed — the check
/// code (not prose) drives the mapping.
Json::Value proposalForCheck( const std::string &code, const std::string &artifactPath )
{
  Json::Value details;
  details["artifact"] = artifactPath;
  if ( code == error_codes::kCrsMismatch )
    return makeProposal( "alternative_recipe", "Reproject the artifact onto the expected CRS",
                         "low",
                         "CRS mismatch is deterministically repairable by reprojecting; "
                         "the recipe search proposes the preparation class.",
                         resolvedSuggestedAction( "reproject_to_reference", details ) );
  if ( code == error_codes::kGridMismatch )
    return makeProposal( "alternative_recipe", "Align the artifact to the expected grid",
                         "low", "Grid mismatch is repaired by resampling/alignment.",
                         resolvedSuggestedAction( "align_to_reference", details ) );
  if ( code == error_codes::kInvalidRadiometry )
    return makeProposal( "alternative_recipe",
                         "Calibrate/normalize radiometry before re-running the analysis",
                         "low",
                         "Radiometric state mismatches bias every downstream ratio; "
                         "calibration is a declared safe preparation.",
                         resolvedSuggestedAction( "normalize_radiometry", details ) );
  if ( code == error_codes::kBandRoleUnresolved )
    return makeProposal( "fix_input", "Inspect band roles feeding this product", "low",
                         "A producer dropped or misnamed bands; inspect before re-planning.",
                         resolvedSuggestedAction( "inspect_bands", details ) );
  if ( code == error_codes::kTrainingInvalid )
    return makeProposal( "fix_input", "Verify the training/reference dataset", "low",
                         "Class-domain checks failed on the training or reference input.",
                         resolvedSuggestedAction( "check_training", details ) );
  if ( code == error_codes::kOutputInvalid )
    return makeProposal( "verify_environment",
                         "Re-run the producing step after freeing disk/checking the output path",
                         "medium",
                         "The artifact or its evidence could not be written; an environment "
                         "problem cannot be repaired by re-planning the science.",
                         Json::Value() );
  // Unknown failed check: honest manual proposal, never a guessed repair.
  return makeProposal( "manual", "Review the failed check and decide the repair", "medium",
                       "No deterministic repair is known for this check code.",
                       Json::Value() );
}

class DiagnoseRunTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:diagnose_run"; }
    std::string displayName() const override { return "Run Diagnosis & Repair Proposals"; }
    std::string description() const override
    {
      return "Reads a run's authoritative state and emits STRUCTURED repair "
             "proposals: {run_id, plan?} → failures (per-step errors + failed "
             "verification checks from the persisted sidecars), missing "
             "declared artifacts, and repair_proposals [{kind, summary, risk, "
             "rationale, action{tool|workbench_command}}]. Proposal actions "
             "resolve through the closed action vocabulary. Bounded: each run "
             "has a diagnose budget; exhausted budgets answer stop=true with a "
             "typed reason. Read-only — it never executes repairs itself.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "diagnose", "repair", "verify", "bounded" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value runId( Json::objectValue );
      runId["type"] = "string";
      runId["description"] = "Workflow run id (from harness:execute_plan).";
      props["run_id"] = runId;
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "Optional original plan (enables missing-artifact detection).";
      props["plan"] = plan;
      Json::Value required( Json::arrayValue );
      required.append( "run_id" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["diagnosis"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string runId = input.get( "run_id", "" ).asString();
      if ( runId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'run_id'",
                                           error_codes::kInvalidParameter, "validation" );

      std::optional<AgentPlan> plan;
      Json::Value warnings( Json::arrayValue );
      if ( input.isMember( "plan" ) && input["plan"].isObject() )
      {
        AgentPlan parsed;
        HarnessError error;
        if ( readAgentPlan( input["plan"], parsed, error ) )
          plan = parsed;
        else
          // A discarded plan would silently disable missing-artifact
          // detection — surface the parse failure instead (review P2).
          warnings.append( "plan_unparseable: " + error.code );
      }

      auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
      const long pipelineId = coordinator.pipelineIdForRun( runId );
      auto run = pipelineId >= 0 ? coordinator.runForPipeline( pipelineId ) : nullptr;
      if ( !run )
        return SpatialToolResult::failure( "Unknown run id: " + runId,
                                           error_codes::kWorkflowNotFound, "validation" );

      Json::Value diagnosis( Json::objectValue );
      diagnosis["run_id"] = runId;
      diagnosis["state"] =
        sicnu::workflow::workflowRunStateToString( run->state() );

      const bool terminal = sicnu::workflow::isTerminalRunState( run->state() );
      if ( !terminal )
      {
        // Observation phase: nothing to repair while the engine works.
        diagnosis["status"] = "running";
        Json::Value stop( Json::objectValue );
        stop["stop"] = false;
        stop["reason"] = "run_not_terminal";
        diagnosis["stop"] = stop;
        diagnosis["repair_proposals"] = Json::Value( Json::arrayValue );
        Json::Value out( Json::objectValue );
        out["diagnosis"] = diagnosis;
        return SpatialToolResult::ok( std::move( out ) );
      }

      // --- failures -------------------------------------------------------
      Json::Value failures( Json::arrayValue );
      std::set<std::string> failedStepIds;
      if ( run->state() == sicnu::workflow::WorkflowRunState::Failed )
      {
        for ( const auto &step : run->stepPlans() )
        {
          if ( step.errorMessage.empty() )
            continue;
          Json::Value failure( Json::objectValue );
          failure["step_id"] = step.stepId;
          failure["error"] = step.errorMessage;
          failures.append( failure );
          failedStepIds.insert( step.stepId );
        }
        if ( !run->errorMessage().empty() && failures.empty() )
        {
          Json::Value failure( Json::objectValue );
          failure["step_id"] = "";
          failure["error"] = run->errorMessage();
          failures.append( failure );
        }
      }

      // --- verification evidence (persisted sidecars, read-only) ----------
      Json::Value verificationFailures( Json::arrayValue );
      std::vector<std::string> outputPaths;
      for ( const auto &step : run->stepPlans() )
        if ( !step.outputLayerPath.empty() )
          outputPaths.push_back( step.outputLayerPath );
      for ( const std::string &path : outputPaths )
      {
        const auto verification = evidence::readVerificationEvidence( path, runId );
        if ( !verification )
          continue;
        for ( const VerificationCheck &check : failedErrorChecks( *verification ) )
        {
          Json::Value entry( Json::objectValue );
          entry["artifact"] = path;
          entry["check"] = check.check;
          entry["code"] = check.code;
          entry["details"] = check.details;
          verificationFailures.append( entry );
        }
      }

      // --- missing declared artifacts -------------------------------------
      Json::Value missingArtifacts( Json::arrayValue );
      if ( plan && plan->outputs.isArray() )
      {
        std::map<std::string, std::string> stepOutput;
        for ( const auto &step : run->stepPlans() )
          if ( !step.outputLayerPath.empty() )
            stepOutput[ step.stepId ] = step.outputLayerPath;
        for ( const Json::Value &output : plan->outputs )
        {
          const std::string fromStep = output.get( "from_step", "" ).asString();
          const auto it = stepOutput.find( fromStep );
          // A completed run whose producing step recorded no path is just as
          // missing as one whose file vanished — both are reported.
          if ( it != stepOutput.end() &&
               QFileInfo::exists( QString::fromStdString( it->second ) ) )
            continue;
          Json::Value missing( Json::objectValue );
          missing["name"] = output.get( "name", "" ).asString();
          missing["from_step"] = fromStep;
          missing["expected_path"] = it != stepOutput.end() ? it->second : "";
          missingArtifacts.append( missing );
        }
      }

      // --- diagnose budget --------------------------------------------------
      const int attempts = diagnoseAttempts( runId );
      Json::Value bounds( Json::objectValue );
      bounds["diagnose_attempts"] = attempts;
      bounds["attempt_limit"] = kMaxDiagnoseAttempts;

      Json::Value stop( Json::objectValue );
      stop["stop"] = false;
      stop["reason"] = "";

      // --- proposals --------------------------------------------------------
      Json::Value proposals( Json::arrayValue );
      if ( run->state() == sicnu::workflow::WorkflowRunState::Failed )
      {
        const std::string message = run->errorMessage() ;
        const HarnessError normalized = normalizeLegacyError( "EXECUTION_FAILED", message );
        if ( normalized.code == error_codes::kTransientFailure ||
             message.find( "timed out" ) != std::string::npos )
        {
          proposals.append( makeProposal(
            "retry_transient", "Resume the run (only steps without valid outputs re-run)",
            "low",
            "The recorded failure is transient-class; resume is bounded by the "
            "run_status ledger.",
            resolvedSuggestedAction( "resume_run", Json::Value() ) ) );
        }
        for ( const Json::Value &failure : failures )
        {
          const std::string error = failure.get( "error", "" ).asString();
          if ( error.find( "memory" ) != std::string::npos ||
               error.find( "Memory" ) != std::string::npos )
            proposals.append( makeProposal(
              "verify_environment", "Free memory or reduce the processing window, then resume",
              "medium",
              "Resource exhaustion is environmental; re-planning cannot create RAM.",
              Json::Value(), failure.get( "step_id", "" ).asString() ) );
          if ( error.find( "No such file" ) != std::string::npos ||
               error.find( "not found" ) != std::string::npos ||
               error.find( "DATASET_NOT_FOUND" ) != std::string::npos )
            proposals.append( makeProposal(
              "fix_input", "Re-ground the missing input, then repair the plan binding",
              "low",
              "The step referenced a dataset that no longer resolves.",
              resolvedSuggestedAction( "reinspect_dataset", Json::Value() ),
              failure.get( "step_id", "" ).asString() ) );
        }
      }
      for ( const Json::Value &failure : verificationFailures )
        proposals.append( proposalForCheck( failure.get( "code", "" ).asString(),
                                            failure.get( "artifact", "" ).asString() ) );
      if ( missingArtifacts.size() > 0 )
        proposals.append( makeProposal(
          "fix_input", "A declared output file is missing although its step completed",
          "medium",
          "The declared artifact was not committed where the engine recorded it; "
          "verify storage and the step's commit path.",
          resolvedSuggestedAction( "reinspect_dataset", Json::Value() ) ) );

      // Deduplicate identical (kind + action + target_step) proposals; the
      // same root cause often fails several checks.
      Json::Value deduped( Json::arrayValue );
      std::set<std::string> seen;
      constexpr int kMaxProposals = 32;
      for ( const Json::Value &proposal : proposals )
      {
        const std::string key = proposal.get( "kind", "" ).asString() + "|" +
                                proposal.get( "action", Json::Value() )
                                  .get( "action", "" ).asString() + "|" +
                                proposal.get( "target_step", "" ).asString();
        if ( !seen.insert( key ).second )
          continue;
        if ( static_cast<int>( deduped.size() ) >= kMaxProposals )
        {
          diagnosis["proposals_truncated"] = true;
          break;
        }
        deduped.append( proposal );
      }

      diagnosis["status"] =
        run->state() == sicnu::workflow::WorkflowRunState::Failed ? "failed" : "completed";
      diagnosis["failures"] = failures;
      diagnosis["verification_failures"] = verificationFailures;
      diagnosis["missing_artifacts"] = missingArtifacts;
      if ( !warnings.empty() )
        diagnosis["warnings"] = warnings;

      if ( attempts >= kMaxDiagnoseAttempts && !deduped.empty() )
      {
        // Budget exhausted: the proposals are still listed, but the loop must
        // stop — Pi reports to the human instead of repairing forever.
        stop["stop"] = true;
        stop["reason"] = "diagnose_budget_exhausted";
        diagnosis["repair_proposals"] = deduped;
      }
      else if ( deduped.empty() )
      {
        stop["stop"] = true;
        stop["reason"] =
          run->state() == sicnu::workflow::WorkflowRunState::Failed
            ? "failure_is_not_repairable_by_the_harness"
            : "nothing_to_repair";
        diagnosis["repair_proposals"] = deduped;
      }
      else
      {
        diagnosis["repair_proposals"] = deduped;
        diagnoseAttempts( runId ) = attempts + 1;
        bounds["diagnose_attempts"] = attempts + 1;
      }
      diagnosis["bounds"] = bounds;
      diagnosis["stop"] = stop;

      Json::Value out( Json::objectValue );
      out["diagnosis"] = diagnosis;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

} // namespace

void registerRunLoopTools()
{
  SpatialToolRegistry::instance().registerTool( std::make_shared<DiagnoseRunTool>() );
}

} // namespace sicnu::agent::harness
