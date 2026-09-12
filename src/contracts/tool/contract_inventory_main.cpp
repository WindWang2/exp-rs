/***************************************************************************
 * contract_inventory_main.cpp — contract graph snapshot tool (M0/M10)
 *
 * Regenerates or verifies the committed contract snapshot:
 *
 *   contract_inventory --source-root <repo> --out <snapshot.json>
 *   contract_inventory --source-root <repo> --check <snapshot.json>
 *
 * `--check` byte-compares the snapshot against a fresh generation of the
 * live graph and exits non-zero with a summary when they differ (the
 * snapshot is stale — regenerate or consciously accept the contract
 * change). Regeneration is the documented remedy; silent acceptance is not
 * possible.
 ***************************************************************************/
#include "contracts/contract_graph.h"
#include "contracts/graph_assembly.h"
#include "contracts/text_scan.h"

#include <operators/framework/rs_operator_registry.h>
#include <operators/rs/rs_operators_init.h>

#include <json/json.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string canonicalJson( const Json::Value &root )
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "  ";
    std::string out = Json::writeString( b, root );
    out += "\n";
    return out;
}

std::string readFile( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    return std::string( std::istreambuf_iterator<char>( in ),
                        std::istreambuf_iterator<char>() );
}

} // namespace

int main( int argc, char **argv )
{
    std::string sourceRoot, outPath, checkPath;
    for ( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if ( arg == "--source-root" && i + 1 < argc )
            sourceRoot = argv[++i];
        else if ( arg == "--out" && i + 1 < argc )
            outPath = argv[++i];
        else if ( arg == "--check" && i + 1 < argc )
            checkPath = argv[++i];
        else
        {
            std::cerr << "usage: contract_inventory --source-root <repo> "
                         "(--out <file> | --check <file>)\n";
            return 2;
        }
    }
    if ( sourceRoot.empty() || ( outPath.empty() == checkPath.empty() ) )
    {
        std::cerr << "exactly one of --out / --check is required\n";
        return 2;
    }

    // Install every operator family before assembly.
    sicnu::operators::rs::initBuiltinRsOperators();
    sicnu::operators::RSOperatorRegistry::instance();

    const auto assembled = sicnu::contracts::buildLiveGraph( sourceRoot );
    for ( const auto &note : assembled.notes )
        std::cerr << "note: " << note << "\n";

    const auto findings = assembled.graph.computeFindings();
    for ( const auto &f : findings )
        std::cerr << "finding: " << f.kind << " " << f.id << " (" << f.detail
                  << ")\n";

    if ( !outPath.empty() )
    {
        std::ofstream out( outPath, std::ios::binary );
        if ( !out )
        {
            std::cerr << "cannot write " << outPath << "\n";
            return 2;
        }
        out << canonicalJson( assembled.graph.toJson() );
        std::cout << "nodes: " << assembled.graph.nodes().size()
                  << " edges: " << assembled.graph.edges().size()
                  << " findings: " << findings.size() << " -> " << outPath
                  << "\n";
        return findings.empty() ? 0 : 1;
    }

    // --check: byte-compare.
    const std::string expected = canonicalJson( assembled.graph.toJson() );
    const std::string actual = readFile( checkPath );
    if ( actual.empty() )
    {
        std::cerr << "snapshot missing or empty: " << checkPath << "\n";
        return 1;
    }
    if ( expected != actual )
    {
        std::cerr << "SNAPSHOT STALE: " << checkPath
                  << " differs from the live contract graph.\n"
                  << "Remedy: rebuild contract_inventory and run:\n"
                  << "  contract_inventory --source-root <repo> --out "
                  << checkPath << "\n"
                  << "then review the diff as a conscious contract update.\n";
        return 1;
    }
    std::cout << "snapshot up to date: nodes "
              << assembled.graph.nodes().size()
              << " edges " << assembled.graph.edges().size() << "\n";
    return findings.empty() ? 0 : 1;
}
