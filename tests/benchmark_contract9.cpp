// benchmark_contract9.cpp — contract generation cost baselines (M8,
// Contract Platform 9.0). Run manually:
//
//   benchmark_contract9 --out benchmarks/contract9.json
//
// Measures the tooling-side cost of the contract platform (never on a
// product hot path):
//   * operator_param_scan   — full source scan of src/operators/**: the
//                             implementation↔schema projection guard cost
//   * graph_assembly        — full contract graph assembly from all surfaces
//   * graph_serialize       — canonical exp.contract.graph.v1 serialization
//   * descriptor_projection — per-operator canonical descriptor projection
//
// Evidence only: wall-clock numbers are regression EVIDENCE (JSON artifact
// with environment headers), never a hard gate. Bounds: the whole run is
// O(source bytes) with hard caps inherited from the scanner (2 MB/file).

#include "contracts/contract_descriptor.h"
#include "contracts/contract_graph.h"
#include "contracts/graph_assembly.h"
#include "contracts/operator_param_scanner.h"

#include <operators/framework/rs_operator.h>
#include <operators/framework/rs_operator_registry.h>
#include <operators/rs/rs_operators_init.h>

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::contracts;

namespace {

struct Timing
{
    std::string name;
    double ms = 0.0;
    std::string detail;
};

double elapsedMs( const std::chrono::steady_clock::time_point &start )
{
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>( end - start ).count();
}

} // namespace

int main( int argc, char **argv )
{
    std::string outPath;
    for ( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if ( arg == "--out" && i + 1 < argc )
            outPath = argv[++i];
    }

    std::vector<Timing> timings;

    // Registry warm-up (excluded from timings): installs every family.
    sicnu::operators::rs::initBuiltinRsOperators();
    sicnu::operators::RSOperatorRegistry::instance();
    const auto operatorNames =
        sicnu::operators::RSOperatorRegistry::instance().operatorNames();

    // 1. operator param scan (source extraction).
    OperatorParamScanner scanner( CMAKE_SOURCE_DIR );
    auto t0 = std::chrono::steady_clock::now();
    const auto scans = scanner.scanAll();
    timings.push_back(
        { "operator_param_scan", elapsedMs( t0 ),
          "operators=" + std::to_string( scans.size() ) } );

    // 2. descriptor projection per operator.
    t0 = std::chrono::steady_clock::now();
    std::size_t paramsProjected = 0;
    int projectionFailures = 0;
    for ( const auto &name : operatorNames )
    {
        auto op =
            sicnu::operators::RSOperatorRegistry::instance().create( name );
        if ( !op )
            continue;
        ContractDescriptor d;
        std::string error;
        if ( ContractDescriptor::fromOperatorSchema( name, op->schema(), d,
                                                     error ) )
            paramsProjected += d.params.size();
        else
            ++projectionFailures;
    }
    timings.push_back(
        { "descriptor_projection", elapsedMs( t0 ),
          "operators=" + std::to_string( operatorNames.size() ) +
              " params=" + std::to_string( paramsProjected ) +
              " failures=" + std::to_string( projectionFailures ) } );

    // 3. graph assembly.
    t0 = std::chrono::steady_clock::now();
    const auto assembled = buildLiveGraph( CMAKE_SOURCE_DIR );
    timings.push_back(
        { "graph_assembly", elapsedMs( t0 ),
          "nodes=" + std::to_string( assembled.graph.nodes().size() ) +
              " edges=" + std::to_string( assembled.graph.edges().size() ) } );

    // 4. canonical serialization.
    t0 = std::chrono::steady_clock::now();
    const Json::Value doc = assembled.graph.toJson();
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    const std::string serialized = Json::writeString( wb, doc );
    timings.push_back(
        { "graph_serialize", elapsedMs( t0 ),
          "bytes=" + std::to_string( serialized.size() ) } );

    if ( outPath.empty() )
    {
        for ( const auto &t : timings )
            std::printf( "%-24s %10.1f ms  %s\n", t.name.c_str(), t.ms,
                         t.detail.c_str() );
        return 0;
    }

    std::ofstream out( outPath, std::ios::binary );
    if ( !out.good() )
    {
        std::fprintf( stderr, "cannot write %s\n", outPath.c_str() );
        return 2;
    }
    out << "{\n";
    out << "  \"schema\": \"exp.bench.contract9.v1\",\n";
    out << "  \"os\": \""
#if defined( _WIN32 )
        "windows"
#elif defined( __APPLE__ )
        "macos"
#else
        "linux"
#endif
        << "\",\n";
    out << "  \"cpu_cores\": " << std::max( 1u, std::thread::hardware_concurrency() ) << ",\n";
    out << "  \"build\": \"release\",\n";
    out << "  \"note\": \"wall-clock evidence only — not a gate; tooling-side cost\",\n";
    out << "  \"measurements\": [\n";
    for ( std::size_t i = 0; i < timings.size(); ++i )
    {
        const Timing &t = timings[i];
        std::string detail = t.detail;
        std::replace( detail.begin(), detail.end(), '"', '\'' );
        out << "    { \"name\": \"" << t.name << "\", \"ms\": " << t.ms
            << ", \"detail\": \"" << detail << "\" }"
            << ( i + 1 < timings.size() ? "," : "" ) << "\n";
    }
    out << "  ]\n}\n";
    std::printf( "contract9: %zu measurements -> %s\n", timings.size(),
                 outPath.c_str() );
    return 0;
}
