// test_r24_a_perf_viewshed.cpp — R24-A science track (WP-A6).
//
// Performance evidence harness, NOT a wall-clock gate: every assertion here
// is a correctness oracle; timings are printed to stdout only (median of 5
// runs, Mpix/s, peak RSS) and collected by the track's PERF_BASELINE.md
// before (pre-fix build) and after the viewshed ring-bucketing fix. The
// same binary also times the D8 chain with and without the flat-resolution
// pass, bounding the WP-A4 operator change's structural cost.

#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/terrain_hydrology.h"
#include "processing/algorithms/terrain_viewshed.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace
{

size_t peakRssKb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
        return pmc.PeakWorkingSetSize / 1024;
    return 0;
#else
    struct rusage ru;
    getrusage( RUSAGE_SELF, &ru );
    return static_cast<size_t>( ru.ru_maxrss );
#endif
}

double medianOf( std::vector<double> &v )
{
    for ( size_t i = 1; i < v.size(); ++i )
        for ( size_t j = i; j > 0 && v[j - 1] > v[j]; --j )
            std::swap( v[j - 1], v[j] );
    return v[v.size() / 2];
}

} // namespace

TEST_CASE( "viewshedR3 timing evidence (no gate)", "[processing][terrain][perf][r24_a]" )
{
    constexpr int kN = 1024;
    constexpr int kRuns = 5;
    std::vector<float> dem( static_cast<size_t>( kN ) * kN );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
        {
            const double dx = x - kN / 2.0, dy = y - kN / 2.0;
            const double r = std::hypot( dx, dy );
            dem[static_cast<size_t>( y ) * kN + x] =
                static_cast<float>( 100.0 + 40.0 * std::sin( r / 64.0 )
                                    + 8.0 * std::sin( x / 23.0 ) * std::cos( y / 31.0 ) );
        }

    TerrainVisibility::ViewshedParams p;
    p.obsCol = kN / 2.0;
    p.obsRow = kN / 2.0;
    std::vector<std::uint8_t> vis;

    // Warm-up + correctness anchor: the run must succeed every time.
    REQUIRE( TerrainVisibility::viewshedR3( dem.data(), kN, kN, -9999.0f,
                                            1.0, 1.0, p, &vis ) );
    REQUIRE( vis.size() == static_cast<size_t>( kN ) * kN );

    std::vector<double> ms( kRuns );
    for ( int i = 0; i < kRuns; ++i )
    {
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE( TerrainVisibility::viewshedR3( dem.data(), kN, kN, -9999.0f,
                                                1.0, 1.0, p, &vis ) );
        const auto t1 = std::chrono::steady_clock::now();
        ms[i] = std::chrono::duration<double, std::milli>( t1 - t0 ).count();
    }
    const double med = medianOf( ms );
    std::printf( "[PERF] viewshedR3 %dx%d cellSize=1: median %.1f ms over %d runs, "
                 "%.2f Mpix/s, peakRSS %zu kB\n",
                 kN, kN, med, kRuns,
                 static_cast<double>( kN ) * kN / ( med * 1000.0 ), peakRssKb() );
    SUCCEED( "timing recorded" );
}

TEST_CASE( "D8 chain timing with and without flat resolution (no gate)",
           "[processing][terrain][flow][perf][r24_a]" )
{
    constexpr int kN = 1024;
    constexpr int kRuns = 5;
    std::vector<float> dem( static_cast<size_t>( kN ) * kN );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
            dem[static_cast<size_t>( y ) * kN + x] =
                static_cast<float>( 0.01 * x + 0.005 * y
                                    + 2.0 * std::sin( x / 91.0 ) * std::cos( y / 87.0 ) );

    std::vector<float> filled( dem.size() ), resolved( dem.size() ), dir( dem.size() ),
        acc( dem.size() );

    auto runPlain = [&]() {
        REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), kN, kN,
                                               -9999.0f ) );
        REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), kN, kN,
                                              -9999.0f ) );
        REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), kN, kN ) );
    };
    auto runResolved = [&]() {
        REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), kN, kN,
                                               -9999.0f ) );
        REQUIRE( TerrainHydrology::resolveFlats( filled.data(), resolved.data(), kN, kN,
                                                 -9999.0f, nullptr, {} ) );
        REQUIRE( TerrainFlow::flowDirections( resolved.data(), dir.data(), kN, kN,
                                              -9999.0f ) );
        REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), kN, kN ) );
    };

    runPlain();  // warm-up
    std::vector<double> msPlain( kRuns ), msResolved( kRuns );
    for ( int i = 0; i < kRuns; ++i )
    {
        auto t0 = std::chrono::steady_clock::now();
        runPlain();
        auto t1 = std::chrono::steady_clock::now();
        runResolved();
        auto t2 = std::chrono::steady_clock::now();
        msPlain[i] = std::chrono::duration<double, std::milli>( t1 - t0 ).count();
        msResolved[i] = std::chrono::duration<double, std::milli>( t2 - t1 ).count();
    }
    std::printf( "[PERF] D8 fill+dir+acc %dx%d: plain median %.1f ms, +resolveFlats "
                 "median %.1f ms (+%.0f%%), peakRSS %zu kB\n",
                 kN, kN, medianOf( msPlain ), medianOf( msResolved ),
                 100.0 * ( medianOf( msResolved ) - medianOf( msPlain ) )
                     / medianOf( msPlain ),
                 peakRssKb() );
    SUCCEED( "timing recorded" );
}
