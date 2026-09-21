// src/agent_loop/scientific_agent_session.cpp
#include "scientific_agent_session.h"

#include <json/writer.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace sicnu::agent_loop {
namespace {

std::string trimCopy( const std::string &text )
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while ( begin < end && std::isspace( static_cast< unsigned char >( text[ begin ] ) ) )
        ++begin;
    while ( end > begin && std::isspace( static_cast< unsigned char >( text[ end - 1 ] ) ) )
        --end;
    return text.substr( begin, end - begin );
}

DecisionEvidence evidence( const std::string &kind, const std::string &ref )
{
    return DecisionEvidence{ kind, ref };
}

/// Session ids are generated when the caller did not supply one. The
/// process-wide counter keeps them unique within a process; callers that
/// need cross-process determinism pass their own id.
std::atomic< int > sSessionCounter{ 0 };

} // namespace

bool teachingGateBlocksExecution( const TeachingPolicy &policy )
{
    // The role must ALREADY be normalized by the caller (the production
    // adapter runs it through harness_actions::normalizeLabRole, where an
    // empty raw role degrades to "student"). An empty role here means "no
    // role asserted" — agent semantics.
    return policy.intentDomain == "lab" && policy.role == "student";
}

ScientificAgentSession::ScientificAgentSession( SessionPolicy policy, Dependencies deps,
                                                std::function< long long() > clock,
                                                std::string sessionId )
    : mPolicy( std::move( policy ) ), mDeps( deps ),
      mJournal( sessionId.empty()
                    ? "session-" + std::to_string( ++sSessionCounter )
                    : std::move( sessionId ) ),
      mClock( std::move( clock ) )
{
}

long long ScientificAgentSession::now()
{
    if ( mClock )
        return mClock();
    return mClockValue++;
}

bool ScientificAgentSession::enterStage( const std::string &stage )
{
    const TransitionResult result = mMachine.advance( stage );
    if ( !result.ok )
    {
        // The driver only walks legal edges; anything else is an internal
        // invariant break and fails closed rather than skipping a stage.
        note( "internal_error", stage,
              Json::Value( result.error.code + ": " + result.error.message ) );
        abort( stop_reasons::kInternalError );
        return false;
    }
    if ( isKnownTerminalState( stage ) )
    {
        // Reaching a terminal state is recorded as the terminal event;
        // the visited-stage list holds running stages only.
        note( "terminal", stage, Json::Value( Json::objectValue ) );
        return true;
    }
    note( "stage_enter", stage, Json::Value( Json::objectValue ) );
    mVisitedStages.push_back( stage );
    return true;
}

void ScientificAgentSession::note( const std::string &event, const std::string &stage,
                                   Json::Value payload )
{
    mJournal.append( event, stage, std::move( payload ), now() );
}

void ScientificAgentSession::recordDecision( const std::string &stage, DecisionRecord decision )
{
    decision.schemaVersion = kDecisionRecordSchemaVersion;
    decision.decisionId = "decision-" + std::to_string( ++mDecisionSeq );
    decision.sessionId = mJournal.sessionId();
    decision.stage = stage;
    decision.recordedAt = now();
    if ( !decision.policy.isObject() || decision.policy.empty() )
    {
        decision.policy[ "policy_id" ] = mPolicy.policyId();
        decision.policy[ "version" ] = mPolicy.policyVersion();
    }
    mJournal.append( "decision", stage, Json::Value( Json::objectValue ), now(), decision );
    mDecisions.push_back( std::move( decision ) );
}

bool ScientificAgentSession::refuse( const std::string &stopReason )
{
    mStopReason = stopReason;
    const TransitionResult result = mMachine.terminate( terminal_states::kRefused, stopReason );
    ( void )result; // legal from every running stage by construction
    note( "terminal", terminal_states::kRefused, Json::Value( stopReason ) );
    return false;
}

bool ScientificAgentSession::abort( const std::string &stopReason )
{
    mStopReason = stopReason;
    const TransitionResult result = mMachine.terminate( terminal_states::kAborted, stopReason );
    ( void )result; // legal from every running stage by construction
    note( "terminal", terminal_states::kAborted, Json::Value( stopReason ) );
    return false;
}

SessionResult ScientificAgentSession::run( const SessionRunRequest &request )
{
    SessionResult result;
    result.sessionId = mJournal.sessionId();
    mGoal = request.goal;

    // The machine starts at goal_normalization; record the entry so the
    // journal shows every stage the session visited, including the first.
    note( "stage_enter", mMachine.stage(), Json::Value( Json::objectValue ) );
    mVisitedStages.push_back( mMachine.stage() );

    std::string policyError;
    if ( !mPolicy.validate( &policyError ) )
    {
        DecisionRecord decision;
        decision.inputs[ "policy_error" ] = policyError;
        decision.selected[ "action" ] = "refuse";
        decision.reason = "session policy cannot be bounded: " + policyError;
        decision.evidence.push_back( evidence( "policy", "validate" ) );
        recordDecision( stages::kGoalNormalization, std::move( decision ) );
        refuse( stop_reasons::kInvalidPolicy );
    }

    while ( !mMachine.terminal() )
    {
        ++mStepCount;
        if ( mStepCount > mPolicy.maxSteps )
        {
            abort( stop_reasons::kStepLimit );
            break;
        }
        if ( mCancelRequested.load() )
        {
            abort( stop_reasons::kCancelled );
            break;
        }

        const std::string stage = mMachine.stage();
        bool keepGoing = true;
        if ( stage == stages::kGoalNormalization )
            keepGoing = stageGoalNormalization( request );
        else if ( stage == stages::kDataStateSnapshot )
            keepGoing = stageDataStateSnapshot( request );
        else if ( stage == stages::kPlanRequest )
            keepGoing = stagePlanRequest( request );
        else if ( stage == stages::kPreflight )
            keepGoing = stagePreflight();
        else if ( stage == stages::kRepairApproval )
            keepGoing = stageRepairApproval();
        else if ( stage == stages::kExecute )
            keepGoing = stageExecute();
        else if ( stage == stages::kVerify )
            keepGoing = stageVerify();
        else if ( stage == stages::kDiagnose )
            keepGoing = stageDiagnose();
        else if ( stage == stages::kReplan )
            keepGoing = stageReplan();
        else if ( stage == stages::kDelivery )
            keepGoing = stageDelivery();
        else
            keepGoing = abort( stop_reasons::kInternalError );

        if ( !keepGoing )
            break;
    }

    result.ok = mMachine.terminalState() == terminal_states::kDelivered;
    result.terminalState = mMachine.terminalState();
    result.stopReason = mStopReason;
    result.summary = buildSummary();
    result.journal = mJournal;
    return result;
}

bool ScientificAgentSession::stageGoalNormalization( const SessionRunRequest &request )
{
    DecisionRecord decision;
    decision.inputs[ "goal" ] = request.goal;
    decision.inputs[ "intent_hint" ] = request.intent;
    const std::string trimmed = trimCopy( request.goal );
    if ( trimmed.empty() )
    {
        decision.selected[ "action" ] = "refuse";
        decision.reason = "goal is empty after normalization";
        decision.evidence.push_back( evidence( "policy", "non_empty_goal" ) );
        recordDecision( stages::kGoalNormalization, std::move( decision ) );
        return refuse( stop_reasons::kInvalidGoal );
    }
    decision.selected[ "action" ] = "accept_goal";
    decision.reason = "goal accepted as stated: " + trimmed;
    recordDecision( stages::kGoalNormalization, std::move( decision ) );
    return enterStage( stages::kDataStateSnapshot );
}

bool ScientificAgentSession::stageDataStateSnapshot( const SessionRunRequest &request )
{
    if ( !mDeps.data )
        return refuse( stop_reasons::kInternalError );
    mSnapshot = mDeps.data->snapshot( mGoal, request.refs );

    DecisionRecord decision;
    Json::Value slots( Json::arrayValue );
    Json::Value unresolved( Json::arrayValue );
    for ( const AssetFact &asset : mSnapshot.assets )
    {
        slots.append( asset.slot );
        if ( asset.resolved )
            decision.evidence.push_back( evidence( "fact", "slot:" + asset.slot ) );
        else
        {
            unresolved.append( asset.slot );
            decision.evidence.push_back( evidence( "fact", "slot:" + asset.slot + ":unresolved" ) );
        }
    }
    decision.inputs[ "slots" ] = slots;
    decision.inputs[ "unresolved_slots" ] = unresolved;
    decision.selected[ "action" ] = "snapshot";
    decision.reason = "data state captured from facts for " +
                      std::to_string( mSnapshot.assets.size() ) + " declared slot(s)";
    recordDecision( stages::kDataStateSnapshot, std::move( decision ) );
    return enterStage( stages::kPlanRequest );
}

bool ScientificAgentSession::stagePlanRequest( const SessionRunRequest &request )
{
    if ( !mDeps.planner )
        return refuse( stop_reasons::kInternalError );

    PlanRequest planRequest;
    planRequest.goal = mGoal;
    planRequest.intent = request.intent;
    planRequest.snapshot = mSnapshot;
    planRequest.approvedRepairs = mApprovedRepairs;
    planRequest.diagnosis = mDiagnosis.toJson();
    planRequest.attempt = mAttempt;

    const PlanDraft draft = mDeps.planner->plan( planRequest );
    if ( !draft.valid )
    {
        DecisionRecord decision;
        decision.inputs[ "attempt" ] = mAttempt;
        decision.inputs[ "error" ] = draft.error;
        for ( const DecisionAlternative &alt : draft.droppedAlternatives )
            decision.alternatives.push_back( alt );
        for ( const Json::Value &missing : draft.missingFacts )
            decision.evidence.push_back( evidence( "fact", "missing:" + missing.asString() ) );
        decision.selected[ "action" ] = "refuse";
        decision.reason = "planner returned no valid plan (" + draft.error + ")";
        recordDecision( stages::kPlanRequest, std::move( decision ) );
        mLastFailureCode = draft.error.empty() ? stop_reasons::kInvalidPlan : draft.error;
        return refuse( stop_reasons::kInvalidPlan );
    }

    // Resource budget gate: the plan's declared estimates must fit the
    // session budget BEFORE anything executes.
    if ( mPolicy.resourceBudgetMb > 0 && draft.totalRamMb() > mPolicy.resourceBudgetMb )
    {
        DecisionRecord decision;
        decision.inputs[ "plan_estimate_mb" ] =
            static_cast< Json::Int64 >( draft.totalRamMb() );
        decision.inputs[ "resource_budget_mb" ] =
            static_cast< Json::Int64 >( mPolicy.resourceBudgetMb );
        decision.selected[ "action" ] = "abort";
        decision.reason = "plan estimate " + std::to_string( draft.totalRamMb() ) +
                          " MB exceeds the session resource budget " +
                          std::to_string( mPolicy.resourceBudgetMb ) + " MB";
        decision.evidence.push_back( evidence( "policy", "resource_budget_mb" ) );
        recordDecision( stages::kPlanRequest, std::move( decision ) );
        return abort( stop_reasons::kResourceOverBudget );
    }

    mPlan = draft;
    DecisionRecord decision;
    decision.inputs[ "attempt" ] = mAttempt;
    decision.inputs[ "intent" ] = draft.intent;
    decision.inputs[ "plan_id" ] = draft.planId;
    for ( const DecisionAlternative &alt : draft.droppedAlternatives )
        decision.alternatives.push_back( alt );
    for ( const Json::Value &missing : draft.missingFacts )
        decision.evidence.push_back( evidence( "fact", "missing:" + missing.asString() ) );
    decision.selected[ "action" ] = "use_plan";
    decision.selected[ "plan_id" ] = draft.planId;
    decision.selected[ "fingerprint" ] = draft.fingerprint;
    decision.reason = "planner returned a valid plan for intent '" + draft.intent + "'";
    recordDecision( stages::kPlanRequest, std::move( decision ) );
    return enterStage( stages::kPreflight );
}

bool ScientificAgentSession::stagePreflight()
{
    if ( !mDeps.preflight )
        return refuse( stop_reasons::kInternalError );
    mPreflightReport = mDeps.preflight->check( mPlan, mSnapshot );

    const bool executeMode = mPolicy.mode == RunMode::ExecuteWithVerify;
    if ( mPreflightReport.verdict == "blocked" )
    {
        DecisionRecord decision;
        decision.inputs[ "verdict" ] = mPreflightReport.verdict;
        Json::Value issues( Json::arrayValue );
        for ( const PreflightIssue &issue : mPreflightReport.issues )
            issues.append( issue.code + ": " + issue.message );
        decision.inputs[ "issues" ] = issues;
        decision.selected[ "action" ] = "refuse";
        decision.reason = "preflight blocked the plan; execution is never attempted";
        for ( const Json::Value &check : mPreflightReport.checks )
            decision.evidence.push_back( evidence( "preflight", check.asString() ) );
        recordDecision( stages::kPreflight, std::move( decision ) );
        mLastFailureCode = stop_reasons::kPreflightBlocked;
        return refuse( stop_reasons::kPreflightBlocked );
    }

    if ( mPreflightReport.verdict == "fixable" )
    {
        if ( !executeMode && mPolicy.mode == RunMode::DryRun )
        {
            // dry_run reports what WOULD happen, including the approvals a
            // real run would need; it never applies repairs itself.
            DecisionRecord decision;
            decision.inputs[ "verdict" ] = "fixable";
            Json::Value proposals( Json::arrayValue );
            for ( const RepairProposal &proposal : mPreflightReport.proposals )
            {
                const bool autoApproved =
                    mPolicy.repairApproval.classAutoApproved( proposal.riskClass );
                Json::Value entry( Json::objectValue );
                entry[ "rule_id" ] = proposal.ruleId;
                entry[ "risk_class" ] = proposal.riskClass;
                entry[ "would_auto_approve" ] = autoApproved;
                proposals.append( entry );
                decision.evidence.push_back( evidence( "preflight", "proposal:" + proposal.ruleId ) );
            }
            decision.inputs[ "proposals" ] = proposals;
            decision.selected[ "action" ] = "report";
            decision.reason = "dry run reports fixable preflight without applying repairs";
            recordDecision( stages::kPreflight, std::move( decision ) );
            return enterStage( stages::kDelivery );
        }
        return enterStage( stages::kRepairApproval );
    }

    // ok
    DecisionRecord decision;
    decision.inputs[ "verdict" ] = "ok";
    decision.selected[ "action" ] = executeMode ? "proceed_to_execute" : "proceed_to_delivery";
    decision.reason = executeMode ? "preflight ok; execution may proceed"
                                  : "preflight ok; plan reported without execution";
    recordDecision( stages::kPreflight, std::move( decision ) );
    return enterStage( executeMode ? stages::kExecute : stages::kDelivery );
}

bool ScientificAgentSession::stageRepairApproval()
{
    std::vector< std::string > approved;
    bool anyWithheld = false;
    for ( const RepairProposal &proposal : mPreflightReport.proposals )
    {
        const bool autoApproved =
            mPolicy.repairApproval.classAutoApproved( proposal.riskClass );
        DecisionRecord decision;
        decision.inputs[ "rule_id" ] = proposal.ruleId;
        decision.inputs[ "risk_class" ] = proposal.riskClass;
        decision.inputs[ "operator_id" ] = proposal.operatorId;
        decision.evidence.push_back( evidence( "preflight", "proposal:" + proposal.ruleId ) );
        decision.evidence.push_back( evidence( "policy",
                                               "auto_approve:" + proposal.riskClass ) );
        if ( autoApproved )
        {
            DecisionAlternative withheld;
            withheld.id = "withhold";
            withheld.description = "do not apply the repair";
            withheld.whyNot = "risk class '" + proposal.riskClass +
                              "' is auto-approved by the session policy";
            decision.alternatives.push_back( withheld );
            decision.selected[ "action" ] = "approve_repair";
            decision.selected[ "rule_id" ] = proposal.ruleId;
            decision.reason = "repair '" + proposal.ruleId + "' has risk class '" +
                              proposal.riskClass + "', which the policy auto-approves";
            approved.push_back( proposal.ruleId );
        }
        else
        {
            anyWithheld = true;
            DecisionAlternative approve;
            approve.id = "approve";
            approve.description = "apply the repair";
            approve.whyNot = "risk class '" + proposal.riskClass +
                             "' is not auto-approved; it requires an explicit decision";
            decision.alternatives.push_back( approve );
            decision.selected[ "action" ] = "withhold_repair";
            decision.selected[ "rule_id" ] = proposal.ruleId;
            decision.reason = "repair '" + proposal.ruleId + "' has risk class '" +
                              proposal.riskClass +
                              "'; policy does not auto-approve it";
        }
        recordDecision( stages::kRepairApproval, std::move( decision ) );
    }

    if ( !approved.empty() )
    {
        for ( const std::string &ruleId : approved )
            mApprovedRepairs.push_back( ruleId );
        return enterStage( stages::kReplan );
    }

    if ( anyWithheld && !mPolicy.repairApproval.allowUnapprovedFixable )
    {
        DecisionRecord decision;
        decision.selected[ "action" ] = "refuse";
        decision.reason = "fixable preflight has no auto-approvable repair and the policy "
                          "forbids proceeding unfixed";
        decision.evidence.push_back( evidence( "policy", "allow_unapproved_fixable" ) );
        recordDecision( stages::kRepairApproval, std::move( decision ) );
        mLastFailureCode = stop_reasons::kPreflightBlocked;
        return refuse( stop_reasons::kPreflightBlocked );
    }

    if ( anyWithheld )
    {
        // Policy explicitly allows proceeding with the issues unfixed; the
        // decision is recorded, never silent.
        DecisionRecord decision;
        decision.selected[ "action" ] = "proceed_unfixed";
        decision.reason = "policy allows proceeding despite unapproved repairs";
        decision.evidence.push_back( evidence( "policy", "allow_unapproved_fixable" ) );
        recordDecision( stages::kRepairApproval, std::move( decision ) );
    }

    return enterStage( mPolicy.mode == RunMode::ExecuteWithVerify ? stages::kExecute
                                                                  : stages::kDelivery );
}

bool ScientificAgentSession::stageExecute()
{
    if ( teachingGateBlocksExecution( mPolicy.teaching ) )
    {
        DecisionRecord decision;
        decision.selected[ "action" ] = "withhold_execution";
        decision.reason = "teaching constraint withholds artifact-producing execution";
        decision.evidence.push_back( evidence( "policy", "teaching_constraint" ) );
        recordDecision( stages::kExecute, std::move( decision ) );
        mLastFailureCode = stop_reasons::kTeachingRefusal;
        return refuse( stop_reasons::kTeachingRefusal );
    }
    if ( !mDeps.executor )
        return refuse( stop_reasons::kInternalError );

    const ExecutionStart start = mDeps.executor->begin( mPlan );
    if ( !start.started )
    {
        mLastFailureCode = start.error.empty() ? stop_reasons::kExecutionFailed : start.error;
        DecisionRecord decision;
        decision.inputs[ "error" ] = mLastFailureCode;
        decision.selected[ "action" ] = "diagnose";
        decision.reason = "executor refused to start the plan (" + mLastFailureCode + ")";
        recordDecision( stages::kExecute, std::move( decision ) );
        return enterStage( stages::kDiagnose );
    }

    {
        DecisionRecord decision;
        decision.inputs[ "run_id" ] = start.runId;
        decision.selected[ "action" ] = "run";
        decision.reason = "plan submitted through the executor seam";
        decision.evidence.push_back( evidence( "engine", "run:" + start.runId ) );
        recordDecision( stages::kExecute, std::move( decision ) );
    }

    mOutcome = mDeps.executor->poll( start, mPolicy.executorTimeoutMs );

    if ( mCancelRequested.load() )
    {
        mDeps.executor->cancel( start );
        return abort( stop_reasons::kCancelled );
    }
    if ( !mOutcome.finished )
    {
        mDeps.executor->cancel( start );
        DecisionRecord decision;
        decision.inputs[ "run_id" ] = start.runId;
        decision.selected[ "action" ] = "abort";
        decision.reason = "executor did not finish within the session timeout";
        decision.evidence.push_back( evidence( "engine", "run:" + start.runId ) );
        recordDecision( stages::kExecute, std::move( decision ) );
        return abort( stop_reasons::kExecutorTimeout );
    }
    if ( !mOutcome.succeeded )
    {
        mLastFailureCode =
            mOutcome.errorCode.empty() ? stop_reasons::kExecutionFailed : mOutcome.errorCode;
        DecisionRecord decision;
        decision.inputs[ "run_id" ] = start.runId;
        decision.inputs[ "error_code" ] = mLastFailureCode;
        decision.selected[ "action" ] = "diagnose";
        decision.reason = "execution failed with " + mLastFailureCode + ": " +
                          mOutcome.errorMessage;
        decision.evidence.push_back( evidence( "engine", "run:" + start.runId ) );
        recordDecision( stages::kExecute, std::move( decision ) );
        return enterStage( stages::kDiagnose );
    }

    {
        DecisionRecord decision;
        decision.inputs[ "run_id" ] = start.runId;
        decision.selected[ "action" ] = "verify";
        decision.reason = "execution succeeded; outputs go to verification";
        for ( const std::string &artifact : mOutcome.artifacts )
            decision.evidence.push_back( evidence( "engine", "artifact:" + artifact ) );
        recordDecision( stages::kExecute, std::move( decision ) );
    }
    return enterStage( stages::kVerify );
}

bool ScientificAgentSession::stageVerify()
{
    if ( !mDeps.verifier )
        return refuse( stop_reasons::kInternalError );
    mVerified = true;
    mVerification = mDeps.verifier->verify( mPlan, mOutcome );

    DecisionRecord decision;
    decision.inputs[ "verdict" ] = mVerification.verdict();
    for ( const ArtifactVerificationReport &artifact : mVerification.artifacts )
        for ( const std::string &warning : artifact.warnings )
            decision.evidence.push_back( evidence( "verification",
                                                   artifact.path + ":" + warning ) );

    if ( mVerification.verdict() == "FAIL" )
    {
        mLastFailureCode = stop_reasons::kOutputInvalid;
        decision.selected[ "action" ] = "diagnose";
        decision.reason = "verification FAILED; the session never reports success";
        recordDecision( stages::kVerify, std::move( decision ) );
        return enterStage( stages::kDiagnose );
    }

    decision.selected[ "action" ] = "deliver";
    decision.reason = mVerification.verdict() == "PASS_WITH_WARNINGS"
                          ? "verification passed with warnings; they ride along"
                          : "verification passed";
    recordDecision( stages::kVerify, std::move( decision ) );
    return enterStage( stages::kDelivery );
}

bool ScientificAgentSession::stageDiagnose()
{
    if ( !mDeps.diagnoser )
        return refuse( stop_reasons::kInternalError );
    mDiagnosis = mDeps.diagnoser->diagnose( mPlan, mOutcome, mVerification );

    if ( mDiagnosis.proposals.empty() )
    {
        DecisionRecord decision;
        decision.inputs[ "root_cause_code" ] = mDiagnosis.rootCauseCode;
        decision.selected[ "action" ] = "refuse";
        decision.reason = "diagnosis found no repair proposal; the session stops with a "
                          "typed reason instead of retrying";
        decision.evidence.push_back( evidence( "diagnosis", mDiagnosis.rootCauseCode ) );
        recordDecision( stages::kDiagnose, std::move( decision ) );
        return refuse( mLastFailureCode.empty() ? stop_reasons::kExecutionFailed
                                                : mLastFailureCode );
    }

    DecisionRecord decision;
    decision.inputs[ "root_cause_code" ] = mDiagnosis.rootCauseCode;
    for ( const RepairProposal &proposal : mDiagnosis.proposals )
    {
        DecisionAlternative alt;
        alt.id = proposal.ruleId;
        alt.description = "repair '" + proposal.ruleId + "' (" + proposal.operatorId + ")";
        alt.whyNot = ""; // the options are the proposals; the choice is to replan
        decision.alternatives.push_back( alt );
        decision.evidence.push_back( evidence( "diagnosis", "proposal:" + proposal.ruleId ) );
    }
    decision.selected[ "action" ] = "replan";
    decision.reason = "diagnosis produced " +
                      std::to_string( mDiagnosis.proposals.size() ) +
                      " repair proposal(s); the session replans within its budget";
    recordDecision( stages::kDiagnose, std::move( decision ) );
    return enterStage( stages::kReplan );
}

bool ScientificAgentSession::stageReplan()
{
    if ( mMachine.replanCount() > mPolicy.maxReplans )
    {
        DecisionRecord decision;
        decision.inputs[ "replans_used" ] = mMachine.replanCount();
        decision.inputs[ "max_replans" ] = mPolicy.maxReplans;
        decision.selected[ "action" ] = "abort";
        decision.reason = "replan budget exhausted";
        decision.evidence.push_back( evidence( "policy", "max_replans" ) );
        recordDecision( stages::kReplan, std::move( decision ) );
        return abort( stop_reasons::kReplanLimit );
    }

    if ( !mLastFailureCode.empty() )
    {
        const std::string key = mPlan.identity + "|" + mLastFailureCode;
        const int repeats = ++mFailureKeys[ key ];
        if ( repeats >= mPolicy.noProgressThreshold )
        {
            DecisionRecord decision;
            decision.inputs[ "failure_key" ] = key;
            decision.inputs[ "repeats" ] = repeats;
            decision.selected[ "action" ] = "abort";
            decision.reason = "the same science failed the same way " +
                              std::to_string( repeats ) +
                              " times; no progress is possible";
            decision.evidence.push_back( evidence( "policy", "no_progress_threshold" ) );
            recordDecision( stages::kReplan, std::move( decision ) );
            return abort( stop_reasons::kNoProgress );
        }
    }

    DecisionRecord decision;
    decision.inputs[ "attempt" ] = mAttempt;
    decision.inputs[ "replans_used" ] = mMachine.replanCount();
    decision.inputs[ "max_replans" ] = mPolicy.maxReplans;
    decision.selected[ "action" ] = "replan";
    decision.reason = "bounded replan after diagnosis (budget " +
                      std::to_string( mMachine.replanCount() ) + " of " +
                      std::to_string( mPolicy.maxReplans ) + " used)";
    decision.evidence.push_back( evidence( "policy", "max_replans" ) );
    recordDecision( stages::kReplan, std::move( decision ) );

    ++mAttempt;
    return enterStage( stages::kPlanRequest );
}

bool ScientificAgentSession::stageDelivery()
{
    DecisionRecord decision;
    decision.inputs[ "mode" ] = runModeToString( mPolicy.mode );
    if ( mPolicy.mode == RunMode::DryRun )
    {
        decision.selected[ "action" ] = "deliver_would_execute";
        decision.reason = "dry run delivers the plan that would have executed; nothing ran";
    }
    else if ( mPolicy.mode == RunMode::PlanOnly )
    {
        decision.selected[ "action" ] = "deliver_plan";
        decision.reason = "plan-only session delivers the preflighted plan; nothing ran";
    }
    else
    {
        decision.selected[ "action" ] = "deliver";
        decision.reason = "executed and verified; evidence summary delivered";
        decision.evidence.push_back( evidence( "verification", mVerification.verdict() ) );
    }
    recordDecision( stages::kDelivery, std::move( decision ) );
    return enterStage( terminal_states::kDelivered );
}

Json::Value EvidenceSummary::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "schema_version" ] = schemaVersion;
    doc[ "session_id" ] = sessionId;
    doc[ "goal" ] = goal;
    doc[ "mode" ] = mode;
    doc[ "outcome" ] = outcome;
    doc[ "stop_reason" ] = stopReason;
    doc[ "policy" ] = policy;
    doc[ "verification_verdict" ] = verificationVerdict;
    Json::Value stagesDoc( Json::arrayValue );
    for ( const std::string &stage : stages )
        stagesDoc.append( stage );
    doc[ "stages" ] = stagesDoc;
    Json::Value decisionsDoc( Json::arrayValue );
    for ( const DecisionRecord &decision : decisions )
        decisionsDoc.append( decision.toJson() );
    doc[ "decisions" ] = decisionsDoc;
    Json::Value artifactsDoc( Json::arrayValue );
    for ( const std::string &artifact : artifacts )
        artifactsDoc.append( artifact );
    doc[ "artifacts" ] = artifactsDoc;
    doc[ "budgets" ] = budgets;
    doc[ "journal_entries" ] = static_cast< Json::Int64 >( journalEntries );
    doc[ "replay" ] = replay;
    doc[ "would_execute" ] = wouldExecute;
    return doc;
}

EvidenceSummary ScientificAgentSession::buildSummary() const
{
    EvidenceSummary summary;
    summary.sessionId = mJournal.sessionId();
    summary.goal = mGoal;
    summary.mode = runModeToString( mPolicy.mode );
    summary.outcome = mMachine.terminalState();
    summary.stopReason = mStopReason;
    summary.stages = mVisitedStages;
    summary.decisions = mDecisions;
    summary.verificationVerdict = mVerified ? mVerification.verdict() : std::string();
    summary.artifacts = mOutcome.artifacts;
    summary.journalEntries = mJournal.size();

    Json::Value policyDoc( Json::objectValue );
    policyDoc[ "mode" ] = runModeToString( mPolicy.mode );
    policyDoc[ "max_replans" ] = mPolicy.maxReplans;
    policyDoc[ "no_progress_threshold" ] = mPolicy.noProgressThreshold;
    policyDoc[ "resource_budget_mb" ] = static_cast< Json::Int64 >( mPolicy.resourceBudgetMb );
    policyDoc[ "max_steps" ] = mPolicy.maxSteps;
    policyDoc[ "executor_timeout_ms" ] = static_cast< Json::Int64 >( mPolicy.executorTimeoutMs );
    Json::Value repairDoc( Json::objectValue );
    Json::Value autoClasses( Json::arrayValue );
    for ( const std::string &riskClass : mPolicy.repairApproval.autoApproveRiskClasses )
        autoClasses.append( riskClass );
    repairDoc[ "auto_approve_risk_classes" ] = autoClasses;
    repairDoc[ "allow_unapproved_fixable" ] = mPolicy.repairApproval.allowUnapprovedFixable;
    policyDoc[ "repair_approval" ] = repairDoc;
    Json::Value teachingDoc( Json::objectValue );
    teachingDoc[ "intent_domain" ] = mPolicy.teaching.intentDomain;
    teachingDoc[ "role" ] = mPolicy.teaching.role;
    policyDoc[ "teaching" ] = teachingDoc;
    policyDoc[ "policy_id" ] = mPolicy.policyId();
    policyDoc[ "version" ] = mPolicy.policyVersion();
    summary.policy = policyDoc;

    Json::Value budgets( Json::objectValue );
    budgets[ "replans_used" ] = mMachine.replanCount();
    budgets[ "replan_limit" ] = mPolicy.maxReplans;
    budgets[ "steps" ] = mStepCount;
    budgets[ "step_limit" ] = mPolicy.maxSteps;
    budgets[ "resource_budget_mb" ] = static_cast< Json::Int64 >( mPolicy.resourceBudgetMb );
    budgets[ "plan_estimate_mb" ] = static_cast< Json::Int64 >( mPlan.totalRamMb() );
    budgets[ "no_progress_threshold" ] = mPolicy.noProgressThreshold;
    summary.budgets = budgets;

    const SessionJournal::ReplayResult replay = mJournal.replay();
    Json::Value replayDoc( Json::objectValue );
    replayDoc[ "final_stage" ] = replay.finalStage;
    replayDoc[ "terminal_state" ] = replay.terminalState;
    replayDoc[ "replan_count" ] = replay.replanCount;
    summary.replay = replayDoc;

    summary.wouldExecute =
        mPolicy.mode == RunMode::DryRun && mPlan.valid &&
        mMachine.terminalState() == terminal_states::kDelivered;
    return summary;
}

} // namespace sicnu::agent_loop
