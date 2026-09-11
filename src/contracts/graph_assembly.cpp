/***************************************************************************
 * graph_assembly.cpp — build the contract graph from live sources (M0)
 ***************************************************************************/
#include "graph_assembly.h"

#include "command_ref_scanner.h"
#include "error_code_scanner.h"
#include "operator_param_scanner.h"
#include "text_scan.h"

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sicnu::contracts {

namespace {

std::string readFile( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    return std::string( std::istreambuf_iterator<char>( in ),
                        std::istreambuf_iterator<char>() );
}

bool readJson( const std::string &path, Json::Value &out )
{
    const std::string text = readFile( path );
    if ( text.empty() )
        return false;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream is( text );
    return Json::parseFromStream( b, is, &out, &errs );
}

std::string joinPath( const std::string &root, const char *rel )
{
    return ( std::filesystem::path( root ) / rel ).string();
}

/// All *.json files under `dir`, sorted. Empty when the directory is absent.
std::vector<std::string> collectDataJsonFiles( const std::string &dir )
{
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path root( dir );
    if ( !std::filesystem::exists( root, ec ) )
        return out;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied,
        ec );
    std::filesystem::recursive_directory_iterator end;
    while ( !ec && it != end )
    {
        std::error_code fileEc;
        if ( it->is_regular_file( fileEc ) && it->path().extension() == ".json" )
            out.push_back( it->path().string() );
        it.increment( ec );
    }
    std::sort( out.begin(), out.end() );
    return out;
}

} // namespace

AssemblyResult buildLiveGraph( const std::string &sourceRoot )
{
    AssemblyResult result;
    ContractGraph &g = result.graph;

    // ── operators (live registry + scanner facts) ────────────────────────
    OperatorParamScanner scanner( sourceRoot );
    const auto operatorScans = scanner.scanAll();
    for ( const auto &op : operatorScans )
    {
        ContractNode node;
        node.id = op.operatorId;
        node.kind = "operator";
        node.origin = op.file;
        node.attributes = std::vector<std::string>( op.declaredParams.begin(),
                                                    op.declaredParams.end() );
        g.addNode( std::move( node ) );
    }

    // ── commands ─────────────────────────────────────────────────────────
    CommandRefScanner cmdScanner;
    CommandRefReport refs;
    const std::string commandDefs =
        joinPath( sourceRoot, "src/app/workbench/command_defs.cpp" );
    const std::string workbenchWindow =
        joinPath( sourceRoot, "src/app/main_window_workbench.cpp" );
    cmdScanner.scanRegistered( readFile( commandDefs ), "command_defs.cpp",
                               refs );
    cmdScanner.scanRegistered( readFile( workbenchWindow ),
                               "main_window_workbench.cpp", refs );
    for ( const auto &id : refs.registeredIds )
    {
        ContractNode node;
        node.id = id;
        node.kind = "command";
        node.origin = refs.evidence[id];
        g.addNode( std::move( node ) );
    }

    // Consumer reference edges.
    for ( const auto &f : collectCppFiles( joinPath( sourceRoot, "src/app" ) ) )
    {
        const std::string src = readFile( f );
        cmdScanner.scanLookups( src, f, refs );
        cmdScanner.scanCtas( src, f, refs );
    }
    const std::string preflightFile =
        joinPath( sourceRoot, "src/agent/harness/scientific_preflight.cpp" );
    cmdScanner.scanPreflightActions( readFile( preflightFile ),
                                     "scientific_preflight.cpp", refs );
    for ( const auto &id : refs.lookupIds )
        g.addEdge( { "surface_lookup", id, id, refs.evidence[id] } );
    for ( const auto &id : refs.ctaCommandIds )
        g.addEdge( { "empty_state_cta", id, id, refs.evidence[id] } );
    for ( const auto &id : refs.preflightActionIds )
        g.addEdge( { "preflight_action", id, id, refs.evidence[id] } );

    // ── help topics ──────────────────────────────────────────────────────
    const std::string helpDir = joinPath( sourceRoot, "data/help" );
    for ( const auto &file : collectDataJsonFiles( helpDir ) )
    {
        Json::Value doc;
        if ( !readJson( file, doc ) || !doc.isArray() )
        {
            result.notes.push_back( "unreadable help file: " + file );
            continue;
        }
        for ( const auto &e : doc )
        {
            const std::string id = e.get( "id", "" ).asString();
            if ( id.empty() )
                continue;
            ContractNode node;
            node.id = id;
            node.kind = "help_topic";
            node.origin = file;
            g.addNode( std::move( node ) );
            if ( id.rfind( "command.", 0 ) == 0 )
                g.addEdge( { "command_help", id.substr( 8 ), id, file } );
        }
    }

    // ── error codes + diagnostics pages ─────────────────────────────────
    ErrorCodeScanner errScanner;
    ErrorCodeReport errReport;
    errScanner.scanEnum(
        readFile( joinPath( sourceRoot,
                            "src/operators/framework/rs_operator_error.h" ) ),
        "ErrorCode", errReport );
    errScanner.scanToStringSwitch(
        readFile( joinPath( sourceRoot,
                            "src/operators/framework/rs_operator_error.cpp" ) ),
        "ErrorCode", errReport );
    errScanner.scanHarnessCodes(
        readFile( joinPath( sourceRoot,
                            "src/agent/harness/harness_error.h" ) ),
        errReport );
    for ( const auto &[name, str] : errReport.caseMap )
    {
        if ( name == "Success" )
            continue;
        ContractNode node;
        node.id = "operator/" + str;
        node.kind = "error_code";
        node.origin = "rs_operator_error.h";
        g.addNode( std::move( node ) );
    }
    for ( const auto &[var, code] : errReport.harnessCodes )
    {
        ContractNode node;
        node.id = "harness/" + code;
        node.kind = "error_code";
        node.origin = "harness_error.h";
        g.addNode( std::move( node ) );
    }

    Json::Value diagDoc;
    if ( readJson( joinPath( sourceRoot, "data/help/diagnostics.json" ),
                   diagDoc ) &&
         diagDoc.isArray() )
    {
        for ( const auto &e : diagDoc )
        {
            const std::string id = e.get( "id", "" ).asString();
            const std::string family = e.get( "family", "" ).asString();
            const std::string code = e.get( "code", "" ).asString();
            ContractNode node;
            node.id = id;
            node.kind = "diagnostic";
            node.origin = "data/help/diagnostics.json";
            g.addNode( std::move( node ) );
            if ( family == "harness" || family == "operator" )
                g.addEdge( { "diagnostic_for", id, family + "/" + code,
                             "diagnostics.json" } );
        }
    }
    else
    {
        result.notes.push_back( "diagnostics.json unreadable" );
    }

    // ── capability knowledge ─────────────────────────────────────────────
    const std::string capsDir = joinPath( sourceRoot, "data/agent/capabilities" );
    for ( const auto &file : collectDataJsonFiles( capsDir ) )
    {
        Json::Value doc;
        if ( !readJson( file, doc ) || !doc.isArray() )
        {
            result.notes.push_back( "unreadable capability file: " + file );
            continue;
        }
        for ( const auto &e : doc )
        {
            const std::string id = e.get( "id", "" ).asString();
            if ( id.empty() )
                continue;
            ContractNode node;
            node.id = id;
            node.kind = "capability_entry";
            node.origin = file;
            g.addNode( std::move( node ) );
            if ( id.rfind( "family:", 0 ) != 0 )
                g.addEdge( { "capability_for", id, id, file } );
        }
    }

    return result;
}

} // namespace sicnu::contracts
