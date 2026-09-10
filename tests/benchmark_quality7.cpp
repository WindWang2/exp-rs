// benchmark_quality7.cpp — repeatable micro-baselines for the quality
// platform (task H, Verification 7.0). Run manually:
//
//   benchmark_quality7 --out benchmarks/quality7.json
//
// Measures (median of 7 repeats, fixed iteration counts):
//   * trace_id_generate       — TraceIdGenerator::next()
//   * trace_ndjson_encode     — encodeNdjson() of a representative event
//   * trace_emit_disabled     — hot path with no sink installed (must be
//                               ~one atomic load; hard ceiling asserted in
//                               tests, measured here as evidence)
//   * trace_emit_ring         — emit into an installed ring sink
//   * fault_probe_disarmed    — SICNU_FAULT_POINT with nothing armed
//   * job_dispatch_roundtrip  — JobEngine submit→terminal for an instant job
//   * condition_validate      — MapSpec condition syntax validation
//   * condition_evaluate      — MapSpec condition evaluation
//   * resource_uri_parse      — ResourceUri::parse of a remote URL
//
// Evidence only: wall-clock here is for regression EVIDENCE (JSON files in
// benchmarks/ with environment headers), never a hard CI gate.
#include <catch2/catch_test_macros.hpp>

#include "runtime/observability/fault_point.h"
#include "runtime/observability/trace.h"
#include "runtime/observability/trace_id.h"

#include "agent/mapspec/mapspec_conditions.h"
#include "geospatial/util/resource_uri.h"
#include "job_engine.h"
#include "jobs/job_types.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>

using namespace sicnu::runtime::observability;
using namespace sicnu::runtime::observability::trace;
using namespace sicnu::runtime::observability::fault;

namespace
{
struct Measurement
{
    std::string name;
    double opsPerSecond;
    int iterations;
};

template <typename Fn>
double measureOpsPerSecond( int iterations, Fn &&fn )
{
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    for ( int i = 0; i < iterations; ++i )
        fn();
    const auto end = clock::now();
    const double seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>( end - start ).count();
    return seconds > 0 ? static_cast<double>( iterations ) / seconds : 0.0;
}

Measurement medianOf( const std::string &name, int repeats, int iterations,
                      std::function<double( int )> runner )
{
    std::vector<double> samples;
    for ( int r = 0; r < repeats; ++r )
        samples.push_back( runner( iterations ) );
    std::sort( samples.begin(), samples.end() );
    return { name, samples[samples.size() / 2], iterations };
}

std::string escapeForJson( const std::string &text )
{
    std::string out;
    for ( const char ch : text )
    {
        if ( ch == '"' || ch == '\\' )
            out += '\\';
        out += ch;
    }
    return out;
}
} // namespace

int main( int argc, char **argv )
{
    std::string outPath = "benchmarks/quality7.json";
    for ( int i = 1; i + 1 < argc; ++i )
        if ( std::strcmp( argv[i], "--out" ) == 0 )
            outPath = argv[i + 1];

    std::vector<Measurement> results;

    // --- trace: id generation -------------------------------------------
    results.push_back( medianOf( "trace_id_generate", 7, 200000, []( int n ) {
        return measureOpsPerSecond( n, [] {
            volatile const char *id = TraceIdGenerator::next().c_str(); // keep it honest
            ( void )id;
        } );
    } ) );

    // --- trace: NDJSON encode -------------------------------------------
    TraceEvent sample;
    sample.run = "run-bench";
    sample.task = "123";
    sample.job = "job-abc";
    sample.worker = "w1";
    sample.op = "rs:ndvi";
    sample.event = "execution_end";
    sample.phase = "end";
    sample.status = "ok";
    sample.detail = "benchmark representative event";
    sample.durationUs = 12345;
    results.push_back( medianOf( "trace_ndjson_encode", 7, 200000, [ & ]( int n ) {
        return measureOpsPerSecond( n, [ & ] { volatile size_t s = encodeNdjson( sample ).size(); ( void )s; } );
    } ) );

    // --- trace: emit disabled (hot path) ---------------------------------
    Trace::install( nullptr );
    results.push_back( medianOf( "trace_emit_disabled", 7, 500000, []( int n ) {
        return measureOpsPerSecond( n, [] {
            TraceEvent event;
            event.event = "submitted";
            emitEvent( event );
        } );
    } ) );

    // --- trace: emit into ring sink --------------------------------------
    {
        auto ring = std::make_shared<RingTraceSink>( 1024 );
        Trace::install( ring );
        results.push_back( medianOf( "trace_emit_ring", 7, 200000, []( int n ) {
            return measureOpsPerSecond( n, [] {
                TraceEvent event;
                event.task = "1";
                event.event = "progress";
                emitEvent( event );
            } );
        } ) );
        Trace::install( nullptr );
    }

    // --- fault probe disarmed --------------------------------------------
    disarmAllFaults();
    results.push_back( medianOf( "fault_probe_disarmed", 7, 500000, []( int n ) {
        return measureOpsPerSecond( n, [] {
            volatile bool fired = SICNU_FAULT_POINT( "benchmark.probe" );
            ( void )fired;
        } );
    } ) );

    // --- condition AST ----------------------------------------------------
    const std::string condition = "count > 0 and has( results.tiles ) and name != \"\"";
    Json::Value context{ Json::objectValue };
    context["count"] = 3;
    context["results"]["tiles"] = 1;
    context["name"] = "scene";
    results.push_back( medianOf( "condition_validate", 7, 100000, [ & ]( int n ) {
        return measureOpsPerSecond( n, [ & ] {
            std::vector<std::string> problems;
            volatile bool ok =
                sicnu::agent::mapspec::validateConditionSyntax( condition, &problems );
            ( void )ok;
        } );
    } ) );
    results.push_back( medianOf( "condition_evaluate", 7, 200000, [ & ]( int n ) {
        return measureOpsPerSecond( n, [ & ] {
            bool value = false;
            std::string error;
            volatile bool ok = sicnu::agent::mapspec::evaluateCondition(
                condition, context, &value, &error );
            ( void )ok;
        } );
    } ) );

    // --- ResourceUri parse -------------------------------------------------
    const std::string url = "https://user:key@example.com:8443/prefix/scene.tif?token=abc&bbox=1,2,3,4";
    results.push_back( medianOf( "resource_uri_parse", 7, 200000, [ & ]( int n ) {
        return measureOpsPerSecond( n, [ & ] {
            volatile auto kind = sicnu::geo::ResourceUri::parse( url ).kind;
            ( void )kind;
        } );
    } ) );

    // --- JobEngine dispatch roundtrip -------------------------------------
    {
        auto &engine = JobEngine::instance();
        engine.setMaxWorkers( 4 );
        engine.clearExecutors();
        const auto executor = []( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
            Json::Value result;
            result["ok"] = true;
            return result;
        };
        const int kDispatches = 2000;
        results.push_back( medianOf( "job_dispatch_roundtrip", 5, kDispatches, [ & ]( int n ) {
            std::vector<std::string> ids( static_cast<size_t>( n ) );
            using clock = std::chrono::steady_clock;
            const auto start = clock::now();
            for ( int i = 0; i < n; ++i )
            {
                ids[static_cast<size_t>( i )] = "bench-" + std::to_string( i );
                sicnu::jobs::JobRequest request;
                request.algorithmId = "callable:bench";
                engine.submitWithId( request, ids[static_cast<size_t>( i )], executor );
            }
            for ( const std::string &id : ids )
                engine.waitForJob( id, 30000 );
            const auto end = clock::now();
            const double seconds =
                std::chrono::duration_cast<std::chrono::duration<double>>( end - start ).count();
            return seconds > 0 ? static_cast<double>( n ) / seconds : 0.0;
        } ) );
        engine.shutdownForTests();
        engine.clearCompleted();
    }

    // --- emit JSON ----------------------------------------------------------
    std::ofstream out( outPath, std::ios::binary );
    if ( !out.good() )
    {
        std::fprintf( stderr, "cannot write %s\n", outPath.c_str() );
        return 2;
    }
    out << "{\n";
    out << "  \"schema\": \"exp.bench.quality7.v1\",\n";
#ifdef _WIN32
    out << "  \"os\": \"windows\",\n";
#else
    out << "  \"os\": \"other\",\n";
#endif
    out << "  \"cpu_cores\": " << std::thread::hardware_concurrency() << ",\n";
#ifdef NDEBUG
    out << "  \"build\": \"release\",\n";
#else
    out << "  \"build\": \"debug\",\n";
#endif
    out << "  \"note\": \"wall-clock evidence only — not a CI gate\",\n";
    out << "  \"measurements\": [\n";
    for ( size_t i = 0; i < results.size(); ++i )
    {
        const Measurement &m = results[i];
        out << "    { \"name\": \"" << escapeForJson( m.name ) << "\", \"ops_per_s\": "
            << m.opsPerSecond << ", \"iterations\": " << m.iterations << " }"
            << ( i + 1 < results.size() ? "," : "" ) << "\n";
    }
    out << "  ]\n}\n";
    std::printf( "wrote %s (%zu measurements)\n", outPath.c_str(), results.size() );
    return 0;
}
