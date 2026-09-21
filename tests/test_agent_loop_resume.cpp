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
#include <json/writer.h>

#include "agent_loop/scientific_agent_session.h"
#include "agent_loop/fake_seams.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace sicnu::agent_loop;

namespace {

std::string jsonToString( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder[ "commentStyle" ] = "None";
    builder[ "indentation" ] = "";
    return Json::writeString( builder, doc );
}

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
    // reuse). The cancel landed at the data_state_snapshot boundary, so
    // exactly one decision (goal normalization) preceded it and the resumed
    // run continues at decision-2.
    std::vector< std::string > decisionIds;
    for ( const DecisionRecord &decision : completed.summary.decisions )
        decisionIds.push_back( decision.decisionId );
    REQUIRE( decisionIds.size() >= 4 );
    REQUIRE( decisionIds[ 0 ] == "decision-2" );
    REQUIRE( decisionIds[ 1 ] == "decision-3" );

    // The data-state snapshot is re-taken on resume (a read-only fact step;
    // the staleness philosophy of the harness session store applies: facts
    // predating the current bytes are re-gathered, never trusted blindly).
    REQUIRE( stageEntryCount( completed.journal, stages::kDataStateSnapshot ) == 2 );

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
