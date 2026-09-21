// src/agent_loop/fake_seams.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: deterministic offline doubles.
//
// One declarative FakeScenario scripts every seam; FakeSeams implements
// the interfaces from it with zero I/O and zero nondeterminism. These
// doubles let the whole loop run end to end offline (unit tests and the
// teaching exemplar) without an LLM and without the workflow engine.
//
// They are NOT the production adapter: production wires the same
// interfaces to the harness and the authoritative engine in
// src/agent/tools/agent_session_adapter.*.
//

#include "session_seams.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sicnu::agent_loop {

struct SlotScript {
    std::string slot;
    std::string ref;
    std::string kind;
    bool resolved = true;
};

struct PreflightScript {
    std::string verdict = "ok"; ///< "ok" | "fixable" | "blocked"
    std::vector< RepairProposal > proposals;
};

struct ExecutionScript {
    bool succeeded = true;
    std::string errorCode;
    std::vector< std::string > artifacts;
};

struct VerifyScript {
    std::string verdict = "PASS"; ///< "PASS" | "FAIL"
    std::string warningCheck;     ///< non-empty marks a warning-class finding
};

struct DiagnoseScript {
    std::string rootCause;
    std::vector< RepairProposal > proposals;
};

struct FakeScenario {
    std::string intent = "ndvi";
    std::vector< SlotScript > slots = { { "primary", "/data/scene.tif", "raster", true } };
    std::vector< PreflightScript > preflight = { {} };
    std::vector< ExecutionScript > execution = { {} };
    std::vector< VerifyScript > verification = { {} };
    std::vector< DiagnoseScript > diagnosis = { {} };
};

/// Deterministic 16-hex fingerprint (FNV-1a over a canonical string).
std::string deterministicFingerprint( const std::string &canonical );

class FakeDataStateProvider final : public IDataStateProvider {
  public:
    explicit FakeDataStateProvider( const FakeScenario &scenario ) : mScenario( scenario ) {}

    DataStateSnapshot snapshot( const std::string &goal, const Json::Value &refs ) override;

  private:
    const FakeScenario &mScenario;
};

class FakePlanner final : public IPlanner {
  public:
    explicit FakePlanner( const FakeScenario &scenario ) : mScenario( scenario ) {}

    PlanDraft plan( const PlanRequest &request ) override;

  private:
    const FakeScenario &mScenario;
};

class FakePreflight final : public IPreflight {
  public:
    explicit FakePreflight( const FakeScenario &scenario ) : mScenario( scenario ) {}

    PreflightReport check( const PlanDraft &plan, const DataStateSnapshot &snapshot ) override;

  private:
    const FakeScenario &mScenario;
};

class FakeExecutor final : public IExecutor {
  public:
    explicit FakeExecutor( const FakeScenario &scenario ) : mScenario( scenario ) {}

    ExecutionStart begin( const PlanDraft &plan ) override;
    ExecutionOutcome poll( const ExecutionStart &start, long long timeoutMs ) override;
    void cancel( const ExecutionStart &start ) override;

  private:
    const FakeScenario &mScenario;
    std::map< std::string, int > mAttemptByRun;
    std::set< std::string > mCancelled;
};

class FakeVerifier final : public IVerifier {
  public:
    explicit FakeVerifier( const FakeScenario &scenario ) : mScenario( scenario ) {}

    VerificationReport verify( const PlanDraft &plan,
                               const ExecutionOutcome &outcome ) override;

  private:
    const FakeScenario &mScenario;
};

class FakeDiagnoser final : public IDiagnoser {
  public:
    explicit FakeDiagnoser( const FakeScenario &scenario ) : mScenario( scenario ) {}

    Diagnosis diagnose( const PlanDraft &plan, const ExecutionOutcome &outcome,
                        const VerificationReport &verification ) override;

  private:
    const FakeScenario &mScenario;
};

/// One configured bundle of the doubles.
class FakeSeams {
  public:
    explicit FakeSeams( const FakeScenario &scenario )
        : mData( scenario ), mPlanner( scenario ), mPreflight( scenario ),
          mExecutor( scenario ), mVerifier( scenario ), mDiagnoser( scenario )
    {
    }

    IDataStateProvider &dataProvider() { return mData; }
    IPlanner &planner() { return mPlanner; }
    IPreflight &preflight() { return mPreflight; }
    IExecutor &executor() { return mExecutor; }
    IVerifier &verifier() { return mVerifier; }
    IDiagnoser &diagnoser() { return mDiagnoser; }

  private:
    FakeDataStateProvider mData;
    FakePlanner mPlanner;
    FakePreflight mPreflight;
    FakeExecutor mExecutor;
    FakeVerifier mVerifier;
    FakeDiagnoser mDiagnoser;
};

// ---------------------------------------------------------------------------
// Inline implementations (header-only by design: the doubles are test and
// exemplar infrastructure, not product runtime code).
// ---------------------------------------------------------------------------

inline std::string deterministicFingerprint( const std::string &canonical )
{
    std::uint64_t hash = 1469598103934665603ULL; // FNV-1a offset basis
    for ( const unsigned char c : canonical )
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buffer[ 17 ];
    std::snprintf( buffer, sizeof( buffer ), "%016llx", static_cast< unsigned long long >( hash ) );
    return std::string( buffer, 16 );
}

inline DataStateSnapshot FakeDataStateProvider::snapshot( const std::string &goal,
                                                          const Json::Value &refs )
{
    ( void )goal;
    ( void )refs;
    DataStateSnapshot snap;
    int resolvedCount = 0;
    for ( const SlotScript &slot : mScenario.slots )
    {
        AssetFact fact;
        fact.slot = slot.slot;
        fact.ref = slot.ref;
        fact.kind = slot.kind;
        fact.resolved = slot.resolved;
        if ( !slot.resolved )
            fact.resolutionError = "DATASET_NOT_FOUND";
        else
            fact.facts[ "kind" ] = slot.kind;
        snap.assets.push_back( fact );
        if ( slot.resolved )
            ++resolvedCount;
    }
    snap.summary[ "resolved" ] = resolvedCount;
    snap.summary[ "total" ] = static_cast< Json::Int64 >( mScenario.slots.size() );
    return snap;
}

inline PlanDraft FakePlanner::plan( const PlanRequest &request )
{
    PlanDraft draft;
    draft.attempt = request.attempt;
    draft.intent = request.intent.empty() ? mScenario.intent : request.intent;

    // Closed intent table: the offline planner knows exactly three
    // scientific intents; anything else is an invalid draft with a typed
    // error (the session refuses it — no silent fallback).
    std::string operatorId;
    if ( draft.intent == "ndvi" )
        operatorId = "rs:ndvi";
    else if ( draft.intent == "change" )
        operatorId = "rs:change_detect";
    else if ( draft.intent == "classify" )
        operatorId = "rs:classify";

    if ( operatorId.empty() )
    {
        draft.valid = false;
        draft.error = "NOT_SUPPORTED";
        draft.missingFacts.append( "intent_recipe:" + draft.intent );
        return draft;
    }

    Json::Value step( Json::objectValue );
    step[ "id" ] = "step-1";
    step[ "operator_id" ] = operatorId;
    step[ "params" ] = Json::Value( Json::objectValue );
    step[ "verification" ] = "raster";
    draft.steps.append( step );

    Json::Value output( Json::objectValue );
    output[ "name" ] = "primary_output";
    output[ "from_step" ] = "step-1";
    output[ "port" ] = "output";
    output[ "kind" ] = "raster";
    draft.outputs.append( output );

    draft.estimates.push_back( PlanEstimate{ "step-1", 256 } );

    // Plan identity moves with the attempt and the approved repairs, so a
    // genuine replan is distinguishable from a repeated failure.
    std::string canonical = draft.intent + "|" + std::to_string( request.attempt );
    for ( const std::string &repair : request.approvedRepairs )
        canonical += "|repair:" + repair;
    draft.fingerprint = deterministicFingerprint( canonical );
    draft.planId = "plan-" + draft.fingerprint;
    draft.valid = true;
    return draft;
}

inline PreflightReport FakePreflight::check( const PlanDraft &plan,
                                             const DataStateSnapshot &snapshot )
{
    ( void )snapshot;
    const std::size_t index =
        plan.attempt >= 1 && static_cast< std::size_t >( plan.attempt ) <= mScenario.preflight.size()
            ? static_cast< std::size_t >( plan.attempt ) - 1
            : mScenario.preflight.size() - 1;
    const PreflightScript &script = mScenario.preflight[ index ];

    PreflightReport report;
    report.verdict = script.verdict;
    report.proposals = script.proposals;
    for ( const RepairProposal &proposal : script.proposals )
    {
        Json::Value check( Json::objectValue );
        check[ "check" ] = "repair_proposal";
        check[ "rule_id" ] = proposal.ruleId;
        check[ "risk_class" ] = proposal.riskClass;
        check[ "passed" ] = true;
        report.checks.append( check );
    }
    return report;
}

inline ExecutionStart FakeExecutor::begin( const PlanDraft &plan )
{
    ExecutionStart start;
    if ( !plan.valid )
    {
        start.error = "INVALID_PLAN";
        return start;
    }
    start.runId = "run-" + plan.planId;
    start.started = true;
    mAttemptByRun[ start.runId ] = plan.attempt;
    return start;
}

inline ExecutionOutcome FakeExecutor::poll( const ExecutionStart &start, long long timeoutMs )
{
    ( void )timeoutMs;
    ExecutionOutcome outcome;
    outcome.runId = start.runId;
    if ( !start.started )
    {
        outcome.finished = true;
        outcome.succeeded = false;
        outcome.errorCode = start.error.empty() ? "EXECUTION_FAILED" : start.error;
        return outcome;
    }
    const auto attemptIt = mAttemptByRun.find( start.runId );
    if ( attemptIt == mAttemptByRun.end() )
        return outcome; // unknown run: not finished (timeout semantics)

    if ( mCancelled.count( start.runId ) )
    {
        outcome.finished = true;
        outcome.succeeded = false;
        outcome.state = "cancelled";
        outcome.errorCode = "CANCELLED";
        return outcome;
    }

    const std::size_t index =
        attemptIt->second >= 1 && static_cast< std::size_t >( attemptIt->second ) <=
                                      mScenario.execution.size()
            ? static_cast< std::size_t >( attemptIt->second ) - 1
            : mScenario.execution.size() - 1;
    const ExecutionScript &script = mScenario.execution[ index ];

    outcome.finished = true;
    outcome.succeeded = script.succeeded;
    outcome.state = script.succeeded ? "succeeded" : "failed";
    outcome.artifacts = script.artifacts;
    outcome.errorCode = script.errorCode;
    if ( !script.succeeded && outcome.errorCode.empty() )
        outcome.errorCode = "EXECUTION_FAILED";
    return outcome;
}

inline void FakeExecutor::cancel( const ExecutionStart &start )
{
    mCancelled.insert( start.runId );
}

inline VerificationReport FakeVerifier::verify( const PlanDraft &plan,
                                                const ExecutionOutcome &outcome )
{
    const std::size_t index =
        plan.attempt >= 1 && static_cast< std::size_t >( plan.attempt ) <=
                                  mScenario.verification.size()
            ? static_cast< std::size_t >( plan.attempt ) - 1
            : mScenario.verification.size() - 1;
    const VerifyScript &script = mScenario.verification[ index ];

    VerificationReport report;
    std::vector< std::string > paths = outcome.artifacts;
    if ( paths.empty() )
        paths.push_back( std::string() );
    for ( const std::string &path : paths )
    {
        ArtifactVerificationReport artifact;
        artifact.path = path;
        if ( script.verdict == "FAIL" )
        {
            artifact.verdict = "FAIL";
            if ( !script.warningCheck.empty() )
                artifact.warnings.push_back( script.warningCheck );
        }
        else if ( !script.warningCheck.empty() )
        {
            artifact.verdict = "PASS_WITH_WARNINGS";
            artifact.warnings.push_back( script.warningCheck );
        }
        else
        {
            artifact.verdict = "PASS";
        }
        report.artifacts.push_back( artifact );
    }
    report.verdictValue = VerificationReport::aggregate( report.artifacts );
    return report;
}

inline Diagnosis FakeDiagnoser::diagnose( const PlanDraft &plan,
                                          const ExecutionOutcome &outcome,
                                          const VerificationReport &verification )
{
    ( void )outcome;
    ( void )verification;
    const std::size_t index =
        plan.attempt >= 1 && static_cast< std::size_t >( plan.attempt ) <=
                                  mScenario.diagnosis.size()
            ? static_cast< std::size_t >( plan.attempt ) - 1
            : mScenario.diagnosis.size() - 1;
    const DiagnoseScript &script = mScenario.diagnosis[ index ];

    Diagnosis diagnosis;
    diagnosis.rootCauseCode = script.rootCause;
    diagnosis.proposals = script.proposals;
    diagnosis.summary = "scripted root cause " + script.rootCause;
    return diagnosis;
}

} // namespace sicnu::agent_loop
