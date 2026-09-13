/***************************************************************************
  tests/test_offline_mode.cpp — Offline classroom deployment (goal D7):
  the process-wide offline gate refuses remote requests with a typed error,
  never dispatches a packet, and the GDAL cloud-/vsi* deny makes direct
  /vsicurl/ opens fail fast. Local /vsi handlers stay usable. No server, no
  sockets — the whole point is that nothing is attempted.
 ***************************************************************************/

#include "data/providers/remote_source_cache.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/offline_gate.h"

#include <QByteArray>
#include <QtGlobal>

#include <catch2/catch_test_macros.hpp>

#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>

#include <optional>
#include <tuple>
#include <utility>

using namespace sicnu::geo;

namespace
{
/// RAII: engage the gate for the scope, always restore process state.
struct OfflineGuard
{
    OfflineGuard() { offline::setEnabled( true ); offline::applyGdalNetworkDeny(); }
    ~OfflineGuard() { offline::clearGdalNetworkDeny(); offline::setEnabled( false ); }
};

/// RAII env save/restore with the repo's cross-platform pattern (qputenv —
/// see test_mcp_server.cpp; setenv/unsetenv do not exist on MSVC).
struct RestoreEnv
{
    explicit RestoreEnv( const char *name ) : m_name( name )
    {
        m_saved = qgetenv( name );
        m_hadValue = !m_saved.isEmpty();
    }
    ~RestoreEnv()
    {
        if ( m_hadValue ) qputenv( m_name.constData(), m_saved );
        else qunsetenv( m_name.constData() );
    }
    QByteArray m_name;
    QByteArray m_saved;
    bool m_hadValue = false;
};
} // namespace

TEST_CASE( "offline gate: remote classification", "[offline][d7]" )
{
    using sicnu::geo::offline::isRemoteTarget;

    CHECK( isRemoteTarget( "https://example.com/scene.tif" ) );
    CHECK( isRemoteTarget( "HTTPS://example.com/scene.tif" ) );
    CHECK( isRemoteTarget( "http://example.com/stac/search" ) );
    CHECK( isRemoteTarget( "/vsicurl/https://example.com/scene.tif" ) );
    CHECK( isRemoteTarget( "/vsis3/bucket/key.tif" ) );
    CHECK( isRemoteTarget( "/vsigs/bucket/key.tif" ) );
    CHECK( isRemoteTarget( "/vsiaz/container/key.tif" ) );

    // Local sources — including the local /vsi handlers — stay usable.
    CHECK( isRemoteTarget( "data/samples/landsat_sample.tif" ) == false );
    CHECK( isRemoteTarget( "/vsimem/in_memory.tif" ) == false );
    CHECK( isRemoteTarget( "/vsizip/archive.zip" ) == false );
    CHECK( isRemoteTarget( "/vsitar/archive.tar" ) == false );
    CHECK( isRemoteTarget( "" ) == false );

    // The range cache wraps other sources: remote exactly when the wrapped
    // target is remote.
    CHECK( isRemoteTarget( "/vsirangecache/https://example.com/scene.tif" ) );
    CHECK( isRemoteTarget( "/vsirangecache//vsis3/bucket/key.tif" ) );
    CHECK( isRemoteTarget( "/vsirangecache/data/local/scene.tif" ) == false );
}

TEST_CASE( "offline gate: SICNU_OFFLINE env semantics", "[offline][d7]" )
{
    RestoreEnv env( "SICNU_OFFLINE" );
    qputenv( "SICNU_OFFLINE", "1" );
    CHECK( sicnu::geo::offline::enabledFromEnv() );
    qputenv( "SICNU_OFFLINE", "true" );
    CHECK( sicnu::geo::offline::enabledFromEnv() );
    qputenv( "SICNU_OFFLINE", "ON" );
    CHECK( sicnu::geo::offline::enabledFromEnv() );
    qputenv( "SICNU_OFFLINE", "0" );
    CHECK( sicnu::geo::offline::enabledFromEnv() == false );
    qunsetenv( "SICNU_OFFLINE" );
    CHECK( sicnu::geo::offline::enabledFromEnv() == false );
}

TEST_CASE( "offline gate: httpFetch refuses with a typed error, zero dispatch",
           "[offline][d7]" )
{
    OfflineGuard guard;

    // The refusal fires before any transport work: this host cannot resolve
    // network names, so a dispatch attempt would surface as a DNS-flavored
    // network error, not the offline refusal text.
    try
    {
        HttpFetchOptions options;
        options.timeoutSeconds = 1;
        options.connectTimeoutSeconds = 1;
        options.maxRetries = 0;
        std::ignore = httpFetch( "https://offline-d7.invalid/stac/search", options );
        FAIL( "httpFetch must throw in offline mode" );
    }
    catch ( const GeoError &e )
    {
        CHECK( e.code() == ErrorCode::NetworkError );
        const std::string message = e.what();
        CHECK( message.find( "offline mode" ) != std::string::npos );
        CHECK( message.find( "SICNU_OFFLINE" ) != std::string::npos );
        CHECK( message.find( "offline-d7.invalid" ) != std::string::npos );
    }

    // The raw-status variant (used by revalidation) refuses identically.
    try
    {
        std::ignore = httpFetchStatus( "https://offline-d7.invalid/scene.tif" );
        FAIL( "httpFetchStatus must throw in offline mode" );
    }
    catch ( const GeoError &e )
    {
        CHECK( std::string( e.what() ).find( "offline mode" ) != std::string::npos );
    }
}

TEST_CASE( "offline gate: GDAL cloud-source deny fails fast without network",
           "[offline][d7]" )
{
    OfflineGuard guard;

    GDALAllRegister();
    CPLPushErrorHandler( CPLQuietErrorHandler );
    const GDALDatasetH dataset =
      GDALOpen( "/vsicurl/https://offline-d7.invalid/scene.tif", GA_ReadOnly );
    CPLPopErrorHandler();

    // Deny = the source "does not exist" (fast, no I/O) — never a timeout.
    CHECK( dataset == nullptr );
}

TEST_CASE( "offline gate: remote pool refuses with an empty lease, zero I/O",
           "[offline][d7]" )
{
    OfflineGuard guard;

    // The pool's acquire would otherwise GDALOpenEx the /vsicurl/ target; the
    // gate must return the empty lease before any transport is attempted (the
    // host name is unresolvable here, so a dispatch attempt would hang in DNS
    // retries instead of returning immediately).
    auto lease = sicnu::data::RemoteDatasetPool::instance().acquire(
      "/vsicurl/https://offline-d7.invalid/scene.tif", GDAL_OF_RASTER );
    CHECK( static_cast<bool>( lease ) == false );
}

TEST_CASE( "offline gate: gate off by default, clear restores", "[offline][d7]" )
{
    RestoreEnv env( "SICNU_OFFLINE" );
    qunsetenv( "SICNU_OFFLINE" );
    CHECK( sicnu::geo::offline::enabled() == false );

    sicnu::geo::offline::applyGdalNetworkDeny();
    sicnu::geo::offline::clearGdalNetworkDeny();
    // After clearing, the config option is unset again — a local open path
    // cannot observe the deny.
    CHECK( CPLGetConfigOption( "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", nullptr ) == nullptr );
}
