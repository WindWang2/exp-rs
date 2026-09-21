// tests/test_autonomy_audit.cpp
//
// RS14-12 teaching autonomy ladder — Slice F: the decision audit log.
//
// Every allow/deny/downgrade is recorded with its typed reason so "why was
// this forbidden" is answerable after the fact. Properties under test:
//   * monotonic sequence numbers, never reused after eviction;
//   * deterministic JSON — no wall-clock anywhere, so an identical request
//     sequence replays byte-identically;
//   * bounded memory (FIFO eviction at the cap);
//   * thread-safe concurrent recording.
// Pure value-object suite: no Qt, no QGIS, no network.

#include <catch2/catch_test_macros.hpp>

#include "agent/autonomy/autonomy_audit.h"
#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_policy.h"

#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::agent::autonomy;

namespace {

AutonomyPolicy practicePolicy( const std::string &level )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson(
        std::string( R"({"schema":"sicnu.autonomy-policy/1","level":")" ) + level +
        R"(","mode":"practice"})" );
    REQUIRE( parsed.ok );
    return parsed.policy;
}

AutonomyRequest labRequest( const std::string &role, const std::string &capability )
{
    AutonomyRequest request;
    request.domain = "lab";
    request.role = role;
    request.capability = capability;
    request.intent = "lab_hint";
    return request;
}

} // namespace

TEST_CASE( "audit log records decisions with monotonic sequences", "[autonomy][audit]" )
{
    AutonomyAuditLog &log = AutonomyAuditLog::instance();
    log.clear();

    const AutonomyPolicy policy = practicePolicy( "L2" );
    const AutonomyRequest request = labRequest( "student", assistance_capabilities::kConceptHint );
    const AutonomyDecision decision = decideAutonomy( policy, request );

    log.record( request, policy, decision );
    log.record( request, policy, decision );

    const std::vector<AutonomyAuditRecord> records = log.records();
    REQUIRE( records.size() == 2 );
    REQUIRE( records[ 0 ].sequence == 1 );
    REQUIRE( records[ 1 ].sequence == 2 );
    REQUIRE( records[ 0 ].schema == kAutonomyDecisionSchema );
    REQUIRE( records[ 0 ].decision == "allow" );
    REQUIRE( records[ 0 ].reasonCode == autonomy_reason_codes::kAllowed );
    REQUIRE( records[ 0 ].mode == autonomy_modes::kPractice );
    REQUIRE( records[ 0 ].role == "student" );
    REQUIRE( records[ 0 ].capability == assistance_capabilities::kConceptHint );
    REQUIRE( records[ 0 ].effectiveLevel == AutonomyLevel::L2 );
    log.clear();
}

TEST_CASE( "audit log records denials and downgrades with their reasons", "[autonomy][audit]" )
{
    AutonomyAuditLog &log = AutonomyAuditLog::instance();
    log.clear();

    const AutonomyPolicy policy = practicePolicy( "L2" );
    const AutonomyRequest denied =
        labRequest( "student", assistance_capabilities::kAutonomousExecution );
    log.record( denied, policy, decideAutonomy( policy, denied ) );

    const AutonomyRequest downgraded =
        labRequest( "student", assistance_capabilities::kNextStepRecommendation );
    log.record( downgraded, policy, decideAutonomy( policy, downgraded ) );

    const std::vector<AutonomyAuditRecord> records = log.records();
    REQUIRE( records.size() == 2 );
    REQUIRE( records[ 0 ].decision == "deny" );
    REQUIRE( records[ 0 ].reasonCode == autonomy_reason_codes::kLabStudentExecution );
    REQUIRE( records[ 1 ].decision == "downgrade" );
    REQUIRE( records[ 1 ].reasonCode == autonomy_reason_codes::kDowngraded );
    REQUIRE( records[ 1 ].downgradeTo == assistance_capabilities::kErrorLocalization );
    log.clear();
}

TEST_CASE( "audit JSON is deterministic and replays byte-identically", "[autonomy][audit]" )
{
    const AutonomyPolicy policy = practicePolicy( "L3" );
    const std::vector<AutonomyRequest> requests = {
        labRequest( "student", assistance_capabilities::kConceptHint ),
        labRequest( "student", assistance_capabilities::kNextStepRecommendation ),
        labRequest( "student", assistance_capabilities::kAutonomousExecution ),
    };

    AutonomyAuditLog &log = AutonomyAuditLog::instance();
    log.clear();
    for ( const AutonomyRequest &request : requests )
        log.record( request, policy, decideAutonomy( policy, request ) );
    const std::string first = log.toJson().toStyledString();
    log.clear();

    for ( const AutonomyRequest &request : requests )
        log.record( request, policy, decideAutonomy( policy, request ) );
    const std::string second = log.toJson().toStyledString();
    log.clear();

    REQUIRE( first == second );
    REQUIRE( first.find( "generated" ) == std::string::npos );
    REQUIRE( first.find( "timestamp" ) == std::string::npos );
}

TEST_CASE( "audit log is bounded with FIFO eviction and monotonic sequences", "[autonomy][audit]" )
{
    AutonomyAuditLog &log = AutonomyAuditLog::instance();
    log.clear();

    const AutonomyPolicy policy = practicePolicy( "L3" );
    const AutonomyRequest request = labRequest( "student", assistance_capabilities::kConceptHint );
    const AutonomyDecision decision = decideAutonomy( policy, request );

    const int total = static_cast<int>( AutonomyAuditLog::kMaxRecords ) + 5;
    for ( int index = 0; index < total; ++index )
        log.record( request, policy, decision );

    const std::vector<AutonomyAuditRecord> records = log.records();
    REQUIRE( records.size() == AutonomyAuditLog::kMaxRecords );
    REQUIRE( records.front().sequence == 6 );
    REQUIRE( records.back().sequence == static_cast<std::uint64_t>( total ) );
    log.clear();
}

TEST_CASE( "audit log tolerates concurrent recording", "[autonomy][audit]" )
{
    AutonomyAuditLog &log = AutonomyAuditLog::instance();
    log.clear();

    const AutonomyPolicy policy = practicePolicy( "L3" );
    const AutonomyRequest request = labRequest( "student", assistance_capabilities::kConceptHint );
    const AutonomyDecision decision = decideAutonomy( policy, request );

    const int threads = 4;
    const int perThread = 250;
    std::vector<std::thread> workers;
    for ( int index = 0; index < threads; ++index )
        workers.emplace_back( [ &log, &request, &policy, &decision, perThread ]() {
            for ( int round = 0; round < perThread; ++round )
                log.record( request, policy, decision );
        } );
    for ( std::thread &worker : workers )
        worker.join();

    const std::vector<AutonomyAuditRecord> records = log.records();
    REQUIRE( records.size() == threads * perThread );
    std::set<std::uint64_t> sequences;
    for ( const AutonomyAuditRecord &record : records )
    {
        REQUIRE( record.sequence >= 1 );
        REQUIRE( sequences.insert( record.sequence ).second );
    }
    log.clear();
}
