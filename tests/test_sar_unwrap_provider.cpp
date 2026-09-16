// tests/test_sar_unwrap_provider.cpp — external unwrap provider adapter
// (Advanced InSAR 11.0, package D).
//
// The oracle is the FAKE PROVIDER ITSELF (tests/support/
// sar_fake_unwrap_provider.cpp): a deterministic stand-in whose modes
// exercise every branch of the process contract — success, crash,
// truncation, NaN holes, hang (timeout), cancellation — plus the typed
// refusals for missing binaries and malformed templates. Every failure
// mode must leave NO scratch behind (QTemporaryDir RAII in the adapter)
// and never fall back to the built-in unwrapper.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_unwrap_provider.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

#ifndef FAKE_PROVIDER_PATH
#define FAKE_PROVIDER_PATH "missing-fake-provider"
#endif

using namespace sicnu::sar;

namespace
{
constexpr int kW = 8;
constexpr int kH = 6;

std::vector<double> makeWrappedPlane()
{
    std::vector<double> plane( static_cast<size_t>( kW ) * kH );
    for ( size_t i = 0; i < plane.size(); ++i )
        plane[i] = -M_PI + ( 2.0 * M_PI * static_cast<double>( i ) )
                             / static_cast<double>( plane.size() );
    return plane;
}

UnwrapProviderRequest makeRequest( const std::vector<double> &plane,
                                   const QString &workDir,
                                   const std::vector<QString> &argsTemplate = {} )
{
    UnwrapProviderRequest request;
    request.providerName = QStringLiteral( "fakeunwrap" );
    request.binPath = QStringLiteral( FAKE_PROVIDER_PATH );
    request.argsTemplate = argsTemplate.empty()
                               ? std::vector<QString>{ QStringLiteral( "{input}" ),
                                                       QStringLiteral( "{output}" ),
                                                       QStringLiteral( "{width}" ) }
                               : argsTemplate;
    request.wrapped = plane.data();
    request.w = kW;
    request.h = kH;
    request.timeoutMs = 20000;
    request.workDir = workDir;
    return request;
}
} // namespace

// QCoreApplication for QProcess/QTemporaryDir (single instance).
namespace
{
struct AppInit
{
    AppInit() { m_app = new QCoreApplication( m_argc, m_argv ); }
    int m_argc = 0;
    char *m_argv[1] = { nullptr };
    QCoreApplication *m_app = nullptr;
};
} // namespace

TEST_CASE( "External provider echoes a valid plane and re-masks NaN inputs",
           "[sar][unwrap-provider][insar11]" )
{
    AppInit init;
    QTemporaryDir work;
    REQUIRE( work.isValid() );

    auto plane = makeWrappedPlane();
    const size_t nanIndex = 13;
    plane[nanIndex] = std::numeric_limits<double>::quiet_NaN();

    UnwrapProviderResult result;
    QString error;
    const UnwrapProviderStatus status = runExternalUnwrapProvider(
        makeRequest( plane, work.path() ), &result, &error );
    REQUIRE( status == UnwrapProviderStatus::Ok );
    REQUIRE( error.isEmpty() );
    REQUIRE( result.unwrapped.size() == plane.size() );
    REQUIRE( result.validCount == static_cast<long>( plane.size() ) - 1 );
    // Identity "unwrap": every valid sample round-trips through float32.
    for ( size_t i = 0; i < plane.size(); ++i )
    {
        if ( i == nanIndex )
        {
            REQUIRE( std::isnan( result.unwrapped[i] ) );
            continue;
        }
        REQUIRE( result.unwrapped[i]
                 == Catch::Approx( plane[i] ).margin( 1e-6 ) );
    }
}

TEST_CASE( "Missing binaries and malformed requests are typed refusals",
           "[sar][unwrap-provider][insar11]" )
{
    AppInit init;
    QTemporaryDir work;
    REQUIRE( work.isValid() );
    const auto plane = makeWrappedPlane();

    UnwrapProviderResult result;
    QString error;

    // Explicitly absent binary: unavailable, never a builtin fallback.
    UnwrapProviderRequest request = makeRequest( plane, work.path() );
    request.binPath = QStringLiteral( "Z:/definitely/not/here/nowhere.exe" );
    REQUIRE( runExternalUnwrapProvider( request, &result, &error )
             == UnwrapProviderStatus::Unavailable );
    REQUIRE( error.contains( "UNWRAP_PROVIDER_UNAVAILABLE" ) );

    // Invalid provider name (path-like injection is refused).
    request = makeRequest( plane, work.path() );
    request.providerName = QStringLiteral( "../evil" );
    REQUIRE( runExternalUnwrapProvider( request, &result, &error )
             == UnwrapProviderStatus::Unavailable );
    REQUIRE( error.contains( "UNWRAP_PROVIDER_UNAVAILABLE" ) );

    // "builtin" is not an external provider name.
    REQUIRE_FALSE( isValidProviderName( QStringLiteral( "builtin" ) ) );
    REQUIRE_FALSE( isValidProviderName( QString() ) );
    REQUIRE( isValidProviderName( QStringLiteral( "snaphu-v2" ) ) );

    // Template that never references {output} cannot address the data.
    request = makeRequest( plane, work.path(),
                           { QStringLiteral( "{input}" ), QStringLiteral( "{width}" ) } );
    REQUIRE( runExternalUnwrapProvider( request, &result, &error )
             == UnwrapProviderStatus::Failed );
    REQUIRE( error.contains( "UNWRAP_PROVIDER_FAILED" ) );
}

TEST_CASE( "Provider failures crash/truncate/NaN leave typed errors and no "
           "half-products", "[sar][unwrap-provider][insar11]" )
{
    AppInit init;
    QTemporaryDir work;
    REQUIRE( work.isValid() );
    const auto plane = makeWrappedPlane();

    UnwrapProviderResult result;
    QString error;

    // Crash (exit code 3) → FAILED, empty result.
    qputenv( "FAKE_UNWRAP_MODE", "crash" );
    REQUIRE( runExternalUnwrapProvider( makeRequest( plane, work.path() ), &result, &error )
             == UnwrapProviderStatus::Failed );
    REQUIRE( error.contains( "UNWRAP_PROVIDER_FAILED" ) );
    REQUIRE( result.unwrapped.empty() );

    // Truncated output → INVALID_OUTPUT.
    qputenv( "FAKE_UNWRAP_MODE", "truncated" );
    REQUIRE( runExternalUnwrapProvider( makeRequest( plane, work.path() ), &result, &error )
             == UnwrapProviderStatus::InvalidOutput );
    REQUIRE( error.contains( "UNWRAP_PROVIDER_INVALID_OUTPUT" ) );

    // NaN holes in an unmasked region → INVALID_OUTPUT (the tool did not
    // solve the field; the adapter refuses instead of papering over it).
    qputenv( "FAKE_UNWRAP_MODE", "nan" );
    REQUIRE( runExternalUnwrapProvider( makeRequest( plane, work.path() ), &result, &error )
             == UnwrapProviderStatus::InvalidOutput );
    qunsetenv( "FAKE_UNWRAP_MODE" );
}

TEST_CASE( "Provider timeout and cancellation kill the process cleanly",
           "[sar][unwrap-provider][insar11]" )
{
    AppInit init;
    QTemporaryDir work;
    REQUIRE( work.isValid() );
    const auto plane = makeWrappedPlane();

    UnwrapProviderResult result;
    QString error;

    UnwrapProviderRequest request = makeRequest( plane, work.path() );
    request.timeoutMs = 800;
    qputenv( "FAKE_UNWRAP_MODE", "hang" );
    const UnwrapProviderStatus status = runExternalUnwrapProvider( request, &result, &error );
    qunsetenv( "FAKE_UNWRAP_MODE" );
    REQUIRE( status == UnwrapProviderStatus::Timeout );
    REQUIRE( error.contains( "UNWRAP_PROVIDER_TIMEOUT" ) );

    // Caller cancellation wins before the deadline.
    request.timeoutMs = 20000;
    request.cancelQuery = [] { return true; };
    qputenv( "FAKE_UNWRAP_MODE", "hang" );
    REQUIRE( runExternalUnwrapProvider( request, &result, &error )
             == UnwrapProviderStatus::Cancelled );
    qunsetenv( "FAKE_UNWRAP_MODE" );
    REQUIRE( error.contains( "CANCELLED" ) );
    REQUIRE( result.unwrapped.empty() );
}
