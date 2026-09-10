// ASan smoke: exercises the Qt-free verification core under /fsanitize=address.
#include "runtime/observability/fault_registry.h"
#include "runtime/observability/fault_point.h"
#include "runtime/observability/trace.h"
#include "runtime/observability/trace_id.h"
#include "runtime/observability/diagnostic_report.h"

#include <atomic>
#include <mutex>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::runtime::observability;

int main()
{
    // ids under concurrency
    std::vector<std::string> ids;
    std::mutex idMutex;
    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
        threads.emplace_back( [&] {
            for ( int i = 0; i < 200; ++i )
            {
                std::string id = trace::TraceIdGenerator::next();
                std::lock_guard<std::mutex> lock( idMutex );
                ids.push_back( std::move( id ) );
            }
        } );
    for ( auto &t : threads )
        t.join();

    // ring sink + events
    auto ring = std::make_shared<trace::RingTraceSink>( 128 );
    trace::Trace::install( ring );
    for ( int i = 0; i < 500; ++i )
    {
        trace::TraceEvent event;
        event.task = std::to_string( i );
        event.event = "asan-probe";
        trace::Trace::publish( event );
    }
    trace::Trace::install( nullptr );

    // file sink + rotation
    const auto dir = std::filesystem::temp_directory_path() / "sicnu-asan-smoke";
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
    trace::FileTraceSink::Options options;
    options.directory = dir.string();
    options.maxFileBytes = 4096;
    {
        trace::FileTraceSink sink( options );
        for ( int i = 0; i < 300; ++i )
        {
            trace::TraceEvent event;
            event.event = "asan-file";
            event.detail = std::string( 32, 'x' );
            sink.write( event );
        }
    }

    // fault registry arm/fire/disarm under threads
    fault::armFault( { "asan.seam", fault::Mode::NextN, 50, {} } );
    std::atomic<int> fired{ 0 };
    threads.clear();
    for ( int t = 0; t < 4; ++t )
        threads.emplace_back( [&] {
            for ( int i = 0; i < 100; ++i )
                if ( SICNU_FAULT_POINT( "asan.seam" ) )
                    ++fired;
        } );
    for ( auto &t : threads )
        t.join();
    if ( fired.load() != 50 )
    {
        std::printf( "FAULT REGISTRY BUDGET BROKEN: %d\n", fired.load() );
        return 1;
    }

    // diagnostic envelope
    diagnostics::DiagnosticReport report;
    report.code = "asan.smoke";
    report.recoverability = diagnostics::Recoverability::Transient;
    report.causeChain = { "a", "b" };
    if ( report.toJson().find( "exp.diag.v1" ) == std::string::npos )
        return 1;

    std::filesystem::remove_all( dir, ec );
    std::printf( "ASAN SMOKE OK: %zu ids, %zu ring events\n", ids.size(), ring->size() );
    return 0;
}
