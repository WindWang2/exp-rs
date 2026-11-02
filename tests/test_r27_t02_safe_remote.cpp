// test_r27_t02_safe_remote.cpp — R27-T2 / #1765 (RIO-4) + #1803 (RIO-6):
// the safe remote open authority refuses every unguarded path and maps
// legal https COGs onto the guarded range-cache transport.
//
// No test here touches the network: refusals are judged from URL shape and
// process flags alone (that is the point — an unauthorized target must be
// refused BEFORE any socket could open), and the Ok case asserts the path
// mapping, not a fetch.
#include <catch2/catch_test_macros.hpp>

#include "data/providers/remote_source_cache.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/remote/safe_remote_open.h"

using namespace sicnu::geo::remote;
using sicnu::geo::RemoteRangeCache;

namespace
{

/// Ensures the cache is NOT installed for refusal cases (fail-closed paths
/// must not depend on global state another test left behind).
struct CacheAbsentGuard
{
    CacheAbsentGuard() { RemoteRangeCache::uninstall(); }
    ~CacheAbsentGuard() { RemoteRangeCache::uninstall(); }
};

} // namespace

TEST_CASE( "#1765 safe open: scheme gate (http/https only, vsicurl unwrapped)",
           "[remote][r27t02][safe_open]" )
{
    CacheAbsentGuard noCache;
    // Local file spellings are not remote at all — refused for THIS api
    // (callers open local paths directly).
    REQUIRE( safeRemoteRasterPath( "/data/local scene.tif" ).status
             == SafeOpenStatus::SchemeRefused );
    REQUIRE( safeRemoteRasterPath( "ftp://example.com/a.tif" ).status
             == SafeOpenStatus::SchemeRefused );
    // The /vsicurl/ spelling unwraps to its https URL (refusals below prove
    // the unwrap reached the URL, not the vsicurl prefix).
    const auto wrapped = safeRemoteRasterPath( "/vsicurl/https://127.0.0.1/a.tif" );
    REQUIRE( wrapped.status == SafeOpenStatus::PrivateTarget );
}

TEST_CASE( "#1765 safe open: private/loopback/cloud-metadata targets are denied by default",
           "[remote][r27t02][safe_open]" )
{
    CacheAbsentGuard noCache;
    const char *targets[] = {
        "https://127.0.0.1/a.tif",             // loopback literal
        "http://localhost/a.tif",              // loopback name
        "https://[::1]/a.tif",                 // IPv6 loopback
        "https://[::ffff:127.0.0.1]/a.tif",    // IPv4-mapped IPv6
        "https://10.1.2.3/a.tif",              // RFC 1918
        "https://192.168.5.5/a.tif",           // RFC 1918
        "https://169.254.169.254/latest/meta-data/", // cloud metadata
        "http://metadata.google.internal/a",   // cloud metadata name
        "https://100.64.0.1/a.tif",            // CGNAT
        "https://fe80::1/a.tif",               // link-local
        "/vsicurl/http://127.0.0.1:8080/x.tif" // legacy wrapped spelling
    };
    for ( const char *t : targets )
    {
        const auto r = safeRemoteRasterPath( t );
        INFO( "target: " << t << " -> " << safeOpenStatusName( r.status ) );
        REQUIRE( r.status == SafeOpenStatus::PrivateTarget );
        REQUIRE( r.path.empty() );
        REQUIRE( r.message.find( t ) != std::string::npos ); // actionable
    }
}

TEST_CASE( "#1765 safe open: offline mode refuses before any transport",
           "[remote][r27t02][safe_open]" )
{
    CacheAbsentGuard noCache;
    sicnu::geo::offline::setEnabled( true );
    struct OffGuard
    {
        ~OffGuard() { sicnu::geo::offline::setEnabled( false ); }
    } offGuard;
    const auto r = safeRemoteRasterPath( "https://example.com/public.tif" );
    REQUIRE( r.status == SafeOpenStatus::OfflineRefused );
    REQUIRE( r.path.empty() );
}

TEST_CASE( "#1765 safe open: without the guarded transport the open fails closed",
           "[remote][r27t02][safe_open]" )
{
    CacheAbsentGuard noCache;
    const auto r = safeRemoteRasterPath( "https://example.com/public.tif" );
    // A public https URL with NO range cache installed must NOT degrade to
    // an unguarded /vsicurl/ open — the exact bypass this API closes.
    REQUIRE( r.status == SafeOpenStatus::CacheUnavailable );
    REQUIRE( r.path.empty() );
}

TEST_CASE( "#1765 safe open: legal https maps onto the guarded range-cache path",
           "[remote][r27t02][safe_open]" )
{
    RemoteRangeCache::uninstall();
    RemoteRangeCache::install( {} );
    struct CacheGuard
    {
        ~CacheGuard() { RemoteRangeCache::uninstall(); }
    } cacheGuard;

    const auto r = safeRemoteRasterPath( "https://example.com/public.tif" );
    REQUIRE( r.status == SafeOpenStatus::Ok );
    REQUIRE( RemoteRangeCache::isCachePath( r.path ) );
    REQUIRE( r.path.find( "example.com/public.tif" ) != std::string::npos );

    // The /vsicurl/ legacy spelling maps to the SAME guarded path.
    const auto wrapped = safeRemoteRasterPath( "/vsicurl/https://example.com/public.tif" );
    REQUIRE( wrapped.status == SafeOpenStatus::Ok );
    REQUIRE( wrapped.path == r.path );
}

TEST_CASE( "#1803 RIO-6 RemoteDatasetPool::acquire routes remote URLs through the gate",
           "[remote][r27t02][pool]" )
{
    // Private target: refused with an empty lease, zero network attempts.
    REQUIRE_FALSE( static_cast<bool>( sicnu::data::RemoteDatasetPool::instance().acquire(
        "/vsicurl/https://127.0.0.1:9/never.tif", GDAL_OF_RASTER ) ) );
    REQUIRE_FALSE( static_cast<bool>( sicnu::data::RemoteDatasetPool::instance().acquire(
        "/vsicurl/https://169.254.169.254/never.tif", GDAL_OF_RASTER ) ) );
    // No guarded transport installed (whatever earlier cases left): public
    // remote URL also fails closed now instead of a raw /vsicurl/ open.
    RemoteRangeCache::uninstall();
    REQUIRE_FALSE( static_cast<bool>( sicnu::data::RemoteDatasetPool::instance().acquire(
        "/vsicurl/https://example.com/never.tif", GDAL_OF_RASTER ) ) );
}
