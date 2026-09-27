/***************************************************************************
 * test_snapshot_gate_r4.cpp — R4 processing-meta track snapshot gate
 *
 * Track: hardening/r4-processing-meta. Complements test_snapshot_drift_12
 * (which answers "is the graph sound / how does drift render?") with the
 * assertions the byte gate itself makes, in-test:
 *
 *   1. double-run stability — generating the live graph and the determinism
 *      census twice produces byte-identical canonical JSON (a flaky generator
 *      would make every regeneration "fix" noise);
 *   2. committed freshness — the committed data/contracts/*.snap.json files
 *      equal the fresh generation byte-for-byte (this is contract_inventory
 *      --check / --census-check as a Catch2 assertion, so "算子/元数据改了、
 *      快照没跟" is red locally, not only in the CLI gate);
 *   3. drift localization — a synthetic payload perturbation of the committed
 *      snapshot must yield a non-empty SnapshotDiffReport whose rendered
 *      lines name the drifted element (formatSnapshotDiff must attribute,
 *      not just count).
 *
 * The lane stays read-only: it never writes snapshots. Regeneration is the
 * documented remedy (contract_inventory --out / --census-out) and is a
 * reviewed commit, not a test side effect.
 ***************************************************************************/
#include "contracts/contract_graph.h"
#include "contracts/determinism_census.h"
#include "contracts/graph_assembly.h"
#include "contracts/snapshot_diff.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

std::string sourceRoot()
{
    return std::string( CMAKE_SOURCE_DIR );
}

bool readWhole( const std::string &path, std::string &out )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

/// The exact serialization the byte gate compares (contract_inventory_main).
std::string canonicalJson( const Json::Value &root )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "  ";
    std::string out = Json::writeString( builder, root );
    out += "\n";
    return out;
}

bool parseJson( const std::string &text, Json::Value &out, std::string &error )
{
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    std::string errs;
    if ( !reader->parse( text.data(), text.data() + text.size(), &out, &errs ) )
    {
        error = errs;
        return false;
    }
    return true;
}

} // namespace

TEST_CASE( "live graph and census generation are byte-stable across two runs",
           "[snapshotgate][r4]" )
{
    const auto first = sicnu::contracts::buildLiveGraph( sourceRoot() );
    const std::string firstJson = canonicalJson( first.graph.toJson() );

    const auto second = sicnu::contracts::buildLiveGraph( sourceRoot() );
    const std::string secondJson = canonicalJson( second.graph.toJson() );

    INFO( "nodes: " << first.graph.nodes().size() );
    REQUIRE( firstJson == secondJson );

    const auto censusA =
        sicnu::contracts::determinismCensusToJson(
            sicnu::contracts::buildDeterminismCensus( sourceRoot() ) );
    const auto censusB =
        sicnu::contracts::determinismCensusToJson(
            sicnu::contracts::buildDeterminismCensus( sourceRoot() ) );
    REQUIRE( canonicalJson( censusA ) == canonicalJson( censusB ) );
}

TEST_CASE( "committed snapshots equal the fresh generation byte-for-byte",
           "[snapshotgate][r4]" )
{
    std::string graphText;
    REQUIRE( readWhole( sourceRoot() + "/data/contracts/contract_graph.snap.json",
                        graphText ) );
    const auto assembled = sicnu::contracts::buildLiveGraph( sourceRoot() );
    const std::string liveJson = canonicalJson( assembled.graph.toJson() );
    if ( graphText != liveJson )
    {
        sicnu::contracts::SnapshotDiffReport report;
        std::string derr;
        Json::Value committed, live;
        std::string perr;
        if ( parseJson( graphText, committed, perr ) &&
             parseJson( liveJson, live, perr ) &&
             sicnu::contracts::diffContractSnapshot( committed, live, report, derr ) )
        {
            UNSCOPED_INFO( "graph snapshot drift:\n"
                           << sicnu::contracts::formatSnapshotDiff( report ) );
        }
        FAIL( "contract_graph.snap.json is stale — regenerate with "
              "contract_inventory --source-root <repo> --out "
              "data/contracts/contract_graph.snap.json" );
    }

    std::string censusText;
    REQUIRE( readWhole(
        sourceRoot() + "/data/contracts/determinism_census.snap.json",
        censusText ) );
    const auto census =
        sicnu::contracts::determinismCensusToJson(
            sicnu::contracts::buildDeterminismCensus( sourceRoot() ) );
    if ( censusText != canonicalJson( census ) )
    {
        FAIL( "determinism_census.snap.json is stale — regenerate with "
              "contract_inventory --source-root <repo> --census-out "
              "data/contracts/determinism_census.snap.json" );
    }
}

TEST_CASE( "synthetic snapshot drift is attributed, not just counted",
           "[snapshotgate][r4]" )
{
    std::string graphText;
    REQUIRE( readWhole( sourceRoot() + "/data/contracts/contract_graph.snap.json",
                        graphText ) );
    Json::Value committed;
    std::string perr;
    REQUIRE( parseJson( graphText, committed, perr ) );

    // Inject payload drift on one node: move its origin attribute. Identity is
    // (kind, id), so this must render as a single "changed" line naming that
    // node — the property that makes a tripped byte gate actionable.
    REQUIRE( committed[ "nodes" ].isArray() );
    REQUIRE_FALSE( committed[ "nodes" ].empty() );
    const std::string nodeId = committed[ "nodes" ][ 0 ][ "id" ].asString();
    const std::string before = committed[ "nodes" ][ 0 ][ "origin" ].asString();
    committed[ "nodes" ][ 0 ][ "origin" ] = before + " (drifted)";

    Json::Value live;
    REQUIRE( parseJson( canonicalJson( committed ), live, perr ) ); // re-serialize
    // The live document for the diff is the UNDRIFTED generation; comparing
    // drifted-recorded vs live must localize the change.
    const auto assembled = sicnu::contracts::buildLiveGraph( sourceRoot() );
    const Json::Value fresh = assembled.graph.toJson();

    sicnu::contracts::SnapshotDiffReport report;
    std::string derr;
    REQUIRE( sicnu::contracts::diffContractSnapshot( committed, fresh, report, derr ) );
    CHECK_FALSE( report.schemaMismatch );
    REQUIRE_FALSE( report.empty() );

    const std::string rendered = sicnu::contracts::formatSnapshotDiff( report );
    UNSCOPED_INFO( "drift report:\n" << rendered );
    CHECK( rendered.find( "differences:" ) != std::string::npos );
    CHECK( rendered.find( nodeId ) != std::string::npos );
    bool originAttributed = false;
    for ( const auto &line : report.lines )
        if ( line.change == "changed" && line.id.find( nodeId ) != std::string::npos &&
             line.field == "origin" )
            originAttributed = true;
    CHECK( originAttributed );
}
