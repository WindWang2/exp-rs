// test_execution_telemetry_11.cpp — WP-F: per-chunk observability with hard
// bounds. Oracles: exact tile counters (independent count in the test), the
// arithmetic sampling bound tiles/rate + O(1), and the ring capacity.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/observability/execution_telemetry.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::runtime;
using observability::Counter;
using observability::EventKind;
using observability::ExecutionTelemetry;

namespace
{
struct TelemetryGuard
{
    ExecutionTelemetry &tel = ExecutionTelemetry::instance();
    TelemetryGuard() { tel.setEnabled( true ); }
    ~TelemetryGuard() { tel.setEnabled( false ); tel.clearEvents(); }
};

sicnu::runtime::chunk::TileSpec tinySpec( int index, int total )
{
    sicnu::runtime::chunk::TileSpec s;
    s.index = index;
    s.totalTiles = total;
    s.width = s.height = 1;
    s.bufferWidth = s.bufferHeight = 1;
    s.bands = 1;
    return s;
}
} // namespace

TEST_CASE( "Pipeline emits sampled per-chunk events within the arithmetic bound",
           "[execution][telemetry]" )
{
    TelemetryGuard guard;
    guard.tel.clearEvents();

    using namespace sicnu::runtime::chunk;
    const int kTiles = 200;
    const std::uint32_t kRate = 16;
    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 2;
    cfg.telemetrySampleRate = kRate;

    std::atomic<int> produced{ 0 };
    ChunkPipeline pipeline(
        [&]( TilePayload &out ) {
            if ( produced.fetch_add( 1 ) >= kTiles )
                return false;
            out = TilePayload{ tinySpec( produced.load() - 1, kTiles ),
                               std::make_shared<std::vector<float>>( 1, 1.0f ) };
            return true;
        },
        { []( TilePayload &&p ) { return std::move( p ); } },
        []( TilePayload && ) { return true; },
        cfg );

    const auto tilesBefore = guard.tel.counters()["tiles_processed"];
    pipeline.run();

    // Exact counter: every processed tile counted once, sampling never
    // applies to counters.
    REQUIRE( guard.tel.counters()["tiles_processed"] - tilesBefore
             == static_cast<std::uint64_t>( kTiles ) );

    // Sampled events: subject-tagged queue-wait / chunk-progress / stage
    // spans, each ≤ tiles/rate + 2 (first tile + multiples of rate).
    const auto events = guard.tel.events();
    auto countKind = [&]( EventKind kind, const std::string &subject ) {
        size_t n = 0;
        for ( const auto &e : events )
            if ( e.kind == kind && ( subject.empty() || e.subject == subject ) )
                ++n;
        return n;
    };
    const size_t bound = kTiles / kRate + 2;
    REQUIRE( countKind( EventKind::QueueWait, "chunk.consumer" ) <= bound );
    REQUIRE( countKind( EventKind::ChunkProgress, "chunk.tiles" ) <= bound );
    REQUIRE( countKind( EventKind::ExecutionEnd, "chunk.stage0" ) <= bound );
    REQUIRE( countKind( EventKind::ExecutionEnd, "chunk.producer" ) <= bound );
    REQUIRE( countKind( EventKind::ExecutionEnd, "chunk.stage0" ) >= 1 );
    REQUIRE( countKind( EventKind::QueueWait, "chunk.consumer" ) >= 1 );

    // Total events stay far below the ring capacity — telemetry can never
    // outgrow the pipeline's own bounded memory.
    REQUIRE( events.size() <= 4 * bound + 8 );
}

TEST_CASE( "Sample rate 0 disables events but keeps counters exact",
           "[execution][telemetry]" )
{
    TelemetryGuard guard;
    guard.tel.clearEvents();

    using namespace sicnu::runtime::chunk;
    const int kTiles = 50;
    ChunkPipeline::Config cfg;
    cfg.telemetrySampleRate = 0;

    std::atomic<int> produced{ 0 };
    ChunkPipeline pipeline(
        [&]( TilePayload &out ) {
            if ( produced.fetch_add( 1 ) >= kTiles )
                return false;
            out = TilePayload{ tinySpec( 0, kTiles ),
                               std::make_shared<std::vector<float>>( 1, 0.f ) };
            return true;
        },
        {},
        []( TilePayload && ) { return true; },
        cfg );

    const auto before = guard.tel.counters()["tiles_processed"];
    pipeline.run();
    REQUIRE( guard.tel.counters()["tiles_processed"] - before
             == static_cast<std::uint64_t>( kTiles ) );
    // No per-chunk events at all.
    for ( const auto &e : guard.tel.events() )
    {
        INFO( "unexpected event: " << observability::eventKindName( e.kind ) );
        REQUIRE( e.kind != EventKind::QueueWait );
        REQUIRE( e.kind != EventKind::ChunkProgress );
    }
}

TEST_CASE( "Ring stays bounded under flooding (capacity is a hard cap)",
           "[execution][telemetry]" )
{
    TelemetryGuard guard;
    guard.tel.clearEvents();

    observability::TelemetryEvent e;
    e.kind = EventKind::RssSample;
    e.subject = "flood";
    for ( int i = 0; i < 40'000; ++i )
        guard.tel.record( e );

    REQUIRE( guard.tel.events().size() <= ExecutionTelemetry::kEventCapacity );
}
