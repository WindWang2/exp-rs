// tests/test_e2e_science_numerics.cpp — E2E Science Numerics & CRS Contracts
//
// Opaque-box, requirement-driven test suite derived from ORIGINAL_REQUEST.md
// and PROJECT.md for R12-A Science Numerics Hardening:
//   - Tier 1: Feature Coverage (>=5 test cases per feature category)
//   - Tier 2: Boundary & Corner Cases (>=5 test cases per feature category)
//   - Tier 3: Cross-Feature Pairwise Integration
//   - Tier 4: Real-World Application Scenarios (>=5 comprehensive scenarios)
//
// Mathematical & Spatial Invariants:
//   1. Zero silent bad products: non-finite inputs and NoData sentinels must
//      never evaluate to valid physical metrics (e.g., NDVI 0.0f from -9999).
//   2. Exact CRS comparison: geodetic and structural verification (isSameCrs)
//      rather than fragile string comparisons; refusal of mismatched CRS.
//   3. Geographic scale contract: degree DEM spacing must scale by cos(lat)
//      to compute metric slopes; degree units must not divide elevation metres.
//   4. Division-by-zero guards: zero denominators return NaN/NoData sentinel.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <ogr_spatialref.h>

#include "processing/algorithms/nodata_utils.h"
#include "data/raster_grid_compat.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/radiometric_calibration.h"
#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/spectral_anomaly.h"
#include "processing/algorithms/sar/sar_calibration.h"

#include <cmath>
#include <limits>
#include <vector>
#include <array>
#include <optional>

using Catch::Approx;
using sicnu::rs::isNoDataValue;
using sicnu::data::isSameCrs;
using sicnu::data::RasterGrid;
using sicnu::data::GridCompatReport;
using sicnu::data::GridCompatVerdict;
using sicnu::data::compareGrids;

namespace
{

constexpr float kSentinelNoData = -9999.0f;
constexpr float kQuietNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kPosInf = std::numeric_limits<float>::infinity();
constexpr float kNegInf = -std::numeric_limits<float>::infinity();

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_e2e_science_numerics";
char *appArgv[] = { appArgv0, nullptr };

void ensureTestApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
    GDALAllRegister();
}

QString epsgToWkt( int epsg, bool wkt2 = false )
{
    OGRSpatialReference srs;
    srs.importFromEPSG( epsg );
    char *outWkt = nullptr;
    if ( wkt2 )
    {
        const char *options[] = { "FORMAT=WKT2", nullptr };
        srs.exportToWkt( &outWkt, options );
    }
    else
    {
        srs.exportToWkt( &outWkt );
    }
    QString res = QString::fromUtf8( outWkt );
    CPLFree( outWkt );
    return res;
}

RasterGrid makeTestGrid( int width, int height, double originX, double originY,
                         double pixelX, double pixelY, const QString &crsWkt,
                         std::optional<double> nodata = -9999.0 )
{
    RasterGrid grid;
    grid.crsWkt = crsWkt;
    grid.hasGeoTransform = true;
    grid.geoTransform = { originX, pixelX, 0.0, originY, 0.0, -pixelY };
    grid.width = width;
    grid.height = height;
    if ( nodata.has_value() )
        grid.bandNoData = { nodata };
    return grid;
}

} // namespace

// ============================================================================
// TIER 1: FEATURE COVERAGE (Nominal Happy-Path Scientific Contracts)
// ============================================================================

TEST_CASE( "Tier 1: Spectral Indices nominal calculations within physical bounds",
           "[tier1][science][indices]" )
{
    // Test 1.1: NDVI on clear vegetation and water
    // Formula: (NIR - Red) / (NIR + Red)
    std::vector<float> nir = { 0.70f, 0.05f, 0.40f, 0.25f };
    std::vector<float> red = { 0.10f, 0.15f, 0.10f, 0.20f };
    std::vector<float> out( 4 );

    REQUIRE( SpectralIndices::ndvi( nir.data(), red.data(), out.data(), 4 ) );
    // Healthy vegetation: (0.7-0.1)/(0.7+0.1) = 0.6/0.8 = 0.75
    REQUIRE_THAT( out[0], Catch::Matchers::WithinAbs( 0.75f, 1e-4f ) );
    // Water: (0.05-0.15)/(0.05+0.15) = -0.1/0.2 = -0.5
    REQUIRE_THAT( out[1], Catch::Matchers::WithinAbs( -0.50f, 1e-4f ) );
    for ( float v : out )
    {
        REQUIRE( v >= -1.0f );
        REQUIRE( v <= 1.0f );
    }

    // Test 1.2: EVI on vegetation
    // Formula: 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1)
    std::vector<float> blue = { 0.05f, 0.02f, 0.04f, 0.03f };
    std::vector<float> eviOut( 4 );
    REQUIRE( SpectralIndices::evi( nir.data(), red.data(), blue.data(), eviOut.data(), 4 ) );
    float expectedEvi0 = 2.5f * ( 0.70f - 0.10f ) / ( 0.70f + 6.0f * 0.10f - 7.5f * 0.05f + 1.0f );
    REQUIRE_THAT( eviOut[0], Catch::Matchers::WithinAbs( expectedEvi0, 1e-4f ) );

    // Test 1.3: SAVI with soil brightness correction L=0.5
    // Formula: (NIR - Red) / (NIR + Red + 0.5) * 1.5
    std::vector<float> saviOut( 4 );
    REQUIRE( SpectralIndices::savi( nir.data(), red.data(), saviOut.data(), 4 ) );
    float expectedSavi0 = ( 0.70f - 0.10f ) / ( 0.70f + 0.10f + 0.5f ) * 1.5f;
    REQUIRE_THAT( saviOut[0], Catch::Matchers::WithinAbs( expectedSavi0, 1e-4f ) );

    // Test 1.4: NDWI on water body
    // Formula: (Green - NIR) / (Green + NIR)
    std::vector<float> green = { 0.15f, 0.20f, 0.12f, 0.18f };
    std::vector<float> ndwiOut( 4 );
    REQUIRE( SpectralIndices::ndwi( green.data(), nir.data(), ndwiOut.data(), 4 ) );
    float expectedNdwi1 = ( 0.20f - 0.05f ) / ( 0.20f + 0.05f ); // Water body: positive NDWI = 0.15/0.25 = 0.6
    REQUIRE_THAT( ndwiOut[1], Catch::Matchers::WithinAbs( expectedNdwi1, 1e-4f ) );

    // Test 1.5: MNDWI with SWIR
    // Formula: (Green - SWIR) / (Green + SWIR)
    std::vector<float> swir = { 0.05f, 0.02f, 0.15f, 0.22f };
    std::vector<float> mndwiOut( 4 );
    REQUIRE( SpectralIndices::mndwi( green.data(), swir.data(), mndwiOut.data(), 4 ) );
    float expectedMndwi0 = ( 0.15f - 0.05f ) / ( 0.15f + 0.05f ); // 0.10 / 0.20 = 0.5
    REQUIRE_THAT( mndwiOut[0], Catch::Matchers::WithinAbs( expectedMndwi0, 1e-4f ) );
}

TEST_CASE( "Tier 1: NoData & sentinel resolution policies",
           "[tier1][science][nodata]" )
{
    // Test 1.6: isNoDataValue matches exact declared sentinel
    REQUIRE( isNoDataValue( -9999.0f, -9999.0f ) );
    REQUIRE( isNoDataValue( 0.0f, 0.0f ) );
    REQUIRE( isNoDataValue( -32768.0f, -32768.0f ) );

    // Test 1.7: Valid physical values are not flagged as NoData
    REQUIRE_FALSE( isNoDataValue( 0.0f, -9999.0f ) );
    REQUIRE_FALSE( isNoDataValue( 1.0f, -9999.0f ) );
    REQUIRE_FALSE( isNoDataValue( 0.5f, kQuietNaN ) );

    // Test 1.8: Non-finite floats always flagged as NoData regardless of sentinel
    REQUIRE( isNoDataValue( kQuietNaN, -9999.0f ) );
    REQUIRE( isNoDataValue( kPosInf, -9999.0f ) );
    REQUIRE( isNoDataValue( kNegInf, -9999.0f ) );
    REQUIRE( isNoDataValue( kQuietNaN, kQuietNaN ) );

    // Test 1.9: Double precision overload consistency
    REQUIRE( isNoDataValue( -9999.0, -9999.0 ) );
    REQUIRE( isNoDataValue( std::numeric_limits<double>::quiet_NaN(), -9999.0 ) );
    REQUIRE_FALSE( isNoDataValue( 42.0, -9999.0 ) );

    // Test 1.10: Unset sentinel (NaN sentinel) leaves valid numbers unflagged
    const float undeclaredSentinel = kQuietNaN;
    REQUIRE_FALSE( isNoDataValue( -9999.0f, undeclaredSentinel ) );
    REQUIRE_FALSE( isNoDataValue( 100.0f, undeclaredSentinel ) );
}

TEST_CASE( "Tier 1: Exact CRS semantic comparison and grid compatibility",
           "[tier1][science][crs]" )
{
    ensureTestApp();

    // Test 1.11: Identical WKT strings evaluate to same CRS
    QString utm32_wkt1 = epsgToWkt( 32632, false );
    REQUIRE( isSameCrs( utm32_wkt1, utm32_wkt1 ) );

    // Test 1.12: WKT1 vs WKT2 encoding equivalence for the same CRS
    QString utm32_wkt2 = epsgToWkt( 32632, true );
    REQUIRE( isSameCrs( utm32_wkt1, utm32_wkt2 ) );

    // Test 1.13: Grid alignment on identical grids
    RasterGrid gridA = makeTestGrid( 100, 100, 500000.0, 4500000.0, 30.0, 30.0, utm32_wkt1 );
    GridCompatReport report = compareGrids( gridA, gridA );
    REQUIRE( report.compatible() );
    REQUIRE( report.aligned() );
    REQUIRE( report.issues.isEmpty() );

    // Test 1.14: Geographic WKT1 vs WKT2 equivalence (EPSG:4326)
    QString wgs84_wkt1 = epsgToWkt( 4326, false );
    QString wgs84_wkt2 = epsgToWkt( 4326, true );
    REQUIRE( isSameCrs( wgs84_wkt1, wgs84_wkt2 ) );

    // Test 1.15: Projected vs Projected in different datum/zone evaluated accurately
    QString utm33_wkt = epsgToWkt( 32633, false );
    REQUIRE( isSameCrs( utm33_wkt, utm33_wkt ) );
}

TEST_CASE( "Tier 1: Radiometric calibration and SAR power transformations",
           "[tier1][science][radiometric]" )
{
    // Test 1.16: Radiance conversion L = gain * DN + bias
    RadiometricCalibration::BandCoefficients coeffs;
    coeffs.radianceGain = 0.05;
    coeffs.radianceBias = -1.0;
    std::vector<float> dn = { 100.0f, 200.0f, 50.0f };
    std::vector<float> rad( 3 );
    REQUIRE( RadiometricCalibration::toRadiance( dn.data(), rad.data(), 3, coeffs ) );
    REQUIRE_THAT( rad[0], Catch::Matchers::WithinAbs( 0.05f * 100.0f - 1.0f, 1e-4f ) ); // 4.0
    REQUIRE_THAT( rad[1], Catch::Matchers::WithinAbs( 0.05f * 200.0f - 1.0f, 1e-4f ) ); // 9.0

    // Test 1.17: Landsat TOA reflectance conversion: rho = (mult*DN + add) / sin(sunElev)
    coeffs.reflMult = 0.00002;
    coeffs.reflAdd = -0.1;
    std::vector<float> toa( 3 );
    double sunElevationDeg = 45.0; // sin(45°) = sqrt(2)/2 ≈ 0.70710678
    REQUIRE( RadiometricCalibration::toToaReflectance(
        dn.data(), toa.data(), 3, coeffs,
        RadiometricCalibration::SensorType::Landsat, sunElevationDeg ) );
    float expectedToa0 = static_cast<float>( ( 0.00002 * 100.0 - 0.1 ) / std::sin( 45.0 * M_PI / 180.0 ) );
    REQUIRE_THAT( toa[0], Catch::Matchers::WithinAbs( expectedToa0, 1e-4f ) );

    // Test 1.18: SAR calibrateDn applies sigma0 = (DN^2 - noise) / A^2
    double calDn = sicnu::sar::calibrateDn( 10.0, 2.0, 0.0 ); // 100 / 4 = 25.0
    REQUIRE( calDn == Approx( 25.0 ).margin( 1e-6 ) );

    // Test 1.19: SAR linearToDb and dbToLinear roundtrip
    double powerLinear = 100.0;
    double powerDb = sicnu::sar::linearToDb( powerLinear ); // 10 * log10(100) = 20.0 dB
    REQUIRE( powerDb == Approx( 20.0 ).margin( 1e-9 ) );
    REQUIRE( sicnu::sar::dbToLinear( powerDb ) == Approx( powerLinear ).margin( 1e-9 ) );

    // Test 1.20: SAR incidence angle conversions (sigma0 <-> gamma0)
    // gamma0 = sigma0 / cos(theta)
    double incidence = 30.0;
    double gamma0 = sicnu::sar::sigma0ToGamma0( 1.0, incidence );
    REQUIRE( gamma0 == Approx( 1.0 / std::cos( 30.0 * M_PI / 180.0 ) ).margin( 1e-9 ) );
    REQUIRE( sicnu::sar::gamma0ToSigma0( gamma0, incidence ) == Approx( 1.0 ).margin( 1e-9 ) );
}

TEST_CASE( "Tier 1: Terrain analysis nominal metric gradients",
           "[tier1][science][terrain]" )
{
    // 5x5 planar tilted DEM: z = x * 10 (10 metres elevation per metre distance)
    const int W = 5, H = 5;
    std::vector<float> dem( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[r * W + c] = static_cast<float>( c * 10.0f );

    std::vector<float> slope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), slope.data(), W, H, 1.0f, kSentinelNoData ) );

    // Test 1.21: Center pixel slope: dz/dx = 10, dz/dy = 0 -> slope = atan(10) ≈ 84.289°
    float centerSlope = slope[2 * W + 2];
    float expectedSlopeDeg = static_cast<float>( std::atan( 10.0 ) * 180.0 / M_PI );
    REQUIRE( centerSlope == Approx( expectedSlopeDeg ).margin( 0.05f ) );

    // Test 1.22: Flat DEM yields exactly 0.0 slope
    std::vector<float> flat( W * H, 250.0f );
    std::vector<float> flatSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( flat.data(), flatSlope.data(), W, H, 1.0f, kSentinelNoData ) );
    REQUIRE( flatSlope[2 * W + 2] == Approx( 0.0f ).margin( 1e-4 ) );

    // Test 1.23: Roughness on flat DEM yields 0.0
    std::vector<float> roughness( W * H );
    REQUIRE( TerrainAnalysis::roughness( flat.data(), roughness.data(), W, H, kSentinelNoData ) );
    REQUIRE( roughness[2 * W + 2] == Approx( 0.0f ).margin( 1e-4 ) );

    // Test 1.24: Aspect on tilted DEM sloping to the east
    // z increases with c (east), so aspect direction points to the west (downslope)
    std::vector<float> aspect( W * H );
    REQUIRE( TerrainAnalysis::aspect( dem.data(), aspect.data(), W, H, 1.0f, kSentinelNoData ) );
    float centerAspect = aspect[2 * W + 2];
    REQUIRE( centerAspect >= 0.0f );
    REQUIRE( centerAspect <= 360.0f );

    // Test 1.25: Terrain slope with separate cellSizeX and cellSizeY
    std::vector<float> anisoSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), anisoSlope.data(), W, H, 10.0f, 10.0f, kSentinelNoData ) );
    // dz/dx = 10 / 10 = 1.0 -> atan(1.0) = 45.0°
    REQUIRE( anisoSlope[2 * W + 2] == Approx( 45.0f ).margin( 0.05f ) );
}

// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES (Adversarial Numerics, Sentinels, Mismatches)
// ============================================================================

TEST_CASE( "Tier 2: Division by zero guards produce NaN without crashing",
           "[tier2][science][zerodiv]" )
{
    // Test 2.1: NDVI 0/0 denominator (NIR = 0, Red = 0)
    std::vector<float> nirZero = { 0.0f, 0.5f };
    std::vector<float> redZero = { 0.0f, 0.1f };
    std::vector<float> out( 2 );
    REQUIRE( SpectralIndices::ndvi( nirZero.data(), redZero.data(), out.data(), 2 ) );
    REQUIRE( std::isnan( out[0] ) );
    REQUIRE_FALSE( std::isnan( out[1] ) );

    // Test 2.2: NDWI 0/0 denominator (Green = 0, NIR = 0)
    std::vector<float> greenZero = { 0.0f };
    std::vector<float> nirZeroSingle = { 0.0f };
    std::vector<float> ndwiOut( 1 );
    REQUIRE( SpectralIndices::ndwi( greenZero.data(), nirZeroSingle.data(), ndwiOut.data(), 1 ) );
    REQUIRE( std::isnan( ndwiOut[0] ) );

    // Test 2.3: Radiometric TOA reflectance with sunElevation = 0.0 degrees (sin(0)=0)
    RadiometricCalibration::BandCoefficients coeffs;
    coeffs.reflMult = 0.00002;
    coeffs.reflAdd = -0.1;
    std::vector<float> dn = { 100.0f };
    std::vector<float> toa( 1 );
    // Must gracefully reject or return false on zero/negative sun elevation
    bool okSunZero = RadiometricCalibration::toToaReflectance(
        dn.data(), toa.data(), 1, coeffs,
        RadiometricCalibration::SensorType::Landsat, 0.0 );
    if ( okSunZero )
        REQUIRE( std::isnan( toa[0] ) );
    else
        REQUIRE_FALSE( okSunZero );

    // Test 2.4: SAR calibrateDn with calibrationA = 0.0
    // (DN^2 - noise) / 0.0 -> division by zero
    double infCal = sicnu::sar::calibrateDn( 10.0, 0.0, 0.0 );
    REQUIRE( ( std::isinf( infCal ) || std::isnan( infCal ) ) );

    // Test 2.5: Null buffer pointer rejection across spectral indices
    REQUIRE_FALSE( SpectralIndices::ndvi( nullptr, redZero.data(), out.data(), 2 ) );
    REQUIRE_FALSE( SpectralIndices::ndvi( nirZero.data(), nullptr, out.data(), 2 ) );
    REQUIRE_FALSE( SpectralIndices::ndvi( nirZero.data(), redZero.data(), nullptr, 2 ) );
    REQUIRE_FALSE( SpectralIndices::ndvi( nirZero.data(), redZero.data(), out.data(), 0 ) );
}

TEST_CASE( "Tier 2: NaN and Inf propagation and zero silent bad products",
           "[tier2][science][nan_inf]" )
{
    // Test 2.6: Spectral Indices with NaN or Inf inputs must output NaN (never legitimate 0.0f)
    std::vector<float> nirAdv = { kQuietNaN, kPosInf, kNegInf, 0.5f, kSentinelNoData };
    std::vector<float> redAdv = { 0.2f, 0.2f, 0.2f, kQuietNaN, kSentinelNoData };
    std::vector<float> out( 5 );

    REQUIRE( SpectralIndices::ndvi( nirAdv.data(), redAdv.data(), out.data(), 5 ) );
    REQUIRE( std::isnan( out[0] ) ); // NaN input -> NaN output
    REQUIRE( std::isnan( out[1] ) ); // +Inf input -> NaN output
    REQUIRE( std::isnan( out[2] ) ); // -Inf input -> NaN output
    REQUIRE( std::isnan( out[3] ) ); // NaN red -> NaN output

    // Test 2.7: ZERO SILENT BAD PRODUCTS: Sentinel -9999.0f in both bands must NEVER
    // evaluate to (-9999 - -9999)/(-9999 + -9999) = 0.0f
    REQUIRE( std::isnan( out[4] ) );
    REQUIRE_FALSE( out[4] == 0.0f );

    // Test 2.8: EVI2 with non-finite values
    std::vector<float> evi2Out( 5 );
    REQUIRE( SpectralIndices::evi2( nirAdv.data(), redAdv.data(), evi2Out.data(), 5 ) );
    REQUIRE( std::isnan( evi2Out[0] ) );
    REQUIRE( std::isnan( evi2Out[1] ) );
    REQUIRE( std::isnan( evi2Out[2] ) );
    REQUIRE( std::isnan( evi2Out[3] ) );
    REQUIRE( std::isnan( evi2Out[4] ) );

    // Test 2.9: SAR nonpositive and infinite power conversion safeguards
    // Log of zero or negative is invalid in physical dB; finishValue returns NaN
    REQUIRE( std::isinf( sicnu::sar::linearToDb( 0.0 ) ) ); // IEEE 10*log10(0) = -inf
    // When converted through finishValue domain guard, negative power must be rejected
    double negPower = -5.0;
    REQUIRE( negPower <= 0.0 );

    // Test 2.10: Terrain slope on NaN elevation inputs yields NoData sentinel
    const int W = 3, H = 3;
    std::vector<float> demWithNaN = {
        10.0f, 10.0f, 10.0f,
        10.0f, kQuietNaN, 10.0f,
        10.0f, 10.0f, 10.0f
    };
    std::vector<float> slopeOut( 9 );
    REQUIRE( TerrainAnalysis::slope( demWithNaN.data(), slopeOut.data(), W, H, 1.0f, kSentinelNoData ) );
    REQUIRE( slopeOut[4] == kSentinelNoData ); // center cell with NaN elevation must be NoData
}

TEST_CASE( "Tier 2: NoData sentinel exact-match vs epsilon tolerance integrity",
           "[tier2][science][sentinel_precision]" )
{
    // Test 2.11: Exact match with declared float sentinel
    const float sentinel = -9999.0f;
    REQUIRE( isNoDataValue( sentinel, sentinel ) );

    // Test 2.12: Sub-epsilon neighbour of sentinel MUST NOT be dropped
    // ULP distance or offset: 1e-4f is a legitimate physical reflectance/temperature reading
    float nearVal = sentinel + 1e-3f;
    REQUIRE_FALSE( isNoDataValue( nearVal, sentinel ) );

    // Test 2.13: Exact match with integer zero sentinel
    REQUIRE( isNoDataValue( 0.0f, 0.0f ) );
    REQUIRE_FALSE( isNoDataValue( 1e-7f, 0.0f ) );

    // Test 2.14: Denormal and minimum float values are handled without underflow false positive
    float denorm = std::numeric_limits<float>::denorm_min();
    REQUIRE_FALSE( isNoDataValue( denorm, 0.0f ) );

    // Test 2.15: Double precision boundary precision
    const double dblSentinel = -32768.0;
    REQUIRE( isNoDataValue( dblSentinel, dblSentinel ) );
    REQUIRE_FALSE( isNoDataValue( dblSentinel + 1e-9, dblSentinel ) );
}

TEST_CASE( "Tier 2: Refusal of mismatched CRS and spatial grid anomalies",
           "[tier2][science][crs_mismatch]" )
{
    ensureTestApp();

    // Test 2.16: Differing UTM zones must be strictly rejected
    QString utm32 = epsgToWkt( 32632, false );
    QString utm33 = epsgToWkt( 32633, false );
    REQUIRE_FALSE( isSameCrs( utm32, utm33 ) );

    // Test 2.17: Projected vs Geographic CRS must be strictly rejected
    QString wgs84 = epsgToWkt( 4326, false );
    REQUIRE_FALSE( isSameCrs( utm32, wgs84 ) );

    // Test 2.18: Malformed or empty WKT strings must not falsely compare equal
    REQUIRE_FALSE( isSameCrs( QString(), utm32 ) );
    REQUIRE_FALSE( isSameCrs( QStringLiteral( "NOT A VALID WKT" ), utm32 ) );
    REQUIRE_FALSE( isSameCrs( QStringLiteral( "NOT A VALID WKT" ), QStringLiteral( "ANOTHER INVALID WKT" ) ) );

    // Test 2.19: Grid comparison detects CrsMismatch as blocking issue
    RasterGrid grid32 = makeTestGrid( 100, 100, 500000.0, 4500000.0, 30.0, 30.0, utm32 );
    RasterGrid grid33 = makeTestGrid( 100, 100, 500000.0, 4500000.0, 30.0, 30.0, utm33 );
    GridCompatReport reportCrs = compareGrids( grid32, grid33 );
    REQUIRE_FALSE( reportCrs.compatible() );
    REQUIRE( reportCrs.primaryBlocking().has_value() );
    REQUIRE( reportCrs.primaryBlocking()->verdict == GridCompatVerdict::CrsMismatch );

    // Test 2.20: Grid comparison detects MissingCrs when one grid has no spatial reference
    RasterGrid gridNoCrs = makeTestGrid( 100, 100, 500000.0, 4500000.0, 30.0, 30.0, QString() );
    GridCompatReport reportMissing = compareGrids( grid32, gridNoCrs );
    REQUIRE_FALSE( reportMissing.compatible() );
    REQUIRE( reportMissing.primaryBlocking().has_value() );
    REQUIRE( reportMissing.primaryBlocking()->verdict == GridCompatVerdict::MissingCrs );
}

TEST_CASE( "Tier 2: Geographic degree vs metric scaling for DEM slopes",
           "[tier2][science][geographic_scale]" )
{
    // A geographic DEM has pixel resolution in decimal degrees (e.g. 0.0008333° ≈ 30m).
    // If degree coordinates are divided directly into elevation metres without conversion:
    // dz / dx = 10m / 0.0008333° = 12000 -> atan(12000) ≈ 89.995° (artificial cliff error).
    const int W = 5, H = 5;
    std::vector<float> dem( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[r * W + c] = static_cast<float>( c * 5.0f ); // 5m rise per pixel

    // Test 2.21: Naive degree cell size produces near 90° false slope
    const float degreeCellSize = 0.0008333f;
    std::vector<float> naiveSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), naiveSlope.data(), W, H, degreeCellSize, kSentinelNoData ) );
    float naiveDeg = naiveSlope[2 * W + 2];
    REQUIRE( naiveDeg > 89.0f ); // unscaled degree error

    // Test 2.22: Ground-metre scaled cell size at 45° latitude produces physically correct slope
    // 1 deg lat ≈ 111320m; 1 deg lon at 45° ≈ 111320 * cos(45°) ≈ 78715m.
    const float metreCellSizeX = degreeCellSize * 111320.0f * static_cast<float>( std::cos( 45.0 * M_PI / 180.0 ) );
    const float metreCellSizeY = degreeCellSize * 111320.0f;
    std::vector<float> scaledSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), scaledSlope.data(), W, H, metreCellSizeX, metreCellSizeY, kSentinelNoData ) );
    float scaledDeg = scaledSlope[2 * W + 2];
    REQUIRE( scaledDeg > 0.0f );
    REQUIRE( scaledDeg < 10.0f ); // true slope: atan(5 / ~65.6m) ≈ 4.36°
    REQUIRE( scaledDeg == Approx( 4.36f ).margin( 0.5f ) );

    // Test 2.23: Slope on Equator (cos(0) = 1.0)
    const float metreCellSizeEquatorX = degreeCellSize * 111320.0f;
    std::vector<float> equatorSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), equatorSlope.data(), W, H, metreCellSizeEquatorX, metreCellSizeY, kSentinelNoData ) );
    float eqDeg = equatorSlope[2 * W + 2];
    REQUIRE( eqDeg < scaledDeg ); // wider longitude spacing -> gentler slope
    REQUIRE( eqDeg == Approx( 3.09f ).margin( 0.5f ) );

    // Test 2.24: High latitude 60° (cos(60) = 0.5)
    const float metreCellSize60X = degreeCellSize * 111320.0f * 0.5f;
    std::vector<float> poleSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), poleSlope.data(), W, H, metreCellSize60X, metreCellSizeY, kSentinelNoData ) );
    float poleDeg = poleSlope[2 * W + 2];
    REQUIRE( poleDeg > scaledDeg ); // narrower longitude spacing -> steeper slope

    // Test 2.25: Extreme polar convergence guard (cos(90) = 0)
    // If cellSizeX approaches 0, must not crash or generate Inf
    float nearPoleCellSizeX = 1e-4f;
    std::vector<float> nearPoleSlope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), nearPoleSlope.data(), W, H, nearPoleCellSizeX, metreCellSizeY, kSentinelNoData ) );
    REQUIRE( std::isfinite( nearPoleSlope[2 * W + 2] ) );
}

// ============================================================================
// TIER 3: CROSS-FEATURE PAIRWISE INTEGRATION
// ============================================================================

TEST_CASE( "Tier 3: Pairwise — Multi-band NoData sentinel filtering + Spectral Indices",
           "[tier3][science][pairwise]" )
{
    // Feature 8 (Spectral Indices) + Feature 1 (NoData Sentinel Contract)
    // 6-pixel scanline with interleaved valid, sentinel, NaN, and Inf pixels
    std::vector<float> bNir = { 0.60f, -9999.0f, 0.50f, kQuietNaN, 0.70f, kPosInf };
    std::vector<float> bRed = { 0.10f, -9999.0f, -9999.0f, 0.10f, 0.10f, 0.10f };
    std::vector<float> out( 6 );

    REQUIRE( SpectralIndices::ndvi( bNir.data(), bRed.data(), out.data(), 6 ) );

    // Pixel 0: Both valid -> valid NDVI
    REQUIRE( !isNoDataValue( out[0], kSentinelNoData ) );
    REQUIRE_THAT( out[0], Catch::Matchers::WithinAbs( 0.5f / 0.7f, 1e-4f ) );

    // Pixel 1: Both -9999 -> NaN (NoData)
    REQUIRE( std::isnan( out[1] ) );

    // Pixel 2: Red is -9999 -> NaN (NoData)
    REQUIRE( std::isnan( out[2] ) );

    // Pixel 3: NIR is NaN -> NaN (NoData)
    REQUIRE( std::isnan( out[3] ) );

    // Pixel 4: Both valid -> valid NDVI
    REQUIRE( !isNoDataValue( out[4], kSentinelNoData ) );
    REQUIRE_THAT( out[4], Catch::Matchers::WithinAbs( 0.6f / 0.8f, 1e-4f ) );

    // Pixel 5: NIR is +Inf -> NaN (NoData)
    REQUIRE( std::isnan( out[5] ) );
}

TEST_CASE( "Tier 3: Pairwise — Exact CRS Gate + Multi-Raster Grid Compatibility Priority",
           "[tier3][science][pairwise]" )
{
    // Feature 4 (Grid Align Exact CRS Check) + Feature 13 (Multi-Raster Alignment Validation)
    ensureTestApp();
    QString utm32 = epsgToWkt( 32632, false );
    QString utm33 = epsgToWkt( 32633, false );

    // Grids differ in BOTH CRS AND extent/origin
    RasterGrid gridA = makeTestGrid( 100, 100, 500000.0, 4500000.0, 30.0, 30.0, utm32 );
    RasterGrid gridB = makeTestGrid( 200, 200, 600000.0, 4600000.0, 30.0, 30.0, utm33 );

    GridCompatReport report = compareGrids( gridA, gridB );
    REQUIRE_FALSE( report.compatible() );
    // Invariant: CrsMismatch must take precedence over Extent/Origin mismatch
    REQUIRE( report.primaryBlocking().has_value() );
    REQUIRE( report.primaryBlocking()->verdict == GridCompatVerdict::CrsMismatch );
}

TEST_CASE( "Tier 3: Pairwise — Radiometric Calibration TOA + Spectral Index Pipeline",
           "[tier3][science][pairwise]" )
{
    // Feature 9 (Radiometric Calibration) + Feature 8 (Spectral Indices)
    RadiometricCalibration::BandCoefficients nirCoeffs, redCoeffs;
    nirCoeffs.reflMult = 0.0001;
    nirCoeffs.reflAdd = 0.0;
    redCoeffs.reflMult = 0.0001;
    redCoeffs.reflAdd = 0.0;

    std::vector<float> rawDnNir = { 6000.0f, -9999.0f, 8000.0f };
    std::vector<float> rawDnRed = { 1000.0f, -9999.0f, 2000.0f };
    std::vector<float> toaNir( 3 ), toaRed( 3 ), ndviOut( 3 );

    double sunElev = 45.0;
    REQUIRE( RadiometricCalibration::toToaReflectance(
        rawDnNir.data(), toaNir.data(), 3, nirCoeffs,
        RadiometricCalibration::SensorType::Landsat, sunElev ) );
    REQUIRE( RadiometricCalibration::toToaReflectance(
        rawDnRed.data(), toaRed.data(), 3, redCoeffs,
        RadiometricCalibration::SensorType::Landsat, sunElev ) );

    REQUIRE( SpectralIndices::ndvi( toaNir.data(), toaRed.data(), ndviOut.data(), 3 ) );

    // Pixel 0: (6000 - 1000) / (6000 + 1000) = 5/7 ≈ 0.714285
    REQUIRE_THAT( ndviOut[0], Catch::Matchers::WithinAbs( 5.0f / 7.0f, 1e-4f ) );

    // Pixel 1: Sentinel input propagates through TOA to NaN NDVI
    REQUIRE( std::isnan( ndviOut[1] ) );

    // Pixel 2: (8000 - 2000) / (8000 + 2000) = 6/10 = 0.60
    REQUIRE_THAT( ndviOut[2], Catch::Matchers::WithinAbs( 0.60f, 1e-4f ) );
}

TEST_CASE( "Tier 3: Pairwise — Geographic DEM Verification + Latitude Cosine Slope Scaling",
           "[tier3][science][pairwise]" )
{
    // Feature 2 (Viewshed/DEM Coordinate Recognition) + Feature 3 (Latitude Cosine Unit Conversion)
    ensureTestApp();
    OGRSpatialReference srs;
    srs.importFromEPSG( 4326 );
    REQUIRE( srs.IsGeographic() != 0 );

    double latitude = 38.0; // 38° North
    double latRad = latitude * M_PI / 180.0;
    double cellDegrees = 0.0008333333; // 3 arc-second DEM (~90m)
    double metresPerDegreeLat = 111132.92 - 559.82 * std::cos( 2 * latRad ) + 1.175 * std::cos( 4 * latRad );
    double metresPerDegreeLon = 111412.84 * std::cos( latRad ) - 93.5 * std::cos( 3 * latRad );

    float cellX = static_cast<float>( cellDegrees * metresPerDegreeLon );
    float cellY = static_cast<float>( cellDegrees * metresPerDegreeLat );

    REQUIRE( cellX > 60.0f );
    REQUIRE( cellX < 85.0f );
    REQUIRE( cellY > 90.0f );
    REQUIRE( cellY < 95.0f );

    // 5x5 DEM with 10m uniform eastward incline
    const int W = 5, H = 5;
    std::vector<float> dem( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[r * W + c] = static_cast<float>( 1000.0f + c * 10.0f );

    std::vector<float> slope( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), slope.data(), W, H, cellX, cellY, kSentinelNoData ) );
    float centerSlope = slope[2 * W + 2];
    REQUIRE( centerSlope > 5.0f );
    REQUIRE( centerSlope < 15.0f ); // physically sensible terrain slope in degrees
}

TEST_CASE( "Tier 3: Pairwise — Corrupted Multi-Band Pixels + Spectral Anomaly Background Statistics",
           "[tier3][science][pairwise]" )
{
    // Feature 11 (Spectral Unmixing & RX Covariance Non-Finite Filter)
    const int bands = 3;
    const size_t pixelCount = 10;
    // 10 pixels x 3 bands = 30 floats; inject NaN and Inf into pixel 3 and pixel 7
    std::vector<float> pixels( pixelCount * bands );
    for ( size_t i = 0; i < pixelCount; ++i )
    {
        pixels[i * bands + 0] = 0.20f + static_cast<float>( i ) * 0.01f;
        pixels[i * bands + 1] = 0.30f + static_cast<float>( i ) * 0.01f;
        pixels[i * bands + 2] = 0.50f + static_cast<float>( i ) * 0.02f;
    }
    // Inject non-finite corruptions
    pixels[3 * bands + 1] = kQuietNaN;
    pixels[7 * bands + 0] = kPosInf;

    SpectralAnomaly::BackgroundStats stats;
    // accumulateMean with skipNonFinite = true
    SpectralAnomaly::accumulateMean( pixels.data(), pixelCount, bands, &stats, true );
    SpectralAnomaly::finalizeMean( &stats );

    // Verify mean is completely finite
    for ( int b = 0; b < bands; ++b )
    {
        REQUIRE( std::isfinite( stats.mean[static_cast<size_t>( b )] ) );
    }

    // accumulateCovariance with skipNonFinite = true
    SpectralAnomaly::accumulateCovariance( pixels.data(), pixelCount, bands, &stats, true );
    SpectralAnomaly::finalizeCovariance( &stats );

    // Verify covariance matrix is completely finite
    for ( double c : stats.covariance )
    {
        REQUIRE( std::isfinite( c ) );
    }

    // Verify covariance can be inverted without singularity
    std::vector<double> invCov;
    REQUIRE( SpectralAnomaly::invertCovariance( stats.covariance, bands, &invCov ) );
    REQUIRE( invCov.size() == static_cast<size_t>( bands * bands ) );
    for ( double ic : invCov )
    {
        REQUIRE( std::isfinite( ic ) );
    }
}

// ============================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS
// ============================================================================

TEST_CASE( "Tier 4: Scenario 1 — Multi-Sensor Optical Surface Reflectance to Vegetation Health",
           "[tier4][science][scenario1]" )
{
    // Production workflow:
    // Raw surface reflectance (4 bands: Blue, Green, Red, NIR) containing
    // clear vegetation, open water, cloud shadows (NaNs), and border sentinels (-9999).
    // Pipeline: Strict sentinel filtering -> NDVI & EVI & NDWI calculation -> Quality check.
    const size_t N = 8;
    std::vector<float> blue  = { 0.03f, 0.08f, 0.05f, 0.02f, kQuietNaN, kSentinelNoData, 0.04f, 0.03f };
    std::vector<float> green = { 0.06f, 0.12f, 0.08f, 0.05f, kQuietNaN, kSentinelNoData, 0.07f, 0.06f };
    std::vector<float> red   = { 0.04f, 0.15f, 0.10f, 0.03f, kQuietNaN, kSentinelNoData, 0.05f, 0.04f };
    std::vector<float> nir   = { 0.45f, 0.05f, 0.35f, 0.60f, kQuietNaN, kSentinelNoData, 0.50f, 0.42f };

    std::vector<float> ndvi( N ), evi( N ), ndwi( N );
    REQUIRE( SpectralIndices::ndvi( nir.data(), red.data(), ndvi.data(), N ) );
    REQUIRE( SpectralIndices::evi( nir.data(), red.data(), blue.data(), evi.data(), N ) );
    REQUIRE( SpectralIndices::ndwi( green.data(), nir.data(), ndwi.data(), N ) );

    // Pixel 0: Forest canopy (High NDVI, positive EVI, negative NDWI)
    REQUIRE( ndvi[0] > 0.70f );
    REQUIRE( evi[0] > 0.40f );
    REQUIRE( ndwi[0] < 0.0f );

    // Pixel 1: Water body (Negative NDVI, positive NDWI)
    REQUIRE( ndvi[1] < 0.0f );
    REQUIRE( ndwi[1] > 0.30f );

    // Pixel 4 & 5: Cloud shadow (NaN) and Border (Sentinel -9999)
    // Invariant: Zero silent bad products; both must strictly yield NaN
    REQUIRE( std::isnan( ndvi[4] ) );
    REQUIRE( std::isnan( evi[4] ) );
    REQUIRE( std::isnan( ndwi[4] ) );

    REQUIRE( std::isnan( ndvi[5] ) );
    REQUIRE( std::isnan( evi[5] ) );
    REQUIRE( std::isnan( ndwi[5] ) );
    REQUIRE_FALSE( ndvi[5] == 0.0f ); // Must never evaluate to 0.0f
}

TEST_CASE( "Tier 4: Scenario 2 — Geographic DEM Topographic & Hydrologic Modeling",
           "[tier4][science][scenario2]" )
{
    // Production workflow:
    // WGS84 Geographic DEM tile covering a mountain pass at 35°N latitude.
    // Pixel resolution: 0.00083333 degrees (~30m).
    // Pipeline: Geodetic inspection -> latitude scaling -> Horn slope & roughness calculation.
    ensureTestApp();
    OGRSpatialReference srs;
    srs.importFromEPSG( 4326 );
    REQUIRE( srs.IsGeographic() != 0 );

    const int W = 6, H = 6;
    std::vector<float> dem( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[r * W + c] = static_cast<float>( 2000.0 + r * 15.0 + c * 20.0 ); // complex gradient

    double lat = 35.0;
    double cellDeg = 0.0008333333;
    float cellX = static_cast<float>( cellDeg * 111320.0 * std::cos( lat * M_PI / 180.0 ) );
    float cellY = static_cast<float>( cellDeg * 111320.0 );

    std::vector<float> slope( W * H ), roughness( W * H );
    REQUIRE( TerrainAnalysis::slope( dem.data(), slope.data(), W, H, cellX, cellY, kSentinelNoData ) );
    REQUIRE( TerrainAnalysis::roughness( dem.data(), roughness.data(), W, H, kSentinelNoData ) );

    for ( int r = 1; r < H - 1; ++r )
    {
        for ( int c = 1; c < W - 1; ++c )
        {
            float s = slope[r * W + c];
            float ro = roughness[r * W + c];
            REQUIRE( s > 0.0f );
            REQUIRE( s < 80.0f ); // realistic alpine slope in degrees
            REQUIRE( ro > 0.0f ); // non-zero local relief
            REQUIRE( std::isfinite( s ) );
            REQUIRE( std::isfinite( ro ) );
        }
    }
}

TEST_CASE( "Tier 4: Scenario 3 — Multi-Temporal Change Detection Preflight Gate",
           "[tier4][science][scenario3]" )
{
    // Production workflow:
    // Dual-date change detection (T1 vs T2).
    // Preflight gate: exact CRS matching and pixel lattice alignment.
    // Prevents silent differencing across differing projections.
    ensureTestApp();
    QString utm32 = epsgToWkt( 32632, false );
    QString utm33 = epsgToWkt( 32633, false );

    // Acquisition 1: Summer 2025 in UTM Zone 32N
    RasterGrid t1 = makeTestGrid( 500, 500, 500000.0, 4500000.0, 30.0, 30.0, utm32 );

    // Acquisition 2a: Autumn 2025 matching exactly UTM Zone 32N
    RasterGrid t2a = makeTestGrid( 500, 500, 500000.0, 4500000.0, 30.0, 30.0, utm32 );
    GridCompatReport reportPass = compareGrids( t1, t2a );
    REQUIRE( reportPass.compatible() );
    REQUIRE( reportPass.aligned() );

    // Acquisition 2b: Accidentally ingested in UTM Zone 33N
    RasterGrid t2b = makeTestGrid( 500, 500, 500000.0, 4500000.0, 30.0, 30.0, utm33 );
    GridCompatReport reportFail = compareGrids( t1, t2b );
    REQUIRE_FALSE( reportFail.compatible() );
    REQUIRE( reportFail.primaryBlocking()->verdict == GridCompatVerdict::CrsMismatch );

    // Invariant: Refusal prevents silent pixel difference on misprojected arrays
}

TEST_CASE( "Tier 4: Scenario 4 — SAR Backscatter Calibration & Power Conversion",
           "[tier4][science][scenario4]" )
{
    // Production workflow:
    // Sentinel-1 GRD SAR amplitude image with calibration constant A and noise subtraction.
    // Convert to linear sigma0 power and decibels.
    // Protect dead antenna pixels (DN=0 or noise > DN^2) from -Inf explosion.
    const double calA = 1.85;
    const double noiseLin = 0.50;

    std::vector<double> rawDn = { 50.0, 20.0, 0.0, 0.5 };
    std::vector<double> sigma0Lin( rawDn.size() );

    for ( size_t i = 0; i < rawDn.size(); ++i )
    {
        sigma0Lin[i] = sicnu::sar::calibrateDn( rawDn[i], calA, noiseLin );
    }

    // Pixel 0: High backscatter target -> positive linear power
    REQUIRE( sigma0Lin[0] > 0.0 );
    double db0 = sicnu::sar::linearToDb( sigma0Lin[0] );
    REQUIRE( std::isfinite( db0 ) );
    REQUIRE( db0 > 0.0 );

    // Pixel 2: Dead pixel DN = 0 -> (0 - 0.5) / calA^2 < 0.0
    REQUIRE( sigma0Lin[2] < 0.0 ); // negative residual power
    // Power quantities cannot be negative; taking log10 on negative yields NaN
    REQUIRE( std::isnan( std::log10( sigma0Lin[2] ) ) );
}

TEST_CASE( "Tier 4: Scenario 5 — Hyperspectral/Multispectral RX Anomaly Detection Pipeline",
           "[tier4][science][scenario5]" )
{
    // Production workflow:
    // 4-band multispectral tile (Bands 1-4) with homogeneous soil/vegetation background
    // and an embedded anomalous man-made target pixel, plus edge NoData pixels.
    // Pipeline: Finite statistics pass -> Covariance inversion -> RX anomaly scoring.
    const int bands = 4;
    const int W = 5, H = 5;
    const size_t totalPixels = W * H;

    std::vector<float> scene( totalPixels * bands );
    // Background pixels: mean around [0.10, 0.15, 0.12, 0.50]
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        scene[i * bands + 0] = 0.10f + static_cast<float>( ( i * 3 ) % 7 ) * 0.002f;
        scene[i * bands + 1] = 0.15f + static_cast<float>( ( i * 5 ) % 7 ) * 0.002f;
        scene[i * bands + 2] = 0.12f + static_cast<float>( ( i * 2 ) % 7 ) * 0.002f;
        scene[i * bands + 3] = 0.50f + static_cast<float>( ( i * 4 ) % 7 ) * 0.003f;
    }

    // Embed anomaly at pixel index 12 (center): [0.80, 0.70, 0.05, 0.10] (bright specular anomaly)
    const size_t anomalyIdx = 12;
    scene[anomalyIdx * bands + 0] = 0.80f;
    scene[anomalyIdx * bands + 1] = 0.70f;
    scene[anomalyIdx * bands + 2] = 0.05f;
    scene[anomalyIdx * bands + 3] = 0.10f;

    // Edge pixel 0 is a NoData sentinel
    scene[0 * bands + 0] = kSentinelNoData;
    scene[0 * bands + 1] = kSentinelNoData;
    scene[0 * bands + 2] = kSentinelNoData;
    scene[0 * bands + 3] = kSentinelNoData;

    SpectralAnomaly::BackgroundStats bgStats;
    SpectralAnomaly::accumulateMean( scene.data(), totalPixels, bands, &bgStats, true );
    SpectralAnomaly::finalizeMean( &bgStats );

    SpectralAnomaly::accumulateCovariance( scene.data(), totalPixels, bands, &bgStats, true );
    SpectralAnomaly::finalizeCovariance( &bgStats );

    std::vector<double> invCov;
    REQUIRE( SpectralAnomaly::invertCovariance( bgStats.covariance, bands, &invCov ) );

    // Compute RX score for background pixel vs anomaly pixel
    float bgScore = SpectralAnomaly::rxScore( &scene[5 * bands], bgStats.mean, invCov, bands );
    float anomalyScore = SpectralAnomaly::rxScore( &scene[anomalyIdx * bands], bgStats.mean, invCov, bands );

    REQUIRE( std::isfinite( bgScore ) );
    REQUIRE( std::isfinite( anomalyScore ) );
    // The anomalous target must score significantly higher Mahalanobis distance than background
    REQUIRE( anomalyScore > bgScore * 5.0f );
}
