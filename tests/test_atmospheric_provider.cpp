// test_atmospheric_provider.cpp — radiometric-physics-11 work package C.
//
// Oracles: DOS1/DOS2 known answers are hand-computed Chavez algebra
// ((rho − dark + 0.01) / T) written directly in the test; the QUAC adapter
// is an integration check against the wrapped house kernel (its science is
// owned by test_atmospheric); a test-local stub provider verifies registry
// dispatch, requirement validation and lifetime. Negative tests pin the
// typed refusals: unknown ids name the registered fallbacks, missing
// auxiliary inputs are enumerated, single/multi-band mismatches refuse.

#include "processing/algorithms/atmospheric_correction.h"
#include "processing/algorithms/atmospheric_provider.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace AtmosphericProvider;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

/// Test-local provider with configurable requirements (records dispatch).
class StubProvider final : public SurfaceReflectanceProvider
{
  public:
    StubProvider( QString id, Requirements req )
        : m_id( std::move( id ) ), m_req( req )
    {
    }
    QString id() const override { return m_id; }
    QString displayName() const override { return QStringLiteral( "stub" ); }
    Requirements requirements() const override { return m_req; }
    bool toSurfaceReflectance( const float *toa, float *surface, size_t count,
                               const CorrectionInputs &, QString * ) const override
    {
        ++m_calls;
        for ( size_t i = 0; i < count; ++i )
            surface[i] = toa[i];
        return true;
    }
    mutable int m_calls = 0;

  private:
    QString m_id;
    Requirements m_req;
};

/// Local stand-in carrying a built-in id, for the duplicate-id test.
class Dos1IdShim final : public SurfaceReflectanceProvider
{
  public:
    QString id() const override { return QStringLiteral( "dos1" ); }
    QString displayName() const override { return QStringLiteral( "shim" ); }
    Requirements requirements() const override { return {}; }
};
} // namespace

TEST_CASE( "registry starts with built-ins, sorted, idempotent", "[provider]" )
{
    ensureBuiltinProviders();
    const QStringList ids = registeredIds();
    REQUIRE( ids.contains( QLatin1String( "dos1" ) ) );
    REQUIRE( ids.contains( QLatin1String( "dos2" ) ) );
    REQUIRE( ids.contains( QLatin1String( "quac" ) ) );
    CHECK( std::is_sorted( ids.begin(), ids.end() ) );

    // Idempotent: re-installing keeps a single entry per id.
    ensureBuiltinProviders();
    CHECK( registeredIds().count( QLatin1String( "dos1" ) ) == 1 );

    // Unknown / empty lookups are null (never a default provider).
    CHECK( provider( QStringLiteral( "6s" ) ) == nullptr );
    CHECK( provider( QString() ) == nullptr );

    // Null registration refused.
    CHECK_FALSE( registerProvider( nullptr ) );
}

TEST_CASE( "duplicate registration is refused, original kept", "[provider]" )
{
    ensureBuiltinProviders();
    Dos1IdShim shim;
    CHECK_FALSE( registerProvider( &shim ) );
    // The built-in remains in place, untouched by the refused shim.
    CHECK( provider( QStringLiteral( "dos1" ) )->displayName()
           == QStringLiteral( "DOS1 (Chavez dark-object subtraction)" ) );
}

TEST_CASE( "DOS1 known answer: (rho - dark + 0.01)", "[provider]" )
{
    CorrectionInputs aux;
    aux.toaDarkLevel = 0.10f;

    const std::vector<float> toa = { 0.20f, 0.50f, 0.10f, kNaN, -0.02f };
    std::vector<float> out( toa.size(), 0.0f );

    const SurfaceReflectanceProvider *p = nullptr;
    REQUIRE( resolve( QStringLiteral( "dos1" ), aux, &p ) );
    REQUIRE( p );
    REQUIRE( p->toSurfaceReflectance( toa.data(), out.data(), toa.size(), aux ) );

    // Hand-computed Chavez algebra, independent of any production call:
    // 0.20 → 0.11, 0.50 → 0.41, dark level → 0.01, NaN → NaN.
    // A negative result (ρ − dark + 0.01 < 0) passes through FINITE — the
    // house kernel does not clip; RadiometricQa::FlagNegative is the
    // mechanism that reports it downstream.
    CHECK( out[0] == Catch::Approx( 0.11f ).epsilon( 1e-6 ) );
    CHECK( out[1] == Catch::Approx( 0.41f ).epsilon( 1e-6 ) );
    CHECK( out[2] == Catch::Approx( 0.01f ).epsilon( 1e-6 ) );
    CHECK( std::isnan( out[3] ) );
    CHECK( out[4] == Catch::Approx( -0.11f ).epsilon( 1e-6 ) );
}

TEST_CASE( "DOS2 known answer: (rho - dark + 0.01) / T", "[provider]" )
{
    CorrectionInputs aux;
    aux.toaDarkLevel = 0.05f;
    aux.transmittance = 0.80f;

    const std::vector<float> toa = { 0.45f, 0.25f };
    std::vector<float> out( toa.size(), 0.0f );

    const SurfaceReflectanceProvider *p = nullptr;
    REQUIRE( resolve( QStringLiteral( "dos2" ), aux, &p ) );
    REQUIRE( p->toSurfaceReflectance( toa.data(), out.data(), toa.size(), aux ) );

    CHECK( out[0] == Catch::Approx( 0.5125f ).epsilon( 1e-6 ) ); // 0.41/0.8
    CHECK( out[1] == Catch::Approx( 0.2625f ).epsilon( 1e-6 ) ); // 0.21/0.8
}

TEST_CASE( "resolve refuses unknown providers by name, never falls back", "[provider]" )
{
    ensureBuiltinProviders();
    CorrectionInputs aux; // fully populated — refusal must be about the id
    aux.toaDarkLevel = 0.1f;
    aux.transmittance = 0.8f;

    const SurfaceReflectanceProvider *p = nullptr;
    QString err;
    CHECK_FALSE( resolve( QStringLiteral( "6s" ), aux, &p, &err ) );
    CHECK( p == nullptr );
    INFO( err.toStdString() );
    CHECK( err.contains( QLatin1String( "6s" ) ) );
    CHECK( err.contains( QLatin1String( "not registered" ) ) );
    CHECK( err.contains( QLatin1String( "dos1" ) ) ); // names the fallbacks
    CHECK_FALSE( resolve( QStringLiteral( "6s" ), aux, &p, nullptr ) ); // no sink, still false
}

TEST_CASE( "requirement validation enumerates every missing input", "[provider]" )
{
    ensureBuiltinProviders();
    CorrectionInputs empty; // nothing set
    const SurfaceReflectanceProvider *p = nullptr;

    QString err;
    CHECK_FALSE( resolve( QStringLiteral( "dos1" ), empty, &p, &err ) );
    CHECK( err.contains( QLatin1String( "dark-object" ) ) );

    CHECK_FALSE( resolve( QStringLiteral( "dos2" ), empty, &p, &err ) );
    CHECK( err.contains( QLatin1String( "dark-object" ) ) );
    CHECK( err.contains( QLatin1String( "transmittance" ) ) );

    // Transmittance outside (0, 1] is refused (would flip or blow up DOS2).
    CorrectionInputs badT;
    badT.toaDarkLevel = 0.1f;
    badT.transmittance = 1.5f;
    CHECK_FALSE( resolve( QStringLiteral( "dos2" ), badT, &p, &err ) );
    CHECK( err.contains( QLatin1String( "transmittance" ) ) );

    // Satisfied requirements pass through.
    CorrectionInputs good = badT;
    good.transmittance = 0.85f;
    CHECK( resolve( QStringLiteral( "dos2" ), good, &p ) );
}

TEST_CASE( "a custom provider plugs into the seam unmodified", "[provider]" )
{
    Requirements req;
    req.needsAod = true;
    req.needsSunGeometry = true;
    // Registry is non-owning: the stub must live for the whole process, so
    // it is a function-local static (the registry is never unregistered).
    static StubProvider stub( QStringLiteral( "test-lut-6s" ), req );
    REQUIRE( registerProvider( &stub ) );

    CorrectionInputs aux;
    const SurfaceReflectanceProvider *p = nullptr;
    QString err;
    CHECK_FALSE( resolve( QStringLiteral( "test-lut-6s" ), aux, &p, &err ) );
    CHECK( err.contains( QLatin1String( "optical depth" ) ) );
    CHECK( err.contains( QLatin1String( "sun geometry" ) ) );

    aux.aod550 = 0.25;
    aux.sunElevationDeg = 55.0;
    aux.sunAzimuthDeg = 150.0;
    aux.hasSunGeometry = true;
    CHECK( resolve( QStringLiteral( "test-lut-6s" ), aux, &p ) );

    const std::vector<float> toa = { 0.3f, 0.4f };
    std::vector<float> out( toa.size() );
    REQUIRE( p->toSurfaceReflectance( toa.data(), out.data(), toa.size(), aux ) );
    CHECK( stub.m_calls == 1 );
}

TEST_CASE( "single/multi-band mismatches refuse with typed errors", "[provider]" )
{
    ensureBuiltinProviders();
    CorrectionInputs aux;
    aux.toaDarkLevel = 0.1f;
    const std::vector<float> toa = { 0.3f };
    std::vector<float> out( 1 );

    QString err;
    const auto *quac = provider( QStringLiteral( "quac" ) );
    REQUIRE( quac );
    // QUAC on a single band: the statistics method needs the stack.
    CHECK_FALSE( quac->toSurfaceReflectance( toa.data(), out.data(), 1, aux, &err ) );
    CHECK( err.contains( QLatin1String( "multi-band" ) ) );

    // DOS1 on a multi-band call: base implementation refuses.
    const auto *dos1 = provider( QStringLiteral( "dos1" ) );
    REQUIRE( dos1 );
    const float *const inBands[] = { toa.data() };
    float *const outBands[] = { out.data() };
    CHECK_FALSE( dos1->toSurfaceReflectanceMultiBand( inBands, outBands, 1, 1, aux, &err ) );
    CHECK( err.contains( QLatin1String( "multi-band" ) ) );
}

TEST_CASE( "QUAC adapter dispatches to the wrapped house kernel", "[provider]" )
{
    ensureBuiltinProviders();
    // Small 3-band scene with controlled spread (house QUAC uses the 1st/99th
    // percentile per band and maps the bright end to ~0.5).
    constexpr size_t n = 100;
    std::vector<std::vector<float>> dn( 3 ), viaProvider( 3 ), direct( 3 );
    for ( int b = 0; b < 3; ++b )
    {
        dn[b].resize( n );
        for ( size_t i = 0; i < n; ++i )
            dn[b][i] = 1000.0f + 10.0f * static_cast<float>( i )
                       + 100.0f * static_cast<float>( b );
        viaProvider[b].resize( n );
        direct[b].resize( n );
    }
    std::vector<const float *> inPtrs;
    std::vector<float *> providerPtrs, directPtrs;
    for ( int b = 0; b < 3; ++b )
    {
        inPtrs.push_back( dn[b].data() );
        providerPtrs.push_back( viaProvider[b].data() );
        directPtrs.push_back( direct[b].data() );
    }
    CorrectionInputs aux; // QUAC declares no requirements
    const auto *quac = provider( QStringLiteral( "quac" ) );
    REQUIRE(
        quac->toSurfaceReflectanceMultiBand( inPtrs.data(), providerPtrs.data(), 3, n, aux ) );
    REQUIRE( AtmosphericCorrection::quac( inPtrs.data(), directPtrs.data(), 3, n, nullptr ) );

    // Adapter parity: identical inputs through the wrapped kernel give
    // identical outputs (pins dispatch, not QUAC science).
    for ( int b = 0; b < 3; ++b )
        for ( size_t i = 0; i < n; ++i )
            REQUIRE( viaProvider[b][i] == direct[b][i] );
}
