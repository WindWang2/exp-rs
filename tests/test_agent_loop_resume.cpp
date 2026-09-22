// tests/test_agent_loop_resume.cpp
//
// RS14-11 Evidence-first Agent Loop — Slice G1: restart/replay where safe.
//
// A cancelled session persists its journal; a later process (or the same
// one) resumes from it — the machine restarts at the journal's final
// stage, the journal is adopted with its sequence and decision numbering,
// and the loop continues WITHOUT re-executing anything already done.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent_loop/scientific_agent_session.h"
#include "agent_loop/fake_seams.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace sicnu::agent_loop;

namespace {

ScientificAgentSession::Dependencies makeDependencies( FakeSeams &seams )
{
    return { &seams.dataProvider(), &seams.planner(), &seams.preflight(), &seams.executor(),
             &seams.verifier(), &seams.diagnoser() };
}

SessionRunRequest makeRequest()
{
    SessionRunRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    return request;
}

/// Delegating planner/executor wrappers with a post-call hook. Tests cancel
/// from inside a stage handler this way — the loop then parks AT that stage
/// boundary, which is exactly the journal shape a crash mid-stage produces.
class HookPlanner final : public IPlanner
{
  public:
    HookPlanner( IPlanner &next, std::function< void() > hook )
        : mNext( next ), mHook( std::move( hook ) )
    {
    }
    PlanDraft plan( const PlanRequest &request ) override
    {
        const PlanDraft draft = mNext.plan( request );
        if ( mHook )
            mHook();
        return draft;
    }

  private:
    IPlanner &mNext;
    std::function< void() > mHook;
};

class HookExecutor final : public IExecutor
{
  public:
    HookExecutor( IExecutor &next, std::function< void() > hook )
        : mNext( next ), mHook( std::move( hook ) )
    {
    }
    ExecutionStart begin( const PlanDraft &plan ) override
    {
        const ExecutionStart start = mNext.begin( plan );
        if ( mHook )
            mHook();
        return start;
    }
    ExecutionOutcome poll( const ExecutionStart &start, long long timeoutMs ) override
    {
        return mNext.poll( start, timeoutMs );
    }
    void cancel( const ExecutionStart &start ) override { mNext.cancel( start ); }

  private:
    IExecutor &mNext;
    std::function< void() > mHook;
};

/// Counts stage_enter entries for `stage` in a journal.
int stageEntryCount( const SessionJournal &journal, const std::string &stage )
{
    int count = 0;
    for ( const JournalEntry &entry : journal.entries() )
        if ( entry.event == "stage_enter" && entry.stage == stage )
            ++count;
    return count;
}

std::string uniqueTempDir( const char *tag )
{
    const fs::path base = fs::temp_directory_path() /
        ( std::string( "sicnu-agent-loop-" ) + tag + "-resume" );
    std::error_code ec;
    fs::remove_all( base, ec );
    fs::create_directories( base, ec );
    return base.string();
}

} // namespace

TEST_CASE( "a cancelled session resumes from its journal and completes", "[agent_loop][resume]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "PASS", "" } };
    SessionPolicy policy = SessionPolicy::defaults();

    // First run: cancel on the 3rd clock tick (mid-plan journalling), so
    // the session aborts at a stage boundary before executing.
    FakeSeams cancelSeams( scenario );
    long long tick = 0;
    ScientificAgentSession cancelling(
        policy, makeDependencies( cancelSeams ),
        [ &cancelling, &tick ]() mutable -> long long {
            const long long at = tick++;
            if ( at == 3 )
                cancelling.requestCancel();
            return at;
        },
        "sess-resume" );
    const SessionResult cancelled = cancelling.run( makeRequest() );

    REQUIRE_FALSE( cancelled.ok );
    REQUIRE( cancelled.terminalState == terminal_states::kAborted );
    REQUIRE( cancelled.stopReason == stop_reasons::kCancelled );

    // Persist the journal (the restart-safe artifact).
    const std::string dir = uniqueTempDir( "cancel" );
    std::string error;
    REQUIRE( cancelled.journal.save( dir, &error ) );
    const auto loaded = SessionJournal::load( dir, "sess-resume", &error );
    REQUIRE( loaded.has_value() );

    // Resume: the machine restarts at the journal's final stage.
    FakeSeams resumeSeams( scenario );
    auto resumed = ScientificAgentSession::resume( *loaded, policy,
                                                         makeDependencies( resumeSeams ) );
    REQUIRE( resumed.has_value() );

    SessionRunRequest request = makeRequest();
    const SessionResult completed = resumed->run( request );

    REQUIRE( completed.ok );
    REQUIRE( completed.terminalState == terminal_states::kDelivered );
    REQUIRE( completed.summary.artifacts.size() == 1 );

    // The pre-cancel journal prefix is preserved verbatim ...
    REQUIRE( completed.journal.entries().size() > cancelled.journal.entries().size() );
    for ( std::size_t i = 0; i < cancelled.journal.entries().size(); ++i )
    {
        const JournalEntry &before = cancelled.journal.entries()[ i ];
        const JournalEntry &after = completed.journal.entries()[ i ];
        REQUIRE( after.seq == before.seq );
        REQUIRE( after.event == before.event );
        REQUIRE( after.stage == before.stage );
    }
    // ... and the sequence is monotonic across the restart boundary.
    long long previousSeq = 0;
    for ( const JournalEntry &entry : completed.journal.entries() )
    {
        REQUIRE( entry.seq > previousSeq );
        previousSeq = entry.seq;
    }

    // Decision numbering continued across the restart (no decision-N
    // reuse), and the evidence summary carries the WHOLE history: the
    // pre-cancel decisions are rebuilt from the journal, so the summary is
    // the session's decisions, not only the post-restart tail.
    std::vector< std::string > decisionIds;
    for ( const DecisionRecord &decision : completed.summary.decisions )
        decisionIds.push_back( decision.decisionId );
    REQUIRE( decisionIds.size() >= 5 );
    REQUIRE( decisionIds[ 0 ] == "decision-1" );
    REQUIRE( decisionIds[ 1 ] == "decision-2" );

    // The logical clock also continues: journal timestamps never go
    // backwards across the restart boundary.
    long long previousAt = -1;
    for ( const JournalEntry &entry : completed.journal.entries() )
    {
        REQUIRE( entry.at >= previousAt );
        previousAt = entry.at;
    }

    // The data-state snapshot is re-taken on resume (a read-only fact step;
    // the staleness philosophy of the harness session store applies: facts
    // predating the current bytes are re-gathered, never trusted blindly).
    // The PROVIDER is really re-invoked (that is the re-take; the resumed
    // seams instance takes it exactly once), while the journal narrates
    // the stage entry once — a resume must not append a duplicate
    // stage_enter for the stage the session is already parked at.
    REQUIRE( resumeSeams.dataProviderFake().snapshotCount() == 1 );
    REQUIRE( stageEntryCount( completed.journal, stages::kDataStateSnapshot ) == 1 );

    // The resumed run reached the same terminal state a direct run would.
    FakeSeams directSeams( scenario );
    ScientificAgentSession direct( policy, makeDependencies( directSeams ), {}, "sess-direct" );
    const SessionResult directResult = direct.run( makeRequest() );
    REQUIRE( directResult.ok );
    REQUIRE( completed.summary.replay[ "terminal_state" ].asString() ==
             directResult.summary.replay[ "terminal_state" ].asString() );
    REQUIRE( completed.summary.verificationVerdict ==
             directResult.summary.verificationVerdict );
}

TEST_CASE( "resume refuses a journal parked at or past the plan seam", "[agent_loop][resume]" )
{
    // Stages from plan_request onward dispatch on in-memory state the
    // journal does not carry as values (the plan draft, the snapshot, the
    // preflight report, the execution outcome). A resume there would run
    // prefight/verify/execute over default-constructed state and fabricate
    // a delivery this process never produced — so resume refuses the whole
    // class, and only the pre-plan stages (goal_normalization,
    // data_state_snapshot) stay resumable.
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "PASS", "" } };
    SessionPolicy policy = SessionPolicy::defaults();

    // Parked at kPreflight: cancel from inside the planner (the handler
    // then enters preflight, and the loop aborts at the boundary).
    {
        FakeSeams seams( scenario );
        ScientificAgentSession *running = nullptr;
        HookPlanner planner( seams.plannerFake(),
                             [ &running ]() {
                                 if ( running )
                                     running->requestCancel();
                             } );
        ScientificAgentSession::Dependencies deps = makeDependencies( seams );
        deps.planner = &planner;
        ScientificAgentSession parked( policy, deps, {}, "sess-park-pf" );
        running = &parked;
        const SessionResult cancelled = parked.run( makeRequest() );
        REQUIRE_FALSE( cancelled.ok );
        REQUIRE( cancelled.stopReason == stop_reasons::kCancelled );
        const SessionJournal::ReplayResult replay = cancelled.journal.replay();
        REQUIRE( replay.finalStage == stages::kPreflight );

        const auto resumed =
            ScientificAgentSession::resume( cancelled.journal, policy, makeDependencies( seams ) );
        REQUIRE_FALSE( resumed.has_value() );
    }

    // Parked at kExecute: cancel from inside the executor's begin().
    {
        FakeSeams seams( scenario );
        ScientificAgentSession *running = nullptr;
        HookExecutor executor( seams.executorFake(), [ &running ]() {
            if ( running )
                running->requestCancel();
        } );
        ScientificAgentSession::Dependencies deps = makeDependencies( seams );
        deps.executor = &executor;
        ScientificAgentSession parked( policy, deps, {}, "sess-park-ex" );
        running = &parked;
        const SessionResult cancelled = parked.run( makeRequest() );
        REQUIRE_FALSE( cancelled.ok );
        REQUIRE( cancelled.stopReason == stop_reasons::kCancelled );
        const SessionJournal::ReplayResult replay = cancelled.journal.replay();
        REQUIRE( replay.finalStage == stages::kExecute );

        const auto resumed =
            ScientificAgentSession::resume( cancelled.journal, policy, makeDependencies( seams ) );
        REQUIRE_FALSE( resumed.has_value() );
    }
}

TEST_CASE( "resume at goal_normalization continues and completes", "[agent_loop][resume]" )
{
    // Parked at the first stage: cancel before the first handler runs.
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "PASS", "" } };
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-park-goal" );
    session.requestCancel();
    const SessionResult cancelled = session.run( makeRequest() );
    REQUIRE_FALSE( cancelled.ok );
    REQUIRE( cancelled.stopReason == stop_reasons::kCancelled );
    REQUIRE( cancelled.journal.replay().finalStage == stages::kGoalNormalization );

    auto resumed = ScientificAgentSession::resume( cancelled.journal, policy,
                                                   makeDependencies( seams ) );
    REQUIRE( resumed.has_value() );
    const SessionResult completed = resumed->run( makeRequest() );
    REQUIRE( completed.ok );
    REQUIRE( completed.terminalState == terminal_states::kDelivered );
}

TEST_CASE( "a resumed session must restate the same goal", "[agent_loop][resume]" )
{
    // The journal is the session's evidence trail: one session narrates one
    // mission. A resume that swaps the goal would splice two missions into
    // one evidence summary under shared budget counters — it is refused
    // with a typed stop reason instead.
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "PASS", "" } };
    SessionPolicy policy = SessionPolicy::defaults();

    FakeSeams cancelSeams( scenario );
    long long tick = 0;
    ScientificAgentSession cancelling(
        policy, makeDependencies( cancelSeams ),
        [ &cancelling, &tick ]() mutable -> long long {
            const long long at = tick++;
            if ( at == 3 )
                cancelling.requestCancel();
            return at;
        },
        "sess-goal" );
    const SessionResult cancelled = cancelling.run( makeRequest() );
    REQUIRE_FALSE( cancelled.ok );

    FakeSeams resumeSeams( scenario );
    auto resumed =
        ScientificAgentSession::resume( cancelled.journal, policy, makeDependencies( resumeSeams ) );
    REQUIRE( resumed.has_value() );

    SessionRunRequest swapped;
    swapped.goal = "a different mission entirely";
    swapped.intent = "classify";
    const SessionResult refused = resumed->run( swapped );
    REQUIRE_FALSE( refused.ok );
    REQUIRE( refused.stopReason == stop_reasons::kGoalMismatch );

    // The refused resume is absorbing: the journal now ends refused, and
    // resume() no longer offers the session.
    REQUIRE( refused.journal.replay().terminalState == terminal_states::kRefused );
    const auto again = ScientificAgentSession::resume( refused.journal, policy,
                                                       makeDependencies( resumeSeams ) );
    REQUIRE_FALSE( again.has_value() );
}

TEST_CASE( "resume refuses a terminal journal", "[agent_loop][resume]" )
{
    FakeScenario scenario;
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-terminal" );
    const SessionResult result = session.run( makeRequest() );
    REQUIRE( result.ok );

    const auto resumed = ScientificAgentSession::resume( result.journal, policy,
                                                         makeDependencies( seams ) );
    REQUIRE_FALSE( resumed.has_value() );
}

TEST_CASE( "resume refuses a journal with no resumable stage", "[agent_loop][resume]" )
{
    const SessionJournal empty( "sess-empty" );
    FakeScenario scenario;
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    const auto resumed = ScientificAgentSession::resume( empty, policy,
                                                         makeDependencies( seams ) );
    REQUIRE_FALSE( resumed.has_value() );

    // A journal whose only content is a decision (no stage entry) has no
    // resumable stage either.
    SessionJournal decisionOnly( "sess-decision-only" );
    DecisionRecord d;
    d.decisionId = "decision-1";
    d.sessionId = "sess-decision-only";
    d.stage = stages::kPreflight;
    d.selected[ "action" ] = "note";
    d.reason = "orphan";
    decisionOnly.append( "decision", stages::kPreflight, Json::Value( Json::objectValue ), 1, d );
    const auto orphan = ScientificAgentSession::resume( decisionOnly, policy,
                                                        makeDependencies( seams ) );
    REQUIRE_FALSE( orphan.has_value() );
}
