// src/agent/tools/agent_session_adapter.cpp
#include "agent_session_adapter.h"

#include "agent/data_platform_tools.h"

#include "agent/harness/agent_plan.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/context_ledger.h"
#include "agent/harness/entity_resolver.h"
#include "agent/harness/grounding_probes.h"
#include "agent/harness/grounding_tools.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/harness_verification.h"
#include "agent/harness/plan_tools.h"
#include "agent/harness/repair_capability_source.h"
#include "agent/harness/scientific_preflight.h"
#include "agent/harness/workflow_planner.h"
#include "agent/spatial_tools/spatial_tool.h"

#include "agent_ops/ops_driver.h"

#include "planner/live/capability_live.h"
#include "planner/plan_ir_projection.h"
#include "planner/planner_core.h"

#include "preflight/capability_mirror.h"

#include "science_context/agent_adapter.h"
#include "science_context/broker.h"
#include "science_context/planner_goal_projection.h"

#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <json/writer.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
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

// asInt64() throws outside int64, including the double -2^63. In-range
// fractions still truncate toward zero.
bool jsonInt64IfFits( const Json::Value &value, Json::Int64 &out )
{
    if ( value.isInt64() )
    {
        out = value.asInt64();
        return true;
    }
    if ( !value.isNumeric() )
        return false;
    const double number = value.asDouble();
    // double(INT64_MAX) rounds to 2^63, which is not an int64.
    if ( !std::isfinite( number ) ||
         number <= static_cast<double>( std::numeric_limits<Json::Int64>::min() ) ||
         number >= static_cast<double>( std::numeric_limits<Json::Int64>::max() ) )
        return false;
    out = static_cast<Json::Int64>( number );
    return true;
}

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
        sicnu::agent::spatial_tools::SpatialToolPtr tool =
            sicnu::agent::spatial_tools::SpatialToolRegistry::instance()
                .find( "spatial:understand" )
                .value_or( nullptr );
        if ( !tool )
        {
            errorCode = "TOOL_NOT_FOUND";
            return std::nullopt;
        }
        Json::Value input( Json::objectValue );
        input[ "asset" ] = ref;
        const sicnu::agent::spatial_tools::SpatialToolResult result = tool->execute( input );
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

// ---------------------------------------------------------------------------
// Shared jsoncpp guard: wrong-typed values fall back, never throw (the same
// discipline the OpsDriver wire boundary applies — a Json::LogicError may
// not escape a seam into the loop).
// ---------------------------------------------------------------------------
std::string jsonString( const Json::Value &value, const char *key, const char *fallback = "" )
{
    const Json::Value &member = value.get( key, Json::Value( fallback ) );
    return member.isString() ? member.asString() : std::string( fallback );
}

} // namespace


/// missingFacts entries are STRINGS in the loop contract (the session
/// streams each one); a non-string compiler fact is rendered compactly,
/// never appended verbatim.
void appendMissingFact( sicnu::agent_loop::PlanDraft &draft, const Json::Value &fact )
{
    if ( fact.isString() )
        draft.missingFacts.append( fact.asString() );
    else
        draft.missingFacts.append( Json::writeString( Json::StreamWriterBuilder(), fact ) );
}

// ---------------------------------------------------------------------------
// HarnessDataStateProvider
// ---------------------------------------------------------------------------
DataStateSnapshot HarnessDataStateProvider::snapshot( const std::string &goal,
                                                      const Json::Value &refs )
{
    ( void )goal;
    using sicnu::agent::harness::probeDatasetFacts;
    using sicnu::agent::harness::probe_scope::allProbeScopes;
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
            if ( !outcome.ok )
                fact.kind = "unknown"; // never an inherited garbage kind
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
    std::string outputRoot; ///< deterministic artifact root (D8)
};

HarnessPlanner::~HarnessPlanner() = default;

/// Deterministic per-process artifact root (R28-T03 D8): the loop owns no
/// workspace, so unrooted plans land in one stable directory per process;
/// hosts with a workspace pass their own root through the bundle call.
std::string defaultOutputRoot()
{
    const QString root = QDir::temp().filePath(
        QStringLiteral( "exp-rs-agent-sessions/%1" ).arg( QCoreApplication::applicationPid() ) );
    QDir().mkpath( root );
    return root.toStdString();
}

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
    planner->mImpl->outputRoot = defaultOutputRoot();
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

    // 1. Bundle over the shared broker. Referenced assets are pre-resolved
    //    through the broker's OWN live resolver and fed as inline passports:
    //    the projection layer requires a non-empty asset id, and for an
    //    unregistered path reference the caller's reference IS the identity
    //    (PlannerAssetFacts.ref semantics) — the resolver carries every
    //    scientific fact, this only fills the identity slot it left honest
    //    about. Unresolvable refs stay OUT (the planner asks).
    SynthesizeRequest synthesize;
    synthesize.goal = request.goal;
    synthesize.intent = request.intent;
    std::map< std::string, std::string > resolvedLifecycle;
    for ( const AssetFact &asset : request.snapshot.assets )
    {
        if ( asset.ref.empty() )
            continue;
        sicnu::science_context::AssetResolveRequest resolve;
        resolve.assetKey = asset.ref;
        const auto resolved = sharedBroker().assets().resolve( resolve );
        if ( !resolved.ok )
        {
            draft.missingFacts.append( "asset_resolve_failed:" + asset.ref + ":" +
                                       ( resolved.error.empty() ? "asset_not_found"
                                                                 : resolved.error ) );
            continue;
        }
        sicnu::state::RemoteSensingAssetState state = resolved.state;
        if ( state.assetId.empty() )
            state.assetId = asset.ref; // caller-side identity for unregistered refs
        if ( state.displayName.empty() )
            state.displayName = asset.ref;
        // R1 honest lifecycle: a DECLARED state (registered/stale/offline/
        // missing...) travels verbatim — the seam never promotes it. Only a
        // resolver Unknown (path-keyed assets carry no catalog lifecycle)
        // is filled by the seam's own observation: the resolver produced a
        // complete passport over a readable dataset, which is material
        // readiness for planning. Provenance stays with the passport.
        std::string lifecycle = sicnu::state::assetLifecycleToString( state.lifecycle );
        if ( state.lifecycle == sicnu::state::AssetLifecycle::Unknown )
            lifecycle = "ready";
        resolvedLifecycle[ state.assetId ] = lifecycle;
        synthesize.passports.push_back( std::move( state ) );
    }
    const auto synthesized = sharedBroker().synthesize( synthesize );
    draft.intent = synthesized.bundle.intent;

    // 2. Passport-backed lifecycle enrichment for the SELECTED assets (the
    //    bundle drops lifecycle state by design; every field is
    //    vocabulary-validated by the projection, rejects stay honest).
    // R1: enrichment uses the resolver's OWN lifecycle verdict per asset
    // (the workspace catalog's state is merged into the passport) — a
    // stale/offline asset plans as such, never promoted to a consumable
    // hard fact by the seam.
    PlannerAssetEnrichment enrichment;
    for ( const auto &summary : synthesized.bundle.assets )
    {
        const auto it = resolvedLifecycle.find( summary.assetId );
        if ( it == resolvedLifecycle.end() )
            continue;
        sicnu::planner::PlannerAssetFacts facts;
        facts.ref = summary.assetId;
        facts.state = it->second;
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
    // The projected IR carries no output_dir (budget/determinism only); the
    // compiler derives per-step output paths from it — root the artifacts
    // deterministically (D8) so execution has somewhere honest to write.
    compile.irDoc["expectations"]["output_dir"] = mImpl->outputRoot;
    compile.goal = request.goal;
    compile.intent = draft.intent;
    Json::Value pins( Json::objectValue );
    Json::Value pinDatasets( Json::objectValue );
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
        {
            compile.inputFacts[ name ] = *understanding;
            // Identity pin from the grounding entity (WP02): what THIS plan
            // was built against. Registered assets pin entity id + revision;
            // unregistered files pin the concrete path.
            const Json::Value entity = understanding->get( "entity", Json::Value() );
            if ( !entity.isObject() )
            {
                // R1: a slot without an identity entity stays UNPINNED —
                // that degradation is surfaced, not silent.
                draft.missingFacts.append( "identity_unpinned:" + name );
            }
            else
            {
                Json::Value pin( Json::objectValue );
                if ( entity.isMember( "asset_entity_id" ) &&
                     entity[ "asset_entity_id" ].isString() &&
                     !entity[ "asset_entity_id" ].asString().empty() )
                    pin[ "asset_entity_id" ] = entity[ "asset_entity_id" ];
                if ( entity.isMember( "asset_id" ) && entity[ "asset_id" ].isString() &&
                     !entity[ "asset_id" ].asString().empty() )
                    pin[ "asset_id" ] = entity[ "asset_id" ];
                if ( entity.isMember( "path" ) && entity[ "path" ].isString() )
                    pin[ "path" ] = entity[ "path" ];
                Json::Int64 revision = 0;
                if ( entity.isMember( "revision" ) &&
                     jsonInt64IfFits( entity[ "revision" ], revision ) && revision > 0 )
                    pin[ "revision" ] = entity[ "revision" ];
                pinDatasets[ name ] = pin;
            }
        }
        else
            draft.missingFacts.append( "grounding_failed:" + ref + ":" + errorCode );
    }

    sicnu::agent::harness::HarnessError compileError;
    sicnu::agent::harness::CompiledWorkflow compiled;
    try
    {
        compiled = compileWorkflow( compile, compileError );
    }
    catch ( const std::exception &e )
    {
        // A seam boundary, not a crash site: any engine exception becomes a
        // typed invalid draft (fail closed) — the loop replans or refuses.
        draft.valid = false;
        draft.error = "COMPILE_EXCEPTION";
        draft.missingFacts.append( std::string( "compile_exception: " ) + e.what() );
        return draft;
    }
    if ( !compileError.code.empty() || compiled.executionBlocked ||
         compiled.workflowJson.empty() )
    {
        draft.valid = false;
        draft.error = compileError.code.empty() ? std::string( "COMPILE_BLOCKED" )
                                                : compileError.code;
        for ( const Json::Value &missing : compiled.missingFacts )
            appendMissingFact( draft, missing );
        return draft;
    }

    const sicnu::agent::harness::AgentPlan &plan = compiled.plan;
    draft.valid = true;
    draft.planId = plan.planId;
    draft.steps = plan.steps;
    draft.outputs = plan.outputs;
    if ( plan.inputs.isArray() && !plan.inputs.empty() )
    {
        // Bindings ride the draft so BOTH later seams (preflight, executor)
        // can re-validate identity against the CURRENT resolution without
        // re-deriving the planner's grounding (WP02/WP04).
        Json::Value bindings( Json::objectValue );
        bindings[ "inputs" ] = plan.inputs;
        if ( !pinDatasets.empty() )
        {
            pins[ "datasets" ] = pinDatasets;
            bindings[ "pins" ] = pins;
        }
        draft.bindings = bindings;
    }
    for ( const Json::Value &alternative : compiled.alternatives )
    {
        if ( !alternative.isObject() )
            continue;
        // The journal reader rejects decision alternatives without an id;
        // an advisory candidate the compiler left unnamed carries no
        // decision value — skip it instead of poisoning the journal.
        const std::string id = jsonString( alternative, "id" );
        if ( id.empty() )
            continue;
        draft.droppedAlternatives.push_back(
            { id,
              jsonString( alternative, "summary" ),
              jsonString( alternative, "why_not" ) } );
    }
    for ( const Json::Value &missing : compiled.missingFacts )
        appendMissingFact( draft, missing );

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
    using sicnu::agent::harness::validatePlanIdentity;
    using sicnu::agent_loop::PreflightIssue;

    PreflightReport report;

    // WP02/WP04 — identity re-validation BEFORE rule packs: the pins were
    // stamped at plan time; the input may have been swapped, re-registered
    // or moved to a new revision since. The harness identity gate resolves
    // both sides and refuses mismatches (IDENTITY_MISMATCH) — the session
    // then aborts with a typed diagnostic instead of running a plan built
    // against other bytes.
    if ( plan.bindings.isObject() && plan.bindings.isMember( "pins" ) )
    {
        sicnu::agent::harness::AgentPlan pinned;
        pinned.intent = plan.intent;
        pinned.inputs = plan.bindings.get( "inputs", Json::Value( Json::arrayValue ) );
        pinned.steps = plan.steps;
        pinned.pins = plan.bindings[ "pins" ];
        const sicnu::agent::harness::HarnessError identityError = validatePlanIdentity( pinned );
        if ( !identityError.code.empty() )
        {
            report.verdict = "blocked";
            PreflightIssue issue;
            issue.code = identityError.code;
            issue.severity = "error";
            issue.message = identityError.summary;
            report.issues.push_back( issue );
            report.checks = identityError.details;
            return report;
        }
    }

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

    // The rule pack consumes the plan's OWN inputs array (the exact
    // document shape harness:execute_plan gates on — check and execute see
    // the same normalized inputs; WP04). Plans without bindings fall back
    // to the session snapshot's slot refs in the same array shape.
    Json::Value refs( Json::arrayValue );
    if ( plan.bindings.isObject() && plan.bindings.get( "inputs", Json::Value() ).isArray() &&
         !plan.bindings[ "inputs" ].empty() )
    {
        refs = plan.bindings[ "inputs" ];
    }
    else
    {
        for ( const AssetFact &asset : snapshot.assets )
        {
            if ( asset.slot.empty() || asset.ref.empty() )
                continue;
            Json::Value entry( Json::objectValue );
            entry[ "name" ] = asset.slot;
            entry[ "ref" ] = asset.ref;
            refs.append( entry );
        }
    }

    const sicnu::agent::harness::PreflightOutcome outcome = preflightIntent( plan.intent, refs );
    report.verdict = outcome.verdict;
    // The loop's checks field is an array of STRINGS (session evidence
    // streams each entry); preflightIntent's checks are objects — render
    // each compactly instead of appending verbatim.
    report.checks = Json::Value( Json::arrayValue );
    for ( const Json::Value &check : outcome.checks )
    {
        if ( check.isString() )
        {
            report.checks.append( check.asString() );
            continue;
        }
        const std::string code = jsonString( check, "code" );
        const std::string name = jsonString( check, "check" );
        const bool passed = check.get( "passed", false ).isBool()
                                ? check[ "passed" ].asBool()
                                : false;
        report.checks.append( ( code.empty() ? name : code + ": " + name ) +
                              ( passed ? " [passed]" : " [failed]" ) );
    }
    for ( const Json::Value &issue : outcome.issues )
    {
        if ( !issue.isObject() )
            continue;
        PreflightIssue mapped;
        mapped.code = jsonString( issue, "code" );
        mapped.severity = jsonString( issue, "severity", "error" );
        mapped.message = jsonString( issue, "message" );
        if ( mapped.message.empty() )
            mapped.message = Json::writeString( Json::StreamWriterBuilder(), issue );
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
/// the engine runs exactly what was preflighted). Plan-time bindings restore
/// the plan's inputs and identity pins.
sicnu::agent::harness::AgentPlan agentPlanFromDraft( const PlanDraft &draft )
{
    using sicnu::agent::harness::AgentPlan;
    AgentPlan plan;
    plan.planId = draft.planId;
    plan.goal = std::string();
    plan.intent = draft.intent;
    plan.inputs = draft.bindings.isObject()
                      ? draft.bindings.get( "inputs", Json::Value( Json::arrayValue ) )
                      : Json::Value( Json::arrayValue );
    plan.pins = draft.bindings.isObject() ? draft.bindings.get( "pins", Json::Value() )
                                          : Json::Value();
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
            outcome.state = "running";
            break;
        case WorkflowRunState::Interrupted:
            // R1: terminal (crash-recovered); the loop's "running" spelling
            // is reserved for live runs, and a typed code keeps the
            // diagnoser from misattributing the failure.
            outcome.state = "failed";
            outcome.errorCode = "RUN_INTERRUPTED";
            outcome.errorMessage = run->errorMessage();
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
    // A cancelled run must deliver nothing. Individual steps may already be
    // marked Completed with a planned output path before cancellation lands;
    // publishing those would falsely claim a deliverable for a cancelled run.
    if ( outcome.state == "cancelled" )
        outcome.artifacts.clear();
    return outcome;
}

} // namespace

ExecutionStart HarnessExecutor::begin( const PlanDraft &plan )
{
    using sicnu::agent::harness::compilePlanToWorkflowJson;
    using sicnu::agent::harness::HarnessError;
    using sicnu::agent::harness::validatePlanIdentity;
    using sicnu::workflow::WorkflowRunCoordinator;

    ExecutionStart start;
    if ( !plan.valid )
    {
        start.error = "PLAN_INVALID";
        return start;
    }

    const sicnu::agent::harness::AgentPlan executable = agentPlanFromDraft( plan );

    // The harness identity gate at the trust transition (WP02/WP04): pins
    // bound at plan time must still resolve to the same datasets. This
    // re-checks what preflight validated — check and execute see the SAME
    // normalized plan document.
    const HarnessError identityError = validatePlanIdentity( executable );
    if ( !identityError.code.empty() )
    {
        start.error = identityError.code;
        return start;
    }

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

    // Bounded run bookkeeping (same discipline as the harness ledgers).
    // Eviction is BY KEY ORDER, not insertion order (a std::map) — the
    // bound protects memory, not LRU fairness; the coordinator's
    // pipelineIdForRun remains the authoritative fallback lookup.
    // Single-threaded by construction: the CLI driver is one thread and
    // MCP dispatches tools on the Qt event loop (serialized).
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
    // The grounding chain (data state AND the planner's slot facts) rides on
    // the builtin spatial tools (spatial:understand → raster/vector
    // inspect). The MCP host registers them at server start; headless hosts
    // (CLI) do not — install here so the seam bundle is self-sufficient in
    // any process. Idempotent (guarded in the registry).
    sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerBuiltinTools();
    sicnu::agent::harness::registerGroundingTools();
    bundle.data = std::make_unique< HarnessDataStateProvider >();
    if ( !HarnessPlanner::create( bundle.planner, bundle.error ) )
        return bundle; // planner authority unavailable — fail closed at the gate
    bundle.preflight = std::make_unique< HarnessPreflight >();
    bundle.executor = std::make_unique< HarnessExecutor >();
    bundle.verifier = std::make_unique< HarnessVerifier >();
    bundle.diagnoser = std::make_unique< HarnessDiagnoser >();
    return bundle;
}

sicnu::agent_ops::OpsDriver *productionAgentOpsDriver()
{
    // One assembly for every host (MCP --mcp, CLI session, desktop Control
    // Center) — per-host copies are how the R28 seams drifted apart in the
    // first place. The WHOLE bundle (seams, provider, driver) is
    // intentionally leaked heap state: the driver and its seams reference
    // process-lifetime singletons (WorkflowRunCoordinator, registries)
    // whose own destruction order at exit is unspecified, so a destructing
    // static could run after one of those is gone (R1-P3: leak seams and
    // provider too — they are part of the same assembly, not just the
    // driver).
    struct ProductionOps
    {
        ProductionSeamBundle seams;
        sicnu::repair::JsonRepairCapabilityProvider repairProvider;
        bool repairReady = false;
        std::unique_ptr<sicnu::agent_ops::OpsDriver> driver;
    };
    static ProductionOps *const ops = [] {
        auto *bundle = new ProductionOps();
        bundle->seams = makeProductionSeamBundle();
        sicnu::repair::RepairError error;
        if ( !sicnu::agent::harness::liveRepairCapabilityProvider( bundle->repairProvider,
                                                                   error ) )
        {
            // Fail-closed: the recovery bridge keeps its typed
            // no_safe_repair refusal (null provider) — no degraded subset.
            qWarning( "productionAgentOpsDriver: live repair capability provider "
                      "unavailable (%s: %s); recovery stays advisory-only",
                      error.code.c_str(), error.message.c_str() );
            bundle->repairReady = false;
        }
        else
        {
            bundle->repairReady = true;
        }
        sicnu::agent_ops::OpsDriver::Options options;
        options.deps.seams.data = bundle->seams.data.get();
        options.deps.seams.planner = bundle->seams.planner.get();
        options.deps.seams.preflight = bundle->seams.preflight.get();
        options.deps.seams.executor = bundle->seams.executor.get();
        options.deps.seams.verifier = bundle->seams.verifier.get();
        options.deps.seams.diagnoser = bundle->seams.diagnoser.get();
        options.deps.repairCapabilityProvider =
            bundle->repairReady ? &bundle->repairProvider : nullptr;
        bundle->driver = std::make_unique<sicnu::agent_ops::OpsDriver>( std::move( options ) );
        return bundle;
    }();
    return ops->driver.get();
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

    // R1: mixed deliveries (raster + vector) must not verify a vector
    // artifact against raster expectations. Match the artifact path to the
    // output whose from_step produced it (basename compare — the executor
    // reports the engine's concrete output paths); an unmatched artifact
    // keeps the structural defaults (fail-closed, never a guessed kind).
    QString pathBasename = QString::fromStdString( path ).section( '/', -1 );
    const Json::Value *matched = nullptr;
    for ( const Json::Value &output : plan.outputs )
    {
        if ( !output.isObject() )
            continue;
        const std::string fromStep = jsonString( output, "from_step" );
        if ( fromStep.empty() )
            continue;
        // step id segment of the produced file: derivedOutputPath uses
        // "<ir-id>_<step-id>.tif" — match the "_<step-id>." token so
        // "step-1" never matches "step-10" (R2).
        if ( !pathBasename.contains( QLatin1Char( '_' ) + QString::fromStdString( fromStep ) +
                                     QLatin1Char( '.' ) ) )
            continue;
        matched = &output;
        break;
    }
    const Json::Value *kindSource = matched;
    // Fallback (documented minimal shape): when no path mapping matched,
    // the FIRST output naming a known kind provides the expectations — but
    // ONLY for single-kind plans; a mixed plan with an unmatched artifact
    // keeps structural defaults (a guessed kind would falsely FAIL the
    // other family).
    if ( !matched )
    {
        std::string onlyKind;
        bool singleKind = true;
        for ( const Json::Value &output : plan.outputs )
        {
            if ( !output.isObject() )
                continue;
            const Json::Value &kind = output[ "kind" ];
            if ( !kind.isString() )
                continue;
            const std::string value = kind.asString();
            if ( value != "raster" && value != "vector" && value != "map" )
                continue;
            if ( onlyKind.empty() )
                onlyKind = value;
            else if ( onlyKind != value )
                singleKind = false;
            if ( !kindSource )
                kindSource = &output;
        }
        if ( !singleKind )
            kindSource = nullptr;
    }
    if ( kindSource )
    {
        const std::string kind = jsonString( *kindSource, "kind" );
        if ( kind == "raster" || kind == "vector" || kind == "map" )
        {
            expectations.kind = kind;
            const std::string crs = jsonString( *kindSource, "crs" );
            if ( !crs.empty() )
                expectations.crs = crs;
        }
    }
    return expectations;
}

} // namespace

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

} // namespace sicnu::agent
