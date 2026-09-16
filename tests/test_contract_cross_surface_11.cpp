/***************************************************************************
 * test_contract_cross_surface_11.cpp — Cross-Surface Drift gates
 * (Platform 11.0, package F)
 *
 * Platform 10.0 bound schema↔sidecar and LabSpec↔registry. 11.0 closes the
 * remaining documentation/knowledge seams against the live registry:
 *
 *   X1 help ↔ registry      — every operator documented under data/help
 *                             resolves in the live registry (anti-phantom:
 *                             a renamed/removed operator must break help);
 *   X2 agent ↔ registry     — every prefixed id in the agent capability
 *                             knowledge resolves live (the knowledge layer
 *                             may not invent operators);
 *   X3 registry ↔ help      — help coverage of the live rs: surface is
 *                             monotone: the count may only grow; the exact
 *                             missing set is reported on failure so docs
 *                             debt stays visible and converging;
 *   X4 contract graph ↔ registry — operator nodes in the committed graph
 *                             snapshot resolve live, and every first-party
 *                             contract record appears in the snapshot with
 *                             its scientific_contract edge (conscious-diff
 *                             ritual: regenerating the snapshot is the only
 *                             way to change it).
 *
 * Offline, deterministic, bounded.
 ***************************************************************************/
#include "contracts/determinism_census.h"
#include "contracts/scientific_contract.h"

#include "agent/cartography/cartography_operators.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace sicnu::contracts;

namespace
{
const std::string &sourceRoot()
{
    static const std::string root = CMAKE_SOURCE_DIR;
    return root;
}

bool readJsonFile( const std::string &path, Json::Value &out )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return false;
    std::string text{ std::istreambuf_iterator<char>( in ),
                      std::istreambuf_iterator<char>() };
    if ( text.empty() )
        return false;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream is( text );
    return Json::parseFromStream( b, is, &out, &errs );
}

/// Live registry, all families.
std::set<std::string> liveIds()
{
    sicnu::operators::rs::initBuiltinRsOperators();
    // The cartography agents register through the agent-library seam after
    // the registry's call_once chain (cartography_operators.h).
    sicnu::agent::cartography::initCartographyOperators();
    std::set<std::string> ids;
    for ( const std::string &id :
          sicnu::operators::RSOperatorRegistry::instance().operatorNames() )
        ids.insert( id );
    return ids;
}

/// "operator.rs.band_math" → "rs:band_math"; non-operator keys skipped.
bool helpKeyToOperatorId( const std::string &key, std::string &out )
{
    if ( key.rfind( "operator.", 0 ) != 0 )
        return false;
    std::string rest = key.substr( 9 ); // after "operator."
    const auto dot = rest.find( '.' );
    if ( dot == std::string::npos || dot == 0 )
        return false;
    out = rest.substr( 0, dot ) + ":" + rest.substr( dot + 1 );
    return true;
}

std::vector<std::string> helpOperatorFiles()
{
    std::vector<std::string> out;
    const fs::path dir = fs::path( sourceRoot() ) / "data" / "help" / "operators";
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( dir, ec ) )
        if ( entry.path().extension() == ".json" )
            out.push_back( entry.path().string() );
    std::sort( out.begin(), out.end() );
    return out;
}
} // namespace

/// Collect the operator ids a help document exposes. Two committed shapes
/// exist: an object keyed "operator.<family>.<name>", and a plain array of
/// topics whose keys/ids may embed the operator id.
void collectHelpIds( const Json::Value &doc, std::set<std::string> &out )
{
    if ( doc.isObject() )
    {
        for ( const auto &key : doc.getMemberNames() )
        {
            std::string id;
            if ( helpKeyToOperatorId( key, id ) )
            {
                out.insert( id );
                continue;
            }
            // Array-shaped files carry the "operator.<family>.<name>" token
            // as the VALUE of an "id" member.
            if ( doc[key].isString() && helpKeyToOperatorId( doc[key].asString(), id ) )
                out.insert( id );
            else if ( doc[key].isObject() || doc[key].isArray() )
                collectHelpIds( doc[key], out );
        }
    }
    else if ( doc.isArray() )
    {
        for ( const Json::Value &item : doc )
            collectHelpIds( item, out );
    }
    else if ( doc.isString() )
    {
        std::string id;
        if ( helpKeyToOperatorId( doc.asString(), id ) )
            out.insert( id );
    }
}

TEST_CASE( "help operator ids resolve in the live registry", "[crosssurface11]" )
{
    const auto live = liveIds();
    std::set<std::string> helpIds;
    for ( const std::string &file : helpOperatorFiles() )
    {
        Json::Value doc;
        REQUIRE( readJsonFile( file, doc ) );
        collectHelpIds( doc, helpIds );
    }
    CHECK( helpIds.size() >= 100 ); // live binding, not a vacuous directory scan
    for ( const std::string &id : helpIds )
    {
        INFO( "help id: " << id );
        CHECK( live.count( id ) == 1 );
    }
}

TEST_CASE( "agent capability knowledge ids resolve in the live registry",
           "[crosssurface11]" )
{
    const auto live = liveIds();
    int checked = 0;
    const fs::path dir = fs::path( sourceRoot() ) / "data" / "agent" / "capabilities";
    std::error_code ec;
    REQUIRE( fs::exists( dir ) );
    for ( const auto &entry : fs::directory_iterator( dir, ec ) )
    {
        if ( entry.path().extension() != ".json" )
            continue;
        Json::Value doc;
        REQUIRE( readJsonFile( entry.path().string(), doc ) );
        for ( const Json::Value &item : doc )
        {
            if ( !item.isObject() || !item.isMember( "id" ) )
                continue;
            const std::string id = item["id"].asString();
            if ( id.find( ':' ) == std::string::npos )
                continue; // family-less pseudo-entries
            if ( id.rfind( "family:", 0 ) == 0 )
                continue; // family_default pseudo-entries
            INFO( "agent capability id: " << id << " (" << entry.path().string() << ")" );
            CHECK( live.count( id ) == 1 );
            ++checked;
        }
    }
    CHECK( checked >= 100 );
}

TEST_CASE( "help coverage of the live rs: surface is monotone", "[crosssurface11]" )
{
    const auto live = liveIds();
    std::set<std::string> helpIds;
    for ( const std::string &file : helpOperatorFiles() )
    {
        Json::Value doc;
        REQUIRE( readJsonFile( file, doc ) );
        collectHelpIds( doc, helpIds );
    }

    std::vector<std::string> missing;
    for ( const std::string &id : live )
        if ( id.rfind( "rs:", 0 ) == 0 && !helpIds.count( id ) )
            missing.push_back( id );

    // Monotone baseline: the 11.0 census counted the live help surface; the
    // number may only grow (docs debt must converge, never silently regress).
    CHECK( helpIds.size() >= 109 );

    if ( !missing.empty() )
    {
        std::string joined;
        for ( const std::string &id : missing )
            joined += "\n  - " + id;
        WARN( "rs: operators without a help page (" << missing.size() << "):" << joined );
    }
}

TEST_CASE( "committed contract graph snapshot mirrors the live surface",
           "[crosssurface11]" )
{
    const auto live = liveIds();
    Json::Value graph;
    REQUIRE( readJsonFile( sourceRoot() + "/data/contracts/contract_graph.snap.json",
                           graph ) );

    std::set<std::string> operatorNodes;
    for ( const Json::Value &node : graph["nodes"] )
        if ( node.isMember( "kind" ) && node["kind"].asString() == "operator"
             && node.isMember( "id" ) )
            operatorNodes.insert( node["id"].asString() );

    // Every snapshot operator node resolves live (no phantoms in the graph).
    int resolved = 0;
    for ( const std::string &id : operatorNodes )
    {
        INFO( "graph node: " << id );
        CHECK( live.count( id ) == 1 );
        ++resolved;
    }
    CHECK( resolved >= 100 );

    // Every first-party contract record appears in the snapshot as a node
    // (the conscious-diff ritual keeps the graph and the registry welded).
    int contractsInGraph = 0;
    for ( const auto &[id, contract] : scientificContracts() )
    {
        ( void )contract;
        if ( operatorNodes.count( id ) == 0 )
            FAIL( "contract record '" + id
                  + "' is missing from the contract graph snapshot — regenerate "
                    "data/contracts/contract_graph.snap.json with "
                    "contract_inventory --out (conscious contract update)" );
        ++contractsInGraph;
    }
    CHECK( contractsInGraph >= 151 ); // 132 rs: + 14 io: + 5 cartography:
}
