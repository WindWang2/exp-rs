// tests/test_agent_loop_core.cpp
//
// RS14-11 Evidence-first Agent Loop — Slice A: core values.
//
// Session state machine (closed stage vocabulary + typed transitions),
// DecisionRecord (versioned, fail-closed), and the replayable session
// journal (append/bound/persist/replay). Pure C++ — no Qt, no GDAL.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

#include "agent_loop/session_state.h"
#include "agent_loop/decision_record.h"
#include "agent_loop/session_journal.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace sicnu::agent_loop;

namespace {

std::string jsonToString( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "";
    return Json::writeString( builder, doc );
}

std::string uniqueTempDir( const char *tag )
{
    const fs::path base = fs::temp_directory_path() /
        ( std::string( "sicnu-agent-loop-" ) + tag + "-" +
          std::to_string( std::hash<std::string>{}( tag ) ) );
    std::error_code ec;
    fs::remove_all( base, ec );
    fs::create_directories( base, ec );
    return base.string();
}

DecisionRecord makeDecision( const std::string &stage, const std::string &reason )
{
    DecisionRecord d;
    d.decisionId = "decision-1"; // assigned by the session driver in production
    d.sessionId = "sess-1";
    d.stage = stage;
    d.recordedAt = 1000;
    d.inputs["intent"] = "ndvi";
    d.alternatives.push_back( DecisionAlternative{ "alt-1", "use EVI instead",
                                                   "goal pins NDVI explicitly" } );
    d.selected["action"] = "plan_with_intent";
    d.selected["intent"] = "ndvi";
    d.reason = reason;
    d.evidence.push_back( DecisionEvidence{ "fact", "band_roles:nir,red" } );
    d.policy["policy_id"] = "session_policy";
    d.policy["version"] = "1.0";
    return d;
}

} // namespace

TEST_CASE( "stage vocabulary is closed", "[agent_loop][core]" )
{
    for ( const char *stage : { stages::kGoalNormalization, stages::kDataStateSnapshot,
                                stages::kPlanRequest, stages::kPreflight,
                                stages::kRepairApproval, stages::kExecute,
                                stages::kVerify, stages::kDiagnose, stages::kReplan,
                                stages::kDelivery } )
        REQUIRE( isKnownStage( stage ) );

    for ( const char *t : { terminal_states::kDelivered, terminal_states::kRefused,
                            terminal_states::kAborted } )
        REQUIRE( isKnownTerminalState( t ) );

    REQUIRE_FALSE( isKnownStage( "executing" ) ); // harness vocabulary, not ours
    REQUIRE_FALSE( isKnownStage( "" ) );
    REQUIRE_FALSE( isKnownStage( "Goal_Normalization" ) );
    REQUIRE_FALSE( isKnownTerminalState( "done" ) );
}

TEST_CASE( "transition table admits exactly the loop edges", "[agent_loop][core]" )
{
    // Happy chain.
    REQUIRE( isLegalTransition( stages::kGoalNormalization, stages::kDataStateSnapshot ) );
    REQUIRE( isLegalTransition( stages::kDataStateSnapshot, stages::kPlanRequest ) );
    REQUIRE( isLegalTransition( stages::kPlanRequest, stages::kPreflight ) );
    REQUIRE( isLegalTransition( stages::kPreflight, stages::kRepairApproval ) );
    REQUIRE( isLegalTransition( stages::kPreflight, stages::kExecute ) );
    REQUIRE( isLegalTransition( stages::kRepairApproval, stages::kExecute ) );
    REQUIRE( isLegalTransition( stages::kRepairApproval, stages::kPlanRequest ) );
    REQUIRE( isLegalTransition( stages::kExecute, stages::kVerify ) );
    REQUIRE( isLegalTransition( stages::kVerify, stages::kDelivery ) );
    REQUIRE( isLegalTransition( stages::kVerify, stages::kDiagnose ) );
    REQUIRE( isLegalTransition( stages::kDiagnose, stages::kReplan ) );
    REQUIRE( isLegalTransition( stages::kReplan, stages::kPlanRequest ) );
    REQUIRE( isLegalTransition( stages::kDiagnose, terminal_states::kRefused ) );
    REQUIRE( isLegalTransition( stages::kReplan, terminal_states::kAborted ) );
    REQUIRE( isLegalTransition( stages::kExecute, stages::kDiagnose ) );
    REQUIRE( isLegalTransition( stages::kDelivery, terminal_states::kDelivered ) );

    // Refusals/aborts reachable from every running stage.
    for ( const char *stage : { stages::kGoalNormalization, stages::kDataStateSnapshot,
                                stages::kPlanRequest, stages::kPreflight,
                                stages::kRepairApproval, stages::kExecute,
                                stages::kVerify, stages::kDiagnose, stages::kReplan,
                                stages::kDelivery } )
    {
        REQUIRE( isLegalTransition( stage, terminal_states::kRefused ) );
        REQUIRE( isLegalTransition( stage, terminal_states::kAborted ) );
    }

    // Skips and back-edges that must NOT exist.
    REQUIRE_FALSE( isLegalTransition( stages::kGoalNormalization, stages::kExecute ) );
    REQUIRE_FALSE( isLegalTransition( stages::kPlanRequest, stages::kExecute ) );
    REQUIRE_FALSE( isLegalTransition( stages::kExecute, stages::kPlanRequest ) );
    REQUIRE_FALSE( isLegalTransition( stages::kVerify, stages::kExecute ) );
    REQUIRE_FALSE( isLegalTransition( stages::kPreflight, terminal_states::kDelivered ) );
    REQUIRE_FALSE( isLegalTransition( stages::kVerify, terminal_states::kDelivered ) );
    REQUIRE_FALSE( isLegalTransition( stages::kDelivery, stages::kVerify ) );
    REQUIRE_FALSE( isLegalTransition( terminal_states::kDelivered, stages::kVerify ) );
    REQUIRE_FALSE( isLegalTransition( terminal_states::kAborted, terminal_states::kRefused ) );
    REQUIRE_FALSE( isLegalTransition( "not_a_stage", stages::kPreflight ) );
    REQUIRE_FALSE( isLegalTransition( stages::kPreflight, "not_a_stage" ) );
}

TEST_CASE( "stage machine enforces transitions and counts replans", "[agent_loop][core]" )
{
    SessionStageMachine machine;
    REQUIRE( machine.stage() == stages::kGoalNormalization );
    REQUIRE_FALSE( machine.terminal() );
    REQUIRE( machine.terminalState().empty() );
    REQUIRE( machine.replanCount() == 0 );

    // Illegal first move: typed error, no mutation.
    const TransitionResult bad = machine.advance( stages::kExecute );
    REQUIRE_FALSE( bad.ok );
    REQUIRE( bad.error.code == error_codes::kIllegalTransition );
    REQUIRE_FALSE( bad.error.message.empty() );
    REQUIRE( machine.stage() == stages::kGoalNormalization );

    REQUIRE( machine.advance( stages::kDataStateSnapshot ).ok );
    REQUIRE( machine.advance( stages::kPlanRequest ).ok );
    REQUIRE( machine.advance( stages::kPreflight ).ok );
    REQUIRE( machine.advance( stages::kExecute ).ok );
    REQUIRE( machine.advance( stages::kVerify ).ok );
    REQUIRE( machine.advance( stages::kDiagnose ).ok );
    REQUIRE( machine.advance( stages::kReplan ).ok );
    REQUIRE( machine.replanCount() == 1 );
    REQUIRE( machine.advance( stages::kPlanRequest ).ok );
    REQUIRE( machine.advance( stages::kPreflight ).ok );
    REQUIRE( machine.advance( stages::kExecute ).ok );
    REQUIRE( machine.advance( stages::kVerify ).ok );
    REQUIRE( machine.advance( stages::kDelivery ).ok );
    REQUIRE( machine.advance( terminal_states::kDelivered ).ok );
    REQUIRE( machine.terminal() );
    REQUIRE( machine.terminalState() == terminal_states::kDelivered );

    // Terminal is absorbing: any further move is a typed error.
    const TransitionResult late = machine.advance( stages::kVerify );
    REQUIRE_FALSE( late.ok );
    REQUIRE( late.error.code == error_codes::kAlreadyTerminal );
    const TransitionResult lateTerm = machine.terminate( terminal_states::kRefused, "x" );
    REQUIRE_FALSE( lateTerm.ok );
    REQUIRE( lateTerm.error.code == error_codes::kAlreadyTerminal );
    REQUIRE( machine.terminalState() == terminal_states::kDelivered );
}

TEST_CASE( "terminate records a stop reason and is legal from any running stage", "[agent_loop][core]" )
{
    SessionStageMachine machine;
    machine.advance( stages::kDataStateSnapshot );
    const TransitionResult r = machine.terminate( terminal_states::kAborted,
                                                  stop_reasons::kNoProgress );
    REQUIRE( r.ok );
    REQUIRE( r.to == terminal_states::kAborted );
    REQUIRE( r.stopReason == stop_reasons::kNoProgress );
    REQUIRE( machine.terminal() );

    SessionStageMachine m2;
    const TransitionResult bad = m2.terminate( "finished", "nope" );
    REQUIRE_FALSE( bad.ok );
    REQUIRE( bad.error.code == error_codes::kUnknownTerminalState );
    REQUIRE_FALSE( m2.terminal() );
}

TEST_CASE( "decision record round-trips through JSON", "[agent_loop][core]" )
{
    DecisionRecord d = makeDecision( stages::kPlanRequest, "intent resolved from band roles" );
    d.decisionId = "decision-1";
    const Json::Value doc = d.toJson();

    REQUIRE( doc["schema_version"].asString() == "1.0" );
    REQUIRE( doc["decision_id"].asString() == "decision-1" );
    REQUIRE( doc["stage"].asString() == stages::kPlanRequest );
    REQUIRE( doc["reason"].asString() == "intent resolved from band roles" );
    REQUIRE( doc["alternatives"].isArray() );
    REQUIRE( doc["alternatives"].size() == 1 );
    REQUIRE( doc["alternatives"][0]["why_not"].asString() == "goal pins NDVI explicitly" );
    REQUIRE( doc["evidence"].isArray() );
    REQUIRE( doc["evidence"][0]["kind"].asString() == "fact" );
    REQUIRE( doc["policy"]["policy_id"].asString() == "session_policy" );

    std::string error;
    const auto back = DecisionRecord::fromJson( doc, &error );
    REQUIRE( back.has_value() );
    REQUIRE( back->decisionId == d.decisionId );
    REQUIRE( back->stage == d.stage );
    REQUIRE( back->reason == d.reason );
    REQUIRE( back->alternatives.size() == 1 );
    REQUIRE( back->alternatives[0].id == "alt-1" );
    REQUIRE( back->evidence.size() == 1 );
    REQUIRE( back->evidence[0].ref == "band_roles:nir,red" );
    REQUIRE( back->selected["action"].asString() == "plan_with_intent" );
    REQUIRE( back->toJson() == doc );
}

TEST_CASE( "decision record reader is fail-closed", "[agent_loop][core]" )
{
    DecisionRecord good = makeDecision( stages::kPreflight, "ok" );
    good.decisionId = "decision-9";

    std::string error;
    REQUIRE( DecisionRecord::fromJson( good.toJson(), &error ).has_value() );

    Json::Value badVersion = good.toJson();
    badVersion["schema_version"] = "2.0";
    REQUIRE_FALSE( DecisionRecord::fromJson( badVersion, &error ).has_value() );
    REQUIRE( error.find( "schema_version" ) != std::string::npos );

    Json::Value noReason = good.toJson();
    noReason.removeMember( "reason" );
    REQUIRE_FALSE( DecisionRecord::fromJson( noReason, &error ).has_value() );

    Json::Value emptyReason = good.toJson();
    emptyReason["reason"] = "";
    REQUIRE_FALSE( DecisionRecord::fromJson( emptyReason, &error ).has_value() );

    Json::Value badStage = good.toJson();
    badStage["stage"] = "executing"; // harness stage, not this vocabulary
    REQUIRE_FALSE( DecisionRecord::fromJson( badStage, &error ).has_value() );
    REQUIRE( error.find( "stage" ) != std::string::npos );

    Json::Value noSelected = good.toJson();
    noSelected.removeMember( "selected" );
    REQUIRE_FALSE( DecisionRecord::fromJson( noSelected, &error ).has_value() );

    Json::Value notObject( Json::arrayValue );
    REQUIRE_FALSE( DecisionRecord::fromJson( notObject, &error ).has_value() );
}

TEST_CASE( "journal appends monotonically and evicts oldest beyond the bound", "[agent_loop][core]" )
{
    SessionJournal journal( "sess-bound", 4 );
    for ( int i = 0; i < 6; ++i )
        journal.append( "note", stages::kPlanRequest, Json::Value( static_cast<Json::Int>( i ) ), 100 + i );

    REQUIRE( journal.size() == 4 );
    const auto &entries = journal.entries();
    REQUIRE( entries.front().seq == 3 );
    REQUIRE( entries.back().seq == 6 );
    REQUIRE( entries.back().payload.asInt() == 5 );
    // Eviction never rewrites seq: the next append continues the sequence.
    journal.append( "note", stages::kPlanRequest, Json::Value( static_cast<Json::Int>( 6 ) ), 107 );
    REQUIRE( journal.entries().back().seq == 7 );
    REQUIRE( journal.size() == 4 );

    // Entries outside the known stage vocabulary or without an event are
    // rejected without consuming a sequence number.
    const long long before = journal.nextSeq();
    REQUIRE_FALSE( journal.append( "", stages::kPlanRequest, Json::Value( 1 ), 1 ) );
    REQUIRE_FALSE( journal.append( "note", "executing", Json::Value( 1 ), 1 ) );
    REQUIRE_FALSE( journal.append( "note", "", Json::Value( 1 ), 1 ) );
    REQUIRE( journal.nextSeq() == before );
    REQUIRE( journal.size() == 4 );
}

TEST_CASE( "journal carries decision records and round-trips them", "[agent_loop][core]" )
{
    SessionJournal journal( "sess-dec" );
    const DecisionRecord d = makeDecision( stages::kPreflight, "fixable, auto-approved" );
    journal.append( "decision", stages::kPreflight, Json::Value( Json::objectValue ), 500,
                    d );

    REQUIRE( journal.entries().size() == 1 );
    const JournalEntry &entry = journal.entries().front();
    REQUIRE( entry.event == "decision" );
    REQUIRE( entry.decision.has_value() );
    REQUIRE( entry.decision->reason == "fixable, auto-approved" );

    const Json::Value doc = journal.toJson();
    std::string error;
    const auto back = SessionJournal::fromJson( doc, &error );
    REQUIRE( back.has_value() );
    REQUIRE( back->entries().size() == 1 );
    REQUIRE( back->entries().front().decision.has_value() );
    REQUIRE( back->entries().front().decision->reason == d.reason );
    REQUIRE( back->toJson() == doc );
}

TEST_CASE( "journal reader rejects corrupt and foreign documents", "[agent_loop][core]" )
{
    std::string error;
    Json::Value notObject( Json::arrayValue );
    REQUIRE_FALSE( SessionJournal::fromJson( notObject, &error ).has_value() );

    Json::Value badVersion( Json::objectValue );
    badVersion["schema_version"] = "9.9";
    badVersion["session_id"] = "s";
    badVersion["entries"] = Json::Value( Json::arrayValue );
    REQUIRE_FALSE( SessionJournal::fromJson( badVersion, &error ).has_value() );
    REQUIRE( error.find( "schema_version" ) != std::string::npos );

    Json::Value badEntry( Json::objectValue );
    badEntry["schema_version"] = "1.0";
    badEntry["session_id"] = "s";
    Json::Value entries( Json::arrayValue );
    Json::Value e( Json::objectValue );
    e["seq"] = 1;
    e["at"] = 1;
    e["stage"] = "executing"; // foreign stage vocabulary
    e["event"] = "note";
    entries.append( e );
    badEntry["entries"] = entries;
    REQUIRE_FALSE( SessionJournal::fromJson( badEntry, &error ).has_value() );
    REQUIRE( error.find( "stage" ) != std::string::npos );
}

TEST_CASE( "journal persists atomically and reloads identically", "[agent_loop][core]" )
{
    const std::string dir = uniqueTempDir( "persist" );
    SessionJournal journal( "sess-persist" );
    journal.append( "stage_enter", stages::kGoalNormalization, Json::Value( Json::objectValue ), 1 );
    journal.append( "decision", stages::kPlanRequest, Json::Value( Json::objectValue ), 2,
                    makeDecision( stages::kPlanRequest, "offline planner picked ndvi" ) );
    journal.append( "stage_enter", stages::kPreflight, Json::Value( Json::objectValue ), 3 );

    std::string error;
    REQUIRE( journal.save( dir, &error ) );
    REQUIRE( fs::exists( fs::path( dir ) / "sess-persist.json" ) );

    const auto loaded = SessionJournal::load( dir, "sess-persist", &error );
    REQUIRE( loaded.has_value() );
    REQUIRE( loaded->entries().size() == 3 );
    REQUIRE( loaded->entries()[1].decision.has_value() );
    REQUIRE( loaded->toJson() == journal.toJson() );

    // No temp files left behind.
    for ( const auto &item : fs::directory_iterator( dir ) )
        REQUIRE( item.path().extension() == ".json" );

    // Missing and corrupt documents fail closed with a typed error.
    const auto missing = SessionJournal::load( dir, "nope", &error );
    REQUIRE_FALSE( missing.has_value() );
    REQUIRE_FALSE( error.empty() );

    {
        std::ofstream corrupt( fs::path( dir ) / "corrupt.json" );
        corrupt << "{ not json";
    }
    const auto broken = SessionJournal::load( dir, "corrupt", &error );
    REQUIRE_FALSE( broken.has_value() );
    REQUIRE_FALSE( error.empty() );
}

TEST_CASE( "journal rejects unsafe session ids", "[agent_loop][core]" )
{
    const std::string dir = uniqueTempDir( "unsafe" );
    std::string error;
    for ( const char *bad : { "", "../escape", "a/b", ".", "..", "sess id", "sess\u0001x" } )
    {
        SessionJournal journal( bad );
        journal.append( "note", stages::kPlanRequest, Json::Value( static_cast<Json::Int>( 1 ) ), 1 );
        REQUIRE_FALSE( journal.save( dir, &error ) );
        REQUIRE_FALSE( error.empty() );
    }
    // The safe subset still works.
    SessionJournal ok( "Sess_1.2-3" );
    ok.append( "note", stages::kPlanRequest, Json::Value( static_cast<Json::Int>( 1 ) ), 1 );
    REQUIRE( ok.save( dir, &error ) );
    REQUIRE( SessionJournal::load( dir, "Sess_1.2-3", &error ).has_value() );
}

TEST_CASE( "journal compacts oversized documents deterministically", "[agent_loop][core]" )
{
    SessionJournal journal( "sess-big" );
    Json::Value payload( Json::objectValue );
    payload["blob"] = std::string( 4096, 'x' );
    for ( int i = 0; i < 8; ++i )
        journal.append( "note", stages::kExecute, payload, i );

    const Json::Value compacted = journal.compactProjection();
    REQUIRE( compacted["entries"].size() == 8 );

    // The projection must be loadable (fail-closed reader accepts it).
    std::string error;
    const auto back = SessionJournal::fromJson( compacted, &error );
    REQUIRE( back.has_value() );
    REQUIRE( back->entries().size() == 8 );

    // ... strictly smaller than the raw document, payload bodies gone ...
    const std::string raw = jsonToString( journal.toJson() );
    const std::string small = jsonToString( compacted );
    REQUIRE( small.size() < raw.size() );
    REQUIRE( small.find( "xxxxxxxx" ) == std::string::npos );
    // ... while the decision records (the evidence) survive compaction.
    SessionJournal withDecision( "sess-big-dec" );
    withDecision.append( "decision", stages::kPreflight, payload, 0,
                         makeDecision( stages::kPreflight, "survives" ) );
    const Json::Value compacted2 = withDecision.compactProjection();
    REQUIRE( compacted2["entries"][0]["decision"]["reason"].asString() == "survives" );
    REQUIRE_FALSE( compacted2["entries"][0]["decision"]["reason"].asString().empty() );

    // Same input -> same projection (determinism).
    REQUIRE( jsonToString( journal.compactProjection() ) == small );
}

TEST_CASE( "replay reconstructs history without seams", "[agent_loop][core]" )
{
    SessionJournal journal( "sess-replay" );
    journal.append( "stage_enter", stages::kGoalNormalization, Json::Value( Json::objectValue ), 1 );
    journal.append( "stage_enter", stages::kDataStateSnapshot, Json::Value( Json::objectValue ), 2 );
    journal.append( "stage_enter", stages::kPlanRequest, Json::Value( Json::objectValue ), 3 );
    journal.append( "stage_enter", stages::kPreflight, Json::Value( Json::objectValue ), 4 );
    journal.append( "stage_enter", stages::kExecute, Json::Value( Json::objectValue ), 5 );
    journal.append( "stage_enter", stages::kVerify, Json::Value( Json::objectValue ), 6 );
    journal.append( "stage_enter", stages::kDiagnose, Json::Value( Json::objectValue ), 7 );
    journal.append( "stage_enter", stages::kReplan, Json::Value( Json::objectValue ), 8 );
    journal.append( "stage_enter", stages::kPlanRequest, Json::Value( Json::objectValue ), 9 );
    journal.append( "stage_enter", stages::kPreflight, Json::Value( Json::objectValue ), 10 );
    journal.append( "stage_enter", stages::kExecute, Json::Value( Json::objectValue ), 11 );
    journal.append( "stage_enter", stages::kVerify, Json::Value( Json::objectValue ), 12 );
    journal.append( "stage_enter", stages::kDelivery, Json::Value( Json::objectValue ), 13 );
    journal.append( "terminal", terminal_states::kDelivered,
                    Json::Value( Json::objectValue ), 14 );

    const SessionJournal::ReplayResult first = journal.replay();
    REQUIRE( first.finalStage == stages::kDelivery );
    REQUIRE( first.terminalState == terminal_states::kDelivered );
    REQUIRE( first.replanCount == 1 );
    REQUIRE( first.entries.size() == 14 );

    // Replay is deterministic: same journal -> same result.
    const SessionJournal::ReplayResult second = journal.replay();
    REQUIRE( second.finalStage == first.finalStage );
    REQUIRE( second.terminalState == first.terminalState );
    REQUIRE( second.replanCount == first.replanCount );

    // A journal reloaded from disk replays identically (no seam state needed).
    const std::string dir = uniqueTempDir( "replay" );
    std::string error;
    REQUIRE( journal.save( dir, &error ) );
    const auto loaded = SessionJournal::load( dir, "sess-replay", &error );
    REQUIRE( loaded.has_value() );
    const SessionJournal::ReplayResult third = loaded->replay();
    REQUIRE( third.finalStage == first.finalStage );
    REQUIRE( third.terminalState == first.terminalState );
    REQUIRE( third.replanCount == first.replanCount );
}

TEST_CASE( "oversized journals persist through the compaction projection", "[agent_loop][core]" )
{
    const std::string dir = uniqueTempDir( "oversize" );
    SessionJournal journal( "sess-oversize" );
    Json::Value payload( Json::objectValue );
    payload[ "blob" ] = std::string( 200 * 1024, 'x' );
    for ( int i = 0; i < 8; ++i )
        journal.append( "note", stages::kExecute, payload, i );

    std::string error;
    REQUIRE( journal.save( dir, &error ) );
    const auto loaded = SessionJournal::load( dir, "sess-oversize", &error );
    REQUIRE( loaded.has_value() );
    REQUIRE( loaded->entries().size() == 8 );
    // The payload bodies were compacted away on disk; the envelope survived.
    const std::string body = jsonToString( loaded->toJson() );
    REQUIRE( body.find( "xxxxxxxx" ) == std::string::npos );
    REQUIRE( body.find( "compacted" ) != std::string::npos );
    REQUIRE( fs::file_size( fs::path( dir ) / "sess-oversize.json" ) <
             static_cast< uintmax_t >( SessionJournal::kMaxDocumentBytes ) );
}

TEST_CASE( "identical journals serialize byte-identically", "[agent_loop][core]" )
{
    auto build = [] {
        SessionJournal j( "sess-det" );
        j.append( "stage_enter", stages::kPlanRequest, Json::Value( Json::objectValue ), 1 );
        j.append( "decision", stages::kPreflight, Json::Value( Json::objectValue ), 2,
                  makeDecision( stages::kPreflight, "deterministic" ) );
        return j;
    };
    REQUIRE( jsonToString( build().toJson() ) == jsonToString( build().toJson() ) );
}
