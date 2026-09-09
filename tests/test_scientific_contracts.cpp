// test_scientific_contracts.cpp — Foundation 6.0 Milestone B: the canonical
// scientific contract layer (numeric-domain decisions resolved once per
// raster, never per tile).
#include "processing/contracts/scientific_contracts.h"
#include "processing/algorithms/spectral_indices.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace sicnu::processing::contracts;

TEST_CASE( "A declared numeric scale resolves the domain outright",
           "[processing][contracts]" )
{
    const NumericDomainContract dn = domainFromDeclaredScale( 10000.0 );
    CHECK( dn.isDnScale() );
    CHECK( dn.divisor == Catch::Approx( 10000.0 ).margin( 1e-12 ) );
    CHECK( dn.resolvedBy == QStringLiteral( "declared-metadata" ) );

    const NumericDomainContract unit = domainFromDeclaredScale( 1.0 );
    CHECK_FALSE( unit.isDnScale() );
    CHECK( unit.divisor == Catch::Approx( 1.0 ).margin( 1e-12 ) );

    // Degenerate declarations fall back to identity, never divide by zero.
    const NumericDomainContract zero = domainFromDeclaredScale( 0.0 );
    CHECK_FALSE( zero.isDnScale() );
    CHECK( zero.divisor == Catch::Approx( 1.0 ).margin( 1e-12 ) );
}

TEST_CASE( "The magnitude rule is evaluated once on the dataset-level sample",
           "[processing][contracts]" )
{
    // #801: the threshold decision moved from per-tile magnitude scans to a
    // single dataset-level contract. The rule itself: max|value| > 5 means
    // DN-scale with the canonical 10000 divisor.
    const NumericDomainContract dn = domainFromMaxAbsSample( 8500.0 );
    CHECK( dn.isDnScale() );
    CHECK( dn.divisor == Catch::Approx( 10000.0 ).margin( 1e-12 ) );
    CHECK( dn.observedMaxAbs == Catch::Approx( 8500.0 ).margin( 1e-12 ) );
    CHECK( dn.resolvedBy == QStringLiteral( "dataset-statistics" ) );

    const NumericDomainContract unit = domainFromMaxAbsSample( 1.2 );
    CHECK_FALSE( unit.isDnScale() );
    CHECK( unit.divisor == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    CHECK( unit.resolvedBy == QStringLiteral( "dataset-statistics" ) );

    // Boundary: exactly at the threshold stays unit (strictly greater rule).
    CHECK_FALSE( domainFromMaxAbsSample( 5.0 ).isDnScale() );
    CHECK( domainFromMaxAbsSample( 5.0 + 1e-9 ).isDnScale() );
}

TEST_CASE( "The default domain is unit reflectance with provenance",
           "[processing][contracts]" )
{
    const NumericDomainContract fallback = defaultUnitDomain();
    CHECK_FALSE( fallback.isDnScale() );
    CHECK( fallback.divisor == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    CHECK( fallback.resolvedBy == QStringLiteral( "default-unit" ) );
}

TEST_CASE( "Numeric domain contracts round-trip through JSON provenance",
           "[processing][contracts]" )
{
    const NumericDomainContract dn = domainFromMaxAbsSample( 12000.0 );
    const QJsonObject json = dn.toJson();
    CHECK( json.value( QStringLiteral( "regime" ) ).toString()
           == QStringLiteral( "dn_scale" ) );
    CHECK( json.value( QStringLiteral( "divisor" ) ).toDouble()
           == Catch::Approx( 10000.0 ).margin( 1e-12 ) );
    CHECK( json.value( QStringLiteral( "observed_max_abs" ) ).toDouble()
           == Catch::Approx( 12000.0 ).margin( 1e-12 ) );
    CHECK_FALSE( json.value( QStringLiteral( "resolved_by" ) ).toString().isEmpty() );

    const QJsonObject unitJson = defaultUnitDomain().toJson();
    CHECK( unitJson.value( QStringLiteral( "regime" ) ).toString()
           == QStringLiteral( "unit_reflectance" ) );
}

TEST_CASE( "Unit kernels equal DN kernels on normalized input (regime"
           " consistency)",
           "[processing][contracts][kernels]" )
{
    // The DN variants are the unit formulas evaluated on DN inputs: dividing
    // by the divisor and applying the unit form must agree with the DN form
    // to float tolerance. This is what makes one dataset-level regime
    // decision seam-free (#801).
    const float dnNir[4] = { 4200.0f, 100.0f, 9000.0f, 0.0f };
    const float dnRed[4] = { 2100.0f, 90.0f, 2000.0f, 5000.0f };
    const float dnBlue[4] = { 1500.0f, 80.0f, 1800.0f, 4000.0f };
    float viaDn[4] = {};
    float unitFromDn[4] = {};
    float normalizedNir[4];
    float normalizedRed[4];
    float normalizedBlue[4];
    for ( int i = 0; i < 4; ++i )
    {
        normalizedNir[i] = dnNir[i] / 10000.0f;
        normalizedRed[i] = dnRed[i] / 10000.0f;
        normalizedBlue[i] = dnBlue[i] / 10000.0f;
    }
    REQUIRE( SpectralIndices::eviDn( dnNir, dnRed, dnBlue, viaDn, 4 ) );
    REQUIRE( SpectralIndices::eviUnit( normalizedNir, normalizedRed, normalizedBlue,
                                       unitFromDn, 4 ) );
    for ( int i = 0; i < 4; ++i )
        CHECK( viaDn[i] == Catch::Approx( unitFromDn[i] ).margin( 1e-5 ) );

    float saviDnOut[4] = {};
    float saviUnitOut[4] = {};
    REQUIRE( SpectralIndices::saviDn( dnNir, dnRed, saviDnOut, 4 ) );
    REQUIRE( SpectralIndices::saviUnit( normalizedNir, normalizedRed, saviUnitOut, 4 ) );
    for ( int i = 0; i < 4; ++i )
        CHECK( saviDnOut[i] == Catch::Approx( saviUnitOut[i] ).margin( 1e-5 ) );

    float evi2DnOut[4] = {};
    float evi2UnitOut[4] = {};
    REQUIRE( SpectralIndices::evi2Dn( dnNir, dnRed, evi2DnOut, 4 ) );
    REQUIRE( SpectralIndices::evi2Unit( normalizedNir, normalizedRed, evi2UnitOut, 4 ) );
    for ( int i = 0; i < 4; ++i )
        CHECK( evi2DnOut[i] == Catch::Approx( evi2UnitOut[i] ).margin( 1e-5 ) );
}
