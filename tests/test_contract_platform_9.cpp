/***************************************************************************
 * test_contract_platform_9.cpp
 *
 * Contract Platform 9.0 (M0) — the contract inventory graph on the live
 * tree:
 *
 *   1. assembly completeness: every authoritative surface contributes
 *      nodes (operators, commands, help topics, diagnostics, error codes,
 *      capability entries) and assembly notes are empty (no unreadable
 *      data);
 *   2. graph integrity: zero duplicate nodes, zero dangling references —
 *      the whole reference graph (surface lookups, CTAs, preflight
 *      actions, help topics, capability entries, diagnostics) resolves;
 *   3. snapshot freshness: the committed data/contracts snapshot
 *      byte-compares against a fresh canonical generation — any contract
 *      change must consciously regenerate it (R6);
 *   4. canonical graph round-trip.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "contracts/contract_graph.h"
#include "contracts/graph_assembly.h"

#include <operators/framework/rs_operator_registry.h>
#include <operators/rs/rs_operators_init.h>

#include <json/json.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::contracts;

namespace {

const char *kSourceDir = CMAKE_SOURCE_DIR;

AssemblyResult assembleLive()
{
    sicnu::operators::rs::initBuiltinRsOperators();
    sicnu::operators::RSOperatorRegistry::instance();
    return buildLiveGraph( kSourceDir );
}

std::size_t countKind( const ContractGraph &g, const char *kind )
{
    std::size_t n = 0;
    for ( const auto &node : g.nodes() )
        if ( node.kind == kind )
            ++n;
    return n;
}

std::string canonicalJson( const Json::Value &root )
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "  ";
    std::string out = Json::writeString( b, root );
    out += "\n";
    return out;
}

} // namespace

TEST_CASE( "Contract graph: every authoritative surface contributes nodes",
           "[contracts9][graph]" )
{
    const auto assembled = assembleLive();
    const auto &g = assembled.graph;

    CHECK( countKind( g, "operator" ) >= 100 );
    CHECK( countKind( g, "command" ) >= 40 );
    CHECK( countKind( g, "help_topic" ) >= 100 );
    CHECK( countKind( g, "diagnostic" ) >= 50 );
    CHECK( countKind( g, "error_code" ) >= 30 );
    CHECK( countKind( g, "capability_entry" ) >= 20 );
    CHECK( g.edges().size() >= 250 );

    // Assembly honesty: no unreadable data files.
    CHECK( assembled.notes.empty() );
}

TEST_CASE( "Contract graph: no duplicate nodes, no dangling references "
           "(allow-listed)",
           "[contracts9][graph][integrity]" )
{
    // Live findings recorded in REVIEW_LOG.md: the preflight emits agent
    // tool suggestions with dot separators / stale ids. The emitting file
    // was owned by the open spatial-scientist-harness-9 PR (#885); the
    // assembler now pins the preflight_action vocabulary as nodes (D10
    // baseline repair), which resolved every recorded entry — the list is
    // empty and the rot guard below stays armed for future drift.
    static const std::set<std::string> kOwnedBy885 = {};
    const auto assembled = assembleLive();
    const auto findings = assembled.graph.computeFindings();
    std::set<std::string> danglingActions;
    for ( const auto &f : findings )
    {
        INFO( "finding: " << f.kind << " " << f.id << " — " << f.detail );
        const bool known =
            f.kind == "dangling_ref" &&
            f.id.rfind( "preflight_action:", 0 ) == 0 &&
            kOwnedBy885.count( f.id.substr( 17 ) ) == 1;
        CHECK( known );
        if ( known )
            danglingActions.insert( f.id.substr( 17 ) );
    }
    // Rot guard: an OWNED-BY-#885 entry with no live finding must go.
    for ( const auto &id : kOwnedBy885 )
    {
        INFO( "stale OWNED-BY-#885 entry: " << id );
        CHECK( danglingActions.count( id ) == 1 );
    }
}

TEST_CASE( "Contract graph: canonical serialization round-trips",
           "[contracts9][graph]" )
{
    const auto assembled = assembleLive();
    const Json::Value doc = assembled.graph.toJson();
    REQUIRE( doc["schema"].asString() == "exp.contract.graph.v1" );

    ContractGraph restored;
    std::string error;
    REQUIRE( ContractGraph::fromJson( doc, restored, error ) );
    INFO( "error: " << error );
    CHECK( restored.nodes().size() == assembled.graph.nodes().size() );
    CHECK( restored.edges().size() == assembled.graph.edges().size() );
    // Deterministic: re-serialization is byte-identical.
    CHECK( canonicalJson( restored.toJson() ) == canonicalJson( doc ) );
}

TEST_CASE( "Contract snapshot is fresh (byte-compare against live graph)",
           "[contracts9][graph][snapshot]" )
{
    auto assembled = assembleLive();
    // Snapshot gates on structure, not on transitory findings: findings are
    // asserted empty in the integrity test, the snapshot pins the
    // node/edge inventory itself.
    const std::string snapshotPath =
        std::string( kSourceDir ) + "/data/contracts/contract_graph.snap.json";

    const std::string fresh = canonicalJson( assembled.graph.toJson() );

    // Deliberate regeneration path (R6): a contract change must consciously
    // refresh the snapshot. Setting SICNU_CONTRACT_SNAPSHOT_WRITE=<path>
    // writes the fresh canonical JSON to <path> and skips the byte-compare
    // for this run; commit the result with the contract change.
    if ( const char *writePath = std::getenv( "SICNU_CONTRACT_SNAPSHOT_WRITE" ) )
    {
        std::ofstream out( writePath, std::ios::binary );
        REQUIRE_FALSE( out.fail() );
        out << fresh;
        WARN( "contract snapshot regenerated: " << writePath );
        return;
    }

    std::ifstream in( snapshotPath, std::ios::binary );
    REQUIRE_FALSE( in.fail() ); // snapshot must exist
    const std::string committed{
        std::istreambuf_iterator<char>( in ),
        std::istreambuf_iterator<char>{} };
    if ( committed != fresh )
    {
        // Distinguish formatting-only drift from real inventory drift.
        ContractGraph committedGraph;
        Json::Value committedDoc;
        Json::CharReaderBuilder rb;
        std::string errs;
        std::istringstream is( committed );
        const bool parsed =
            Json::parseFromStream( rb, is, &committedDoc, &errs );
        REQUIRE( parsed );
        REQUIRE( ContractGraph::fromJson( committedDoc, committedGraph,
                                          errs ) );
        CHECK( committedGraph.nodes().size() ==
               assembled.graph.nodes().size() );
        CHECK( committedGraph.edges().size() ==
               assembled.graph.edges().size() );
        // Inventories match but bytes differ → serialization changed;
        // regenerate. Inventories differ → a real contract change landed:
        // regenerate and review the diff consciously (R6).
        FAIL( "contract snapshot stale — regenerate with: "
              "cmake --build build --target contract_inventory && "
              "./build/src/contracts/contract_inventory --source-root " +
              std::string( kSourceDir ) + " --out " + snapshotPath );
    }
    CHECK( committed == fresh );
}
