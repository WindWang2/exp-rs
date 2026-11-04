// src/agent/tools/agent_session_adapter.cpp
#include "agent_session_adapter.h"

#include "data_platform_tools.h"

#include "agent/harness/agent_plan.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/context_ledger.h"
#include "agent/harness/entity_resolver.h"
#include "agent/harness/grounding_probes.h"
#include "agent/harness/grounding_tools.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/harness_verification.h"
#include "agent/harness/scientific_preflight.h"
#include "agent/harness/workflow_planner.h"
#include "agent/spatial_tools/spatial_tool.h"

#include "planner/live/capability_live.h"
#include "planner/plan_ir_projection.h"
#include "planner/planner_core.h"

#include "preflight/capability_mirror.h"

#include "science_context/agent_adapter.h"
#include "science_context/broker.h"
#include "science_context/planner_goal_projection.h"

#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace sicnu::agent {
namespace {

using sicnu::agent_loop::AssetFact;
using sicnu::agent_loop::DataStateSnapshot;
using sicnu::agent_loop::ExecutionOutcome;
using sicnu::agent_loop::ExecutionStart;
using sicnu::agent_loop::PlanDraft;
using sicnu::agent_loop::PlanRequest;
using sicnu::agent_loop::PreflightReport;
using sicnu::agent_loop::VerificationReport;

// ---------------------------------------------------------------------------
// Shared grounding helper: resolve a dataset ref and return the FULL
// DatasetUnderstanding document (cache-first over the ONE understanding
// cache; live through the ONE spatial:understand tool). This mirrors the
// grounding probe's access path piece for piece — no second grounding
// implementation, no second cache key.
// ---------------------------------------------------------------------------
std::optional< Json::Value > groundUnderstanding( const std::string &ref,
                                                  std::string &errorCode )
{
    using sicnu::agent::harness::resolveDatasetRef;
    sicnu::agent::harness::HarnessError resolveError;
    const std::optional< sicnu::agent::harness::ResolvedDataset > resolved =
        resolveDatasetRef( QString::fromStdString( ref ), &resolveError );
    if ( !resolved )
    {
        errorCode = resolveError.code.empty() ? "DATASET_NOT_FOUND" : resolveError.code;
        return std::nullopt;
    }

    Json::Value understanding = sicnu::agent::harness::cachedUnderstandingFor(
        resolved->path, resolved->revision );
    if ( understanding.isNull() )
    {
        sicnu::agent::SpatialToolPtr tool =
            sicnu::agent::SpatialToolRegistry::instance().find( "spatial:understand" )
                .value_or( nullptr );
        if ( !tool )
        {
            errorCode = "TOOL_NOT_FOUND";
            return std::nullopt;
        }
        Json::Value input( Json::objectValue );
        input[ "asset" ] = ref;
        const sicnu::agent::SpatialToolResult result = tool->execute( input );
        if ( !result.success || result.output.isNull() )
        {
            errorCode = result.errorCode.empty() ? "GROUNDING_FAILED" : result.errorCode;
            return std::nullopt;
        }
        understanding = result.output.get( "dataset_understanding", Json::Value() );
        if ( understanding.isNull() )
        {
            errorCode = "GROUNDING_EMPTY";
            return std::nullopt;
        }
    }
    return understanding;
}

} // namespace

// ---------------------------------------------------------------------------
// HarnessDataStateProvider
// ---------------------------------------------------------------------------
DataStateSnapshot HarnessDataStateProvider::snapshot( const std::string &goal,
                                                      const Json::Value &refs )
{
    ( void )goal;
    using sicnu::agent::harness::allProbeScopes;
    using sicnu::agent::harness::probeDatasetFacts;
    using sicnu::agent::harness::ProbeRequest;

    DataStateSnapshot snap;
    if ( refs.isObject() )
    {
        for ( const std::string &slot : refs.getMemberNames() )
        {
            const Json::Value &value = refs[ slot ];
            AssetFact fact;
            fact.slot = slot;
            if ( !value.isString() || value.asString().empty() )
            {
                fact.ref.clear();
                fact.resolutionError = "INVALID_REF";
                snap.assets.push_back( std::move( fact ) );
                continue;
            }
            fact.ref = value.asString();

            ProbeRequest request;
            request.ref = fact.ref;
            request.scopes = allProbeScopes();
            const auto outcome = probeDatasetFacts( request );
            fact.resolved = outcome.ok;
            fact.facts = outcome.facts;
            fact.resolutionError = outcome.ok ? std::string() : outcome.code;
            if ( outcome.ok )
            {
                const Json::Value &kind = outcome.facts.get( "source_kind", Json::Value() );
                fact.kind = kind.isString() ? kind.asString() : "unknown";
            }
            snap.assets.push_back( std::move( fact ) );
        }
    }

    snap.summary = Json::objectValue;
    int resolved = 0;
    for ( const auto &asset : snap.assets )
        if ( asset.resolved )
            ++resolved;
    snap.summary[ "total" ] = static_cast< Json::Int >( snap.assets.size() );
    snap.summary[ "resolved" ] = static_cast< Json::Int >( resolved );
    return snap;
}

// ---------------------------------------------------------------------------
// HarnessPlanner
// ---------------------------------------------------------------------------
struct HarnessPlanner::Impl
{
    sicnu::planner::LiveCapabilityProvider capability;
};

HarnessPlanner::~HarnessPlanner() = default;

bool HarnessPlanner::create( std::unique_ptr< HarnessPlanner > &out, std::string &error )
{
    // The capability authority is the SAME mirror directory the harness
    // capability knowledge reads; the live provider re-checks every fact
    // against the contracts registry itself.
    const std::string directory =
        sicnu::agent::harness::CapabilityKnowledge::instance().directory();
    sicnu::preflight::CapabilityMirrorProjection mirror;
    if ( directory.empty() || mirror.loadDirectory( directory ) <= 0 )
    {
        error = "CAPABILITY_AUTHORITY_UNAVAILABLE";
        return false;
    }
    std::unique_ptr< HarnessPlanner > planner( new HarnessPlanner() );
    planner->mImpl = std::make_unique< Impl >();
    std::string createError;
    if ( !sicnu::planner::LiveCapabilityProvider::create( mirror, planner->mImpl->capability,
                                                          createError ) )
    {
        error = "CAPABILITY_AUTHORITY_UNHEALTHY: " + createError;
        return false;
    }
    out = std::move( planner );
    return true;
}

PlanDraft HarnessPlanner::plan( const PlanRequest &request )
{
    using sicnu::planner::planScientificWork;
    using sicnu::planner::PlannerProviders;
    using sicnu::planner::projectPlanToIr;
    using sicnu::science_context::agent_adapter::sharedBroker;
    using sicnu::science_context::PlannerAssetEnrichment;
    using sicnu::science_context::PlannerProjectionResult;
    using sicnu::science_context::projectPlannerInputs;
    using sicnu::science_context::SynthesizeRequest;
    using sicnu::agent::harness::compileWorkflow;
    using sicnu::agent::harness::CompileWorkflowRequest;
    using sicnu::agent::harness::planFingerprint;

    PlanDraft draft;
    draft.attempt = request.attempt;

    sicnu::agent::ensureScienceContextAuthorities();

    // 1. Bundle over the shared broker (live recipes, capability routing and
    //    passport facts; cache-aware with the broker's own invalidation).
    SynthesizeRequest synthesize;
    synthesize.goal = request.goal;
    synthesize.intent = request.intent;
    for ( const AssetFact &asset : request.snapshot.assets )
    {
        if ( !asset.ref.empty() )
            synthesize.assetKeys.push_back( asset.ref );
    }
    const auto synthesized = sharedBroker().synthesize( synthesize );
    draft.intent = synthesized.bundle.intent;

    // 2. Passport-backed enrichment for the SELECTED assets (the bundle
    //    carries no lifecycle state by design — the broker's resolver does).
    PlannerAssetEnrichment enrichment;
    for ( const auto &summary : synthesized.bundle.assets )
    {
        sicnu::science_context::AssetResolveRequest resolve;
        resolve.assetKey = summary.assetId;
        const auto resolved = sharedBroker().assets().resolve( resolve );
        if ( !resolved.ok )
            continue; // honest: the asset plans with unknown state and the planner asks
        sicnu::planner::PlannerAssetFacts facts;
        facts.ref = summary.assetId;
        facts.state = sicnu::state::assetLifecycleToString( resolved.state.lifecycle );
        enrichment.assets[ summary.assetId ] = facts;
    }
    if ( !enrichment.assets.empty() )
        enrichment.source = "scientific_state.passports";

    // 3. Bundle → planner inputs (vocabulary-validated; unresolved issues
    //    ride the draft as missing facts).
    const PlannerProjectionResult projection =
        projectPlannerInputs( synthesized.bundle, enrichment );
    for ( const auto &issue : projection.unresolved )
        draft.missingFacts.append( issue.code + ": " + issue.detail );

    // 4. Deterministic scientific planning over the live capability facts.
    PlannerProviders providers;
    providers.capability = &mImpl->capability;
    const sicnu::planner::PlanningResult planned =
        planScientificWork( projection.goal, projection.context, providers );
    const sicnu::planner::ScientificPlan *primary = planned.primary();
    if ( primary == nullptr || primary->verdict == "infeasible" )
    {
        draft.valid = false;
        draft.error = "PLANNER_INFEASIBLE";
        if ( primary != nullptr )
            for ( const auto &reason : primary->verdictReasons )
                draft.missingFacts.append( reason );
        return draft;
    }

    // 5. Plan → workflow_ir → compileWorkflow (analysis/repair/lowering over
    //    the grounding seam). The draft carries the LOWERED executable steps.
    std::vector< std::string > warnings;
    std::string projectionError;
    const Json::Value irDoc = projectPlanToIr( *primary, &warnings, &projectionError );
    if ( !projectionError.empty() || irDoc.isNull() )
    {
        draft.valid = false;
        draft.error = projectionError.empty() ? "IR_PROJECTION_REFUSED" : projectionError;
        return draft;
    }

    CompileWorkflowRequest compile;
    compile.irDoc = irDoc;
    compile.goal = request.goal;
    compile.intent = draft.intent;
    for ( const Json::Value &slot : irDoc.get( "inputs", Json::Value( Json::arrayValue ) ) )
    {
        if ( !slot.isObject() )
            continue;
        const std::string name = slot.get( "name", "" ).asString();
        const std::string ref = slot.get( "ref", "" ).asString();
        if ( name.empty() || ref.empty() )
            continue;
        std::string errorCode;
        const std::optional< Json::Value > understanding = groundUnderstanding( ref, errorCode );
        if ( understanding )
            compile.inputFacts[ name ] = *understanding;
        else
            draft.missingFacts.append( "grounding_failed:" + ref + ":" + errorCode );
    }

    sicnu::agent::harness::HarnessError compileError;
    const sicnu::agent::harness::CompiledWorkflow compiled = compileWorkflow( compile, compileError );
    if ( !compileError.code.empty() || compiled.executionBlocked ||
         compiled.workflowJson.empty() )
    {
        draft.valid = false;
        draft.error = compileError.code.empty() ? std::string( "COMPILE_BLOCKED" )
                                                : compileError.code;
        for ( const auto &missing : compiled.missingFacts )
            draft.missingFacts.append( missing );
        return draft;
    }

    const sicnu::agent::harness::AgentPlan &plan = compiled.plan;
    draft.valid = true;
    draft.planId = plan.planId;
    draft.steps = plan.steps;
    draft.outputs = plan.outputs;
    for ( const Json::Value &alternative : compiled.alternatives )
    {
        if ( !alternative.isObject() )
            continue;
        draft.droppedAlternatives.push_back(
            { alternative.get( "id", "" ).asString(),
              alternative.get( "summary", "" ).asString(),
              alternative.get( "why_not", "" ).asString() } );
    }
    for ( const auto &missing : compiled.missingFacts )
        draft.missingFacts.append( missing );

    const std::string fingerprint = planFingerprint( plan );
    draft.fingerprint = fingerprint + "#" + std::to_string( request.attempt );
    draft.identity = fingerprint;

    const Json::Value estimates =
        sicnu::agent::harness::estimatePlanResources( plan );
    const Json::Value perStep = estimates.get( "per_step", Json::Value( Json::objectValue ) );
    for ( const std::string &stepId : perStep.getMemberNames() )
    {
        sicnu::agent_loop::PlanEstimate estimate;
        estimate.stepId = stepId;
        estimate.ramMb = perStep[ stepId ].isInt64() ? perStep[ stepId ].asInt64() : 0;
        draft.estimates.push_back( estimate );
    }
    return draft;
}

// ---------------------------------------------------------------------------
// HarnessPreflight
// ---------------------------------------------------------------------------
PreflightReport HarnessPreflight::check( const PlanDraft &plan,
                                         const sicnu::agent_loop::DataStateSnapshot &snapshot )
{
    using sicnu::agent::harness::preflightIntent;
    using sicnu::agent_loop::PreflightIssue;

    PreflightReport report;
    if ( plan.intent.empty() )
    {
        // Custom plans keep their natural bypass (no intent → no rule pack)
        // — the same contract harness:execute_plan applies.
        report.verdict = "ok";
        PreflightIssue issue;
        issue.code = "NO_INTENT_RULES";
        issue.severity = "info";
        issue.message = "custom plan: shared preflight rules only";
        report.issues.push_back( issue );
        return report;
    }

    Json::Value refs( Json::objectValue );
    for ( const AssetFact &asset : snapshot.assets )
    {
        if ( asset.slot.empty() || asset.ref.empty() )
            continue;
        refs[ asset.slot ] = asset.ref;
    }

    const sicnu::agent::harness::PreflightOutcome outcome = preflightIntent( plan.intent, refs );
    report.verdict = outcome.verdict;
    report.checks = outcome.checks;
    for ( const Json::Value &issue : outcome.issues )
    {
        if ( !issue.isObject() )
            continue;
        PreflightIssue mapped;
        mapped.code = issue.get( "code", "" ).asString();
        mapped.severity = issue.get( "severity", "error" ).asString();
        mapped.message = issue.get( "message", issue.toStyledString() ).asString();
        report.issues.push_back( std::move( mapped ) );
    }
    return report;
}

// ---------------------------------------------------------------------------
// HarnessExecutor
// ---------------------------------------------------------------------------
namespace {

/// Rebuilds the executable AgentPlan from the draft (the draft's steps ARE
/// the lowered steps verbatim; compilePlanToWorkflowJson is deterministic so
/// the engine runs exactly what was preflighted).
sicnu::agent::harness::AgentPlan agentPlanFromDraft( const PlanDraft &draft )
{
    using sicnu::agent::harness::AgentPlan;
    AgentPlan plan;
    plan.planId = draft.planId;
    plan.goal = std::string();
    plan.intent = draft.intent;
    plan.inputs = Json::Value( Json::arrayValue );
    plan.steps = draft.steps;
    plan.outputs = draft.outputs;
    return plan;
}

ExecutionOutcome outcomeForRun( const std::shared_ptr< sicnu::workflow::WorkflowRun > &run )
{
    using sicnu::workflow::WorkflowRunState;
    using sicnu::workflow::isTerminalRunState;

    // Loop wire contract (session_seams.h): "running" | "succeeded" |
    // "failed" | "cancelled" — the engine's own capitalized spellings stay
    // in the evidence documents, never in the loop state field.
    ExecutionOutcome outcome;
    if ( !run )
        return outcome;
    outcome.runId = run->runId();
    outcome.finished = isTerminalRunState( run->state() );
    switch ( run->state() )
    {
        case WorkflowRunState::Running:
        case WorkflowRunState::Created:
        case WorkflowRunState::Planning:
        case WorkflowRunState::Ready:
        case WorkflowRunState::WaitingResource:
        case WorkflowRunState::Cancelling:
        case WorkflowRunState::Interrupted:
            outcome.state = "running";
            break;
        case WorkflowRunState::Completed:
            outcome.state = "succeeded";
            outcome.succeeded = true;
            break;
        case WorkflowRunState::Failed:
        {
            outcome.state = "failed";
            const sicnu::agent::harness::HarnessError normalized =
                sicnu::agent::harness::normalizeLegacyError( "EXECUTION_FAILED",
                                                             run->errorMessage() );
            outcome.errorCode = normalized.code;
            outcome.errorMessage = run->errorMessage();
            break;
        }
        case WorkflowRunState::Canceled:
            outcome.state = "cancelled";
            outcome.errorCode = "CANCELLED";
            outcome.errorMessage = run->errorMessage();
            break;
    }
    for ( const auto &step : run->stepPlans() )
    {
        if ( step.status == "Completed" && !step.outputLayerPath.empty() )
            outcome.artifacts.push_back( step.outputLayerPath );
    }
    return outcome;
}

} // namespace

ExecutionStart HarnessExecutor::begin( const PlanDraft &plan )
{
    using sicnu::agent::harness::compilePlanToWorkflowJson;
    using sicnu::agent::harness::HarnessError;
    using sicnu::workflow::WorkflowRunCoordinator;

    ExecutionStart start;
    if ( !plan.valid )
    {
        start.error = "PLAN_INVALID";
        return start;
    }

    const sicnu::agent::harness::AgentPlan executable = agentPlanFromDraft( plan );
    HarnessError compileError;
    const std::string workflowJson = compilePlanToWorkflowJson( executable, compileError );
    if ( workflowJson.empty() )
    {
        start.error = compileError.code.empty() ? "COMPILE_FAILED" : compileError.code;
        return start;
    }

    auto &coordinator = WorkflowRunCoordinator::instance();
    const long pipelineId = coordinator.startTrackedPipelineJson( workflowJson, false );
    if ( pipelineId < 0 )
    {
        start.error = "ENGINE_REFUSED";
        return start;
    }
    const auto run = coordinator.runForPipeline( pipelineId );
    if ( !run )
    {
        start.error = "ENGINE_NO_RUN";
        return start;
    }
    start.started = true;
    start.runId = run->runId();

    // Same typed binding harness:execute_plan records — the diagnoser and
    // explain surfaces read the goal/intent/fingerprint from here.
    sicnu::agent::harness::ContextLedger::instance().recordPlanBinding(
        run->runId(), executable.planId, executable.goal, executable.intent, "running",
        sicnu::agent::harness::planFingerprint( executable ),
        executable.raw.isObject() ? executable.raw.get( "workflow_ir", Json::Value() )
                                  : Json::Value() );

    // Bounded run bookkeeping (same discipline as the harness ledgers): a
    // driver serves many sessions, but the map never grows without end.
    while ( mPipelineByRun.size() > 128 && !mPipelineByRun.empty() )
        mPipelineByRun.erase( mPipelineByRun.begin() );
    mPipelineByRun[ run->runId() ] = pipelineId;
    return start;
}

ExecutionOutcome HarnessExecutor::poll( const ExecutionStart &start, long long timeoutMs )
{
    using sicnu::workflow::WorkflowRunCoordinator;

    ExecutionOutcome outcome;
    if ( !start.started || start.runId.empty() )
    {
        outcome.finished = true;
        outcome.succeeded = false;
        outcome.errorCode = start.error.empty() ? "NOT_STARTED" : start.error;
        return outcome;
    }

    auto &coordinator = WorkflowRunCoordinator::instance();
    const long pipelineId = [ this, &start, &coordinator ]() -> long {
        const auto it = mPipelineByRun.find( start.runId );
        if ( it != mPipelineByRun.end() )
            return it->second;
        return coordinator.pipelineIdForRun( start.runId );
    }();
    if ( pipelineId < 0 )
    {
        outcome.finished = true;
        outcome.errorCode = "RUN_NOT_TRACKED";
        return outcome;
    }

    // Bounded wait in 25 ms slices — the engine's own workers drive the run
    // to a terminal state; the loop's timeout budget bounds this poll.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds( timeoutMs > 0 ? timeoutMs : 0 );
    auto run = coordinator.runForPipeline( pipelineId );
    while ( run && !sicnu::workflow::isTerminalRunState( run->state() ) )
    {
        if ( std::chrono::steady_clock::now() >= deadline )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
        run = coordinator.runForPipeline( pipelineId );
    }
    return outcomeForRun( run );
}

void HarnessExecutor::cancel( const ExecutionStart &start )
{
    if ( !start.started || start.runId.empty() )
        return;
    auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
    const auto it = mPipelineByRun.find( start.runId );
    const long pipelineId =
        it != mPipelineByRun.end() ? it->second : coordinator.pipelineIdForRun( start.runId );
    if ( pipelineId >= 0 )
        coordinator.cancelRun( pipelineId );
}

// ---------------------------------------------------------------------------
// HarnessDiagnoser
// ---------------------------------------------------------------------------
sicnu::agent_loop::Diagnosis HarnessDiagnoser::diagnose(
    const PlanDraft &plan, const ExecutionOutcome &outcome,
    const VerificationReport &verification )
{
    ( void )plan;
    using sicnu::workflow::WorkflowRunCoordinator;

    sicnu::agent_loop::Diagnosis diagnosis;
    Json::Value evidence( Json::objectValue );
    evidence[ "verification_verdict" ] = verification.verdict();
    evidence[ "outcome_state" ] = outcome.state;

    auto &coordinator = WorkflowRunCoordinator::instance();
    const long pipelineId = coordinator.pipelineIdForRun( outcome.runId );
    if ( pipelineId >= 0 )
    {
        const auto run = coordinator.runForPipeline( pipelineId );
        if ( run )
        {
            Json::Value steps( Json::arrayValue );
            for ( const auto &step : run->stepPlans() )
            {
                Json::Value entry( Json::objectValue );
                entry[ "step_id" ] = step.stepId;
                entry[ "status" ] = step.status;
                if ( !step.errorMessage.empty() )
                    entry[ "error" ] = step.errorMessage;
                steps.append( entry );
            }
            evidence[ "run" ][ "run_id" ] = run->runId();
            evidence[ "run" ][ "state" ] =
                sicnu::workflow::workflowRunStateToString( run->state() );
            evidence[ "run" ][ "steps" ] = steps;

            if ( run->state() == sicnu::workflow::WorkflowRunState::Failed )
            {
                const sicnu::agent::harness::HarnessError normalized =
                    sicnu::agent::harness::normalizeLegacyError( "EXECUTION_FAILED",
                                                                 run->errorMessage() );
                diagnosis.rootCauseCode = normalized.code;
                diagnosis.summary = run->errorMessage().empty()
                                        ? normalized.summary
                                        : run->errorMessage();
            }
        }
    }

    if ( diagnosis.rootCauseCode.empty() && !verification.verdict().empty() &&
         verification.verdict() != "PASS" )
    {
        diagnosis.rootCauseCode = verification.verdict() == "FAIL"
                                      ? "VERIFICATION_FAILED"
                                      : "VERIFICATION_INDETERMINATE";
        diagnosis.summary = "verification verdict " + verification.verdict();
    }
    diagnosis.evidence = evidence;
    return diagnosis;
}

// ---------------------------------------------------------------------------
// Bundle assembly
// ---------------------------------------------------------------------------
ProductionSeamBundle makeProductionSeamBundle()
{
    ProductionSeamBundle bundle;
    bundle.data = std::make_unique< HarnessDataStateProvider >();
    if ( !HarnessPlanner::create( bundle.planner, bundle.error ) )
        return bundle; // planner authority unavailable — fail closed at the gate
    bundle.preflight = std::make_unique< HarnessPreflight >();
    bundle.executor = std::make_unique< HarnessExecutor >();
    bundle.verifier = std::make_unique< HarnessVerifier >();
    bundle.diagnoser = std::make_unique< HarnessDiagnoser >();
    return bundle;
}

// ---------------------------------------------------------------------------
// HarnessVerifier (existing implementation below)
// ---------------------------------------------------------------------------
namespace {

/// Plan output expectations for one artifact path: matched by the output
/// document's `name`/`from_step` path or, in the loop's minimal wire shape,
/// by an output entry that names the path directly. Everything undeclared
/// falls back to the structural defaults (fail-closed: an undeclared
/// artifact still goes through every default check).
sicnu::agent::harness::VerificationExpectations
expectationsFor( const sicnu::agent_loop::PlanDraft &plan, const std::string &path )
{
    using sicnu::agent::harness::VerificationExpectations;
    VerificationExpectations expectations;
    expectations.requireProvenance = false; // loop deliveries carry no sidecar contract yet
    expectations.requireNonEmpty = true;

    // Minimal shape (documented): the plan's output documents carry no
    // per-artifact path mapping yet, so the FIRST output naming a known
    // kind provides the expectations and every other artifact is verified
    // under structural defaults. A wrong-typed kind is skipped, never
    // dereferenced (no Json::LogicError may escape verify()).
    for ( const Json::Value &output : plan.outputs )
    {
        if ( !output.isObject() )
            continue;
        const Json::Value &kind = output[ "kind" ];
        if ( kind.isString() &&
             ( kind.asString() == "raster" || kind.asString() == "vector" ||
               kind.asString() == "map" ) )
        {
            expectations.kind = kind.asString();
            const Json::Value &crs = output[ "crs" ];
            if ( crs.isString() && !crs.asString().empty() )
                expectations.crs = crs.asString();
            break;
        }
    }
    ( void )path;
    return expectations;
}

sicnu::agent_loop::VerificationReport
HarnessVerifier::verify( const sicnu::agent_loop::PlanDraft &plan,
                         const sicnu::agent_loop::ExecutionOutcome &outcome )
{
    using namespace sicnu::agent_loop;
    using sicnu::agent::harness::aggregateVerdict;
    using sicnu::agent::harness::verifyArtifact;
    using sicnu::agent::harness::Verdict;

    VerificationReport report;
    for ( const std::string &path : outcome.artifacts )
    {
        const auto artifact = verifyArtifact( path, expectationsFor( plan, path ) );
        ArtifactVerificationReport entry;
        entry.path = artifact.path;
        switch ( artifact.verdict )
        {
            case Verdict::Pass:
                entry.verdict = "PASS";
                break;
            case Verdict::PassWithWarnings:
                entry.verdict = "PASS_WITH_WARNINGS";
                break;
            case Verdict::Fail:
            default:
                entry.verdict = "FAIL";
                break;
        }
        for ( const auto &check : artifact.checks )
        {
            if ( check.passed )
                continue;
            // Failed checks become the report's warnings/notes: an
            // error-class check keeps the verdict FAIL (never-success),
            // warning-class ones downgrade to PASS_WITH_WARNINGS upstream.
            const std::string line = check.code.empty() ? check.check
                                                        : ( check.code + ": " + check.check );
            entry.warnings.push_back( line );
        }
        report.artifacts.push_back( entry );
    }

    report.verdictValue = VerificationReport::aggregate( report.artifacts );
    return report;
}

} // namespace

} // namespace sicnu::agent
