/***************************************************************************
 * test_snapshot_drift_12.cpp — Platform 12.0 · Oracle O-6
 *
 * A regenerated contract snapshot must be STRUCTURALLY SOUND, not merely
 * byte-fresh. This lane is the direct encoding of EVIDENCE.md E-13.
 *
 * WHY THIS EXISTS
 *
 *   The committed snapshots are gated by a byte comparison
 *   (`contract_inventory --check`). A byte comparison can report exactly one
 *   thing: "these bytes differ". It cannot distinguish
 *
 *       (a) "content moved"               — benign, expected after a change
 *       (b) "the graph no longer holds together" — a broken contract surface
 *
 *   E-13 proved that matters. A fresh assembly of the live tree reports FIVE
 *   dangling `diagnostic_for` edges (harness codes that exist in
 *   harness_error.cpp but are absent from harness_error.h, which is the only
 *   file the assembler scans). The committed snapshot reports ZERO, because it
 *   predates the diagnostics that reference those codes.
 *
 *   So the gate as it stands was red for reason (a) while hiding reason (b).
 *   The named remedy printed by the tool — "regenerate and review the diff as
 *   a conscious contract update" — would have written those five dangling
 *   edges into the new baseline. That is the brief's forbidden move: blessing
 *   unknown drift to make a snapshot green.
 *
 * WHAT THIS LANE ASSERTS
 *
 *   1. The committed snapshot parses through ContractGraph::fromJson and keeps
 *      its declared schema. (A snapshot nobody can read is not a contract.)
 *   2. Every edge endpoint in the COMMITTED snapshot resolves to a node in the
 *      same snapshot. This is a pure, self-contained invariant: if the file is
 *      internally inconsistent, the file is wrong, regardless of what the live
 *      tree says.
 *   3. The SAME invariant holds for a freshly assembled live graph, and the
 *      assembled findings are surfaced verbatim so a failure names each broken
 *      edge instead of a count.
 *   4. The live/committed drift is rendered as a READABLE report (the
 *      snapshot_diff component), so the failure output tells a maintainer what
 *      changed rather than only that something did.
 *
 * ASSERTIONS 2 AND 3 ARE THE POINT. Assertion 2 is a hard invariant and is
 * expected to pass. Assertion 3 is expected to FAIL on master, with exactly
 * the five E-13 edges — that red is the deliverable, because it converts a
 * silent "byte mismatch" into an actionable "these five references are broken,
 * and here is the file that must change".
 *
 * DESIGN NOTES
 *  - The lane is read-only: it never writes a snapshot. Regeneration stays a
 *    deliberate, reviewed human action.
 *  - It does NOT compare bytes. Byte freshness is the existing gate's job; this
 *    lane answers the different question the existing gate cannot.
 *  - It does NOT weaken the E-13 findings by allow-listing the five ids. An
 *    allow-list here would recreate the very defect being fixed: a recorded
 *    mismatch nobody acts on.
 ***************************************************************************/
#include "contracts/contract_graph.h"
#include "contracts/graph_assembly.h"
#include "contracts/snapshot_diff.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string sourceRoot()
{
    return std::string( CMAKE_SOURCE_DIR );
}

/// Read a file whole. Returns false when it cannot be opened.
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

std::string graphSnapshotPath()
{
    return sourceRoot() + "/data/contracts/contract_graph.snap.json";
}

/// Parse a JSON document from a string. Returns false with a reason.
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

// ─────────────────────────────────────────────────────────────────────────────
// 1 · the committed snapshot is readable and self-consistent
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "the committed contract snapshot parses and is internally consistent",
           "[snapshotdrift12][o6]" )
{
    std::string text;
    REQUIRE( readWhole( graphSnapshotPath(), text ) );
    REQUIRE( !text.empty() );

    Json::Value doc;
    std::string perr;
    REQUIRE( parseJson( text, doc, perr ) );

    sicnu::contracts::ContractGraph graph;
    std::string gerr;
    REQUIRE( sicnu::contracts::ContractGraph::fromJson( doc, graph, gerr ) );
    INFO( "schema: " << doc.get( "schema", "" ).asString() );
    CHECK( doc.get( "schema", "" ).asString()
           == "exp.contract.graph.v1" );

    // The invariant: no edge endpoint may be absent from the node set of the
    // same file. This is self-contained — it does not consult the live tree.
    std::set<std::string> nodeIds;
    for ( const auto &n : graph.nodes() )
        nodeIds.insert( n.id );

    std::vector<std::string> dangling;
    for ( const auto &e : graph.edges() )
    {
        if ( nodeIds.count( e.from ) == 0 )
            dangling.push_back( "from missing: " + e.kind + " " + e.from );
        if ( nodeIds.count( e.to ) == 0 )
            dangling.push_back( "to missing: " + e.kind + " " + e.from + " -> "
                                + e.to );
    }

    // Also surface the graph's own findings, which is the authoritative check.
    const auto findings = graph.computeFindings();
    for ( const auto &f : findings )
        UNSCOPED_INFO( "committed snapshot finding: " << f.kind << " " << f.id
                                                      << " (" << f.detail << ")" );
    for ( const auto &d : dangling )
        UNSCOPED_INFO( "committed snapshot dangling endpoint: " << d );

    INFO( "nodes: " << graph.nodes().size()
                    << " edges: " << graph.edges().size()
                    << " findings: " << findings.size() );

    CHECK( findings.empty() );
    CHECK( dangling.empty() );
}

// ─────────────────────────────────────────────────────────────────────────────
// 2 · a freshly assembled graph must also be sound — E-13's gate
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "a fresh live assembly reports no structural findings",
           "[snapshotdrift12][o6][e13]" )
{
    // This is the assertion the byte gate could not make. On master it FAILS
    // with the five E-13 dangling diagnostic_for edges; that failure is the
    // point of the lane.
    const auto assembled = sicnu::contracts::buildLiveGraph( sourceRoot() );

    for ( const auto &note : assembled.notes )
        UNSCOPED_INFO( "assembly note: " << note );

    const auto findings = assembled.graph.computeFindings();
    for ( const auto &f : findings )
        UNSCOPED_INFO( "live graph finding: " << f.kind << " " << f.id << " ("
                                              << f.detail << ")" );

    INFO( "live nodes: " << assembled.graph.nodes().size()
                         << " live edges: " << assembled.graph.edges().size()
                         << " findings: " << findings.size() );

    // Surface the specific broken references by name, so the failure output is
    // actionable rather than "expected 0, got 5".
    for ( const auto &f : findings )
        if ( f.kind == "dangling_ref" )
            UNSCOPED_INFO( "broken reference: " << f.id );

    CHECK( findings.empty() );
}

// ─────────────────────────────────────────────────────────────────────────────
// 3 · drift between committed and live renders readably
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "committed-vs-live drift renders as a readable report",
           "[snapshotdrift12][o6]" )
{
    std::string text;
    REQUIRE( readWhole( graphSnapshotPath(), text ) );

    Json::Value committed;
    std::string perr;
    REQUIRE( parseJson( text, committed, perr ) );

    const auto assembled = sicnu::contracts::buildLiveGraph( sourceRoot() );
    const Json::Value live = assembled.graph.toJson();

    sicnu::contracts::SnapshotDiffReport report;
    std::string derr;
    REQUIRE( sicnu::contracts::diffContractSnapshot( committed, live, report,
                                                     derr ) );

    const std::string rendered = sicnu::contracts::formatSnapshotDiff( report );
    UNSCOPED_INFO( "drift report:\n" << rendered );

    // The report must at minimum agree with the tool's own accounting: the
    // committed snapshot and the live graph differ, and the report must say
    // how. It must never claim "no differences" while a byte gate is failing.
    CHECK( report.schema == "exp.contract.graph.v1" );
    CHECK_FALSE( report.schemaMismatch );

    // Sanity on the renderer: a non-empty report must produce lines.
    if ( !report.empty() )
        CHECK( rendered.find( "differences:" ) != std::string::npos );
}

// ─────────────────────────────────────────────────────────────────────────────
// 4 · the drift rules themselves (unit-level, independent of the tree)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE( "the drift classifier distinguishes payload moves from element changes",
           "[snapshotdrift12][o6][unit]" )
{
    // A hand-built pair: `origin` is payload, so moving it must be ONE
    // `changed` line, not an add+remove pair. Getting this wrong is what makes
    // a drift report unreadable in practice, so it is pinned here.
    const std::string committedText = R"({
      "schema": "exp.contract.graph.v1",
      "nodes": [
        { "id": "op:a", "kind": "operator", "origin": "src/a.cpp" },
        { "id": "op:b", "kind": "operator", "origin": "src/b.cpp" }
      ],
      "edges": [
        { "kind": "capability_for", "from": "op:a", "to": "op:a", "origin": "data/a.json" }
      ]
    })";
    const std::string liveText = R"({
      "schema": "exp.contract.graph.v1",
      "nodes": [
        { "id": "op:a", "kind": "operator", "origin": "src/moved_a.cpp" },
        { "id": "op:c", "kind": "operator", "origin": "src/c.cpp" }
      ],
      "edges": [
        { "kind": "capability_for", "from": "op:a", "to": "op:a", "origin": "data/a.json" }
      ]
    })";

    Json::Value committed, live;
    std::string e;
    REQUIRE( parseJson( committedText, committed, e ) );
    REQUIRE( parseJson( liveText, live, e ) );

    sicnu::contracts::SnapshotDiffReport report;
    std::string derr;
    REQUIRE( sicnu::contracts::diffContractSnapshot( committed, live, report,
                                                     derr ) );
    UNSCOPED_INFO( sicnu::contracts::formatSnapshotDiff( report ) );

    // op:b removed, op:c added, op:a's origin changed. The edge is untouched.
    CHECK( report.size() == 3 );

    int changed = 0, added = 0, removed = 0;
    bool originChangeSeen = false;
    for ( const auto &l : report.lines )
    {
        if ( l.change == "changed" )
        {
            ++changed;
            if ( l.id == "op:a" && l.field == "origin" )
            {
                originChangeSeen = true;
                CHECK( l.before == "src/a.cpp" );
                CHECK( l.after == "src/moved_a.cpp" );
            }
        }
        else if ( l.change == "added" )
            ++added;
        else if ( l.change == "removed" )
            ++removed;
    }
    CHECK( added == 1 );
    CHECK( removed == 1 );
    CHECK( changed == 1 );
    CHECK( originChangeSeen );
}

TEST_CASE( "the drift classifier refuses mismatched or unknown schemas",
           "[snapshotdrift12][o6][unit]" )
{
    Json::Value graph, census, unknown, noschema;
    std::string e;
    REQUIRE( parseJson( R"({"schema":"exp.contract.graph.v1","nodes":[],"edges":[]})",
                        graph, e ) );
    REQUIRE( parseJson( R"({"schema":"exp.determinism_census.v1","entries":[]})",
                        census, e ) );
    REQUIRE( parseJson( R"({"schema":"exp.unknown.v9"})", unknown, e ) );
    REQUIRE( parseJson( R"({"nodes":[]})", noschema, e ) );

    sicnu::contracts::SnapshotDiffReport report;
    std::string derr;

    // Different schemas are a mismatch, not a silent empty diff.
    REQUIRE( sicnu::contracts::diffContractSnapshot( graph, census, report, derr ) );
    CHECK( report.schemaMismatch );

    // An unrecognized schema is an error, so it cannot be mistaken for "no drift".
    CHECK_FALSE( sicnu::contracts::diffContractSnapshot( unknown, unknown, report, derr ) );
    CHECK_FALSE( derr.empty() );

    // A missing schema is an error too.
    CHECK_FALSE( sicnu::contracts::diffContractSnapshot( noschema, graph, report, derr ) );
    CHECK_FALSE( derr.empty() );

    // And a graph-vs-graph diff of identical documents is genuinely empty.
    REQUIRE( sicnu::contracts::diffContractSnapshot( graph, graph, report, derr ) );
    CHECK( report.empty() );
    CHECK_FALSE( report.schemaMismatch );
}
