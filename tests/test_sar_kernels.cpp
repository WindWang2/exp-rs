// tests/test_sar_kernels.cpp — Platform 3.0 SAR kernel numeric references
// (goal §6/§12): every formula is asserted against a hand-derived analytic
// value with the tolerance locked here. Drift beyond these bounds is a
// scientific regression, not a rounding artifact.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>

#include "processing/algorithms/sar/sar_calibration.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_speckle.h"
#include "processing/algorithms/sar/sar_terrain.h"
#include "processing/algorithms/sar/sar_terrain_geometry.h"
#include "processing/algorithms/sar/sar_texture.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace sicnu::sar;

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_kernels";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}
} // namespace

TEST_CASE( "dB/linear conversions follow the 10·log10 power convention",
           "[sar][calibration]" )
{
    ensureApp();
    REQUIRE( linearToDb( 1.0 ) == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( linearToDb( 10.0 ) == Approx( 10.0 ).margin( 1e-12 ) );
    REQUIRE( linearToDb( 0.001 ) == Approx( -30.0 ).margin( 1e-9 ) );
    // Roundtrip within float precision.
    for ( double db : { -25.0, -3.0, 0.5, 12.0 } )
        REQUIRE( linearToDb( dbToLinear( db ) ) == Approx( db ).margin( 1e-9 ) );
}

TEST_CASE( "DN calibration applies sigma0 = (DN² - noise) / A²",
           "[sar][calibration]" )
{
    ensureApp();
    // Sentinel-1-style: A = 2, DN = 4 → 16/4 = 4.0 linear power.
    REQUIRE( calibrateDn( 4.0, 2.0, 0.0 ) == Approx( 4.0 ).margin( 1e-12 ) );
    // Noise subtraction happens before scaling.
    REQUIRE( calibrateDn( 4.0, 2.0, 2.0 ) == Approx( 3.5 ).margin( 1e-12 ) );
    // dB output of a calibrated value: 10·log10(4) ≈ 6.0206 dB.
    REQUIRE( linearToDb( calibrateDn( 4.0, 2.0, 0.0 ) ) == Approx( 6.0206 ).margin( 1e-3 ) );
}

TEST_CASE( "Calibration and backscatter conversions propagate NaN and domain edges",
           "[sar][calibration][nodata]" )
{
    ensureApp();
    const double qnan = std::numeric_limits<double>::quiet_NaN();

    // NaN input propagates through every pure conversion (never 0, no throw):
    // the raster kernels rely on IEEE propagation to reach the NaN output
    // NoData convention.
    REQUIRE( std::isnan( calibrateDn( qnan, 2.0, 0.0 ) ) );
    REQUIRE( std::isnan( calibrateDn( 4.0, qnan, 0.0 ) ) );
    REQUIRE( std::isnan( linearToDb( qnan ) ) );
    REQUIRE( std::isnan( dbToLinear( qnan ) ) );
    REQUIRE( std::isnan( sigma0ToGamma0( qnan, 30.0 ) ) );
    REQUIRE( std::isnan( sigma0ToGamma0( 2.0, qnan ) ) );
    REQUIRE( std::isnan( sigma0ToBeta0( 2.0, qnan ) ) );

    // log10 domain edges are IEEE values, not exceptions: 0 power → -inf dB,
    // negative power → NaN dB.
    REQUIRE( std::isinf( linearToDb( 0.0 ) ) );
    REQUIRE( linearToDb( 0.0 ) < 0.0 );
    REQUIRE( std::isnan( linearToDb( -4.0 ) ) );

    // cos(90°) underflows to ~6.1e-17, so an exactly-vertical look direction
    // yields a huge-but-finite gamma0, not a pole: document the value class
    // downstream code must clamp (finishValue → output NoData).
    REQUIRE( sigma0ToGamma0( 2.0, 90.0 ) > 1e15 );
}

TEST_CASE( "Backscatter conversions use the local incidence angle",
           "[sar][backscatter]" )
{
    ensureApp();
    // θ = 60°: cos = 0.5 → gamma0 = 2·sigma0.
    REQUIRE( sigma0ToGamma0( 0.25, 60.0 ) == Approx( 0.5 ).margin( 1e-12 ) );
    // θ = 30°: sin = 0.5 → beta0 = 2·sigma0.
    REQUIRE( sigma0ToBeta0( 0.25, 30.0 ) == Approx( 0.5 ).margin( 1e-12 ) );
    // Roundtrip gamma0 ↔ sigma0.
    for ( double theta : { 20.0, 35.0, 50.0 } )
    {
        const double sigma0 = 0.37;
        REQUIRE( gamma0ToSigma0( sigma0ToGamma0( sigma0, theta ), theta )
                 == Approx( sigma0 ).margin( 1e-12 ) );
    }
}

TEST_CASE( "Slope and aspect follow Horn's method with documented conventions",
           "[sar][terrain]" )
{
    ensureApp();
    // Flat DEM: zero slope, aspect undefined (-1).
    const int w = 5;
    std::vector<float> flat( 25, 100.0f );
    const SlopeAspect flatSa = slopeAspectAt( flat.data(), w, 2, 2, 10.0, 1.0 );
    REQUIRE( flatSa.slopeDeg < 1e-9 );
    REQUIRE( flatSa.aspectDeg < 0.0 );

    // Eastward ramp dem[x][y] = 10·x with 10 m cells: dz/dx = 1 → 45° slope,
    // downhill faces west (azimuth 270°).
    std::vector<float> ramp( 25 );
    for ( int y = 0; y < 5; ++y )
        for ( int x = 0; x < 5; ++x )
            ramp[y * 5 + x] = 10.0f * x;
    const SlopeAspect sa = slopeAspectAt( ramp.data(), w, 2, 2, 10.0, 1.0 );
    REQUIRE( sa.slopeDeg == Approx( 45.0 ).margin( 1e-6 ) );
    REQUIRE( sa.aspectDeg == Approx( 270.0 ).margin( 1e-6 ) );

    // Row-increasing ramp (dem[y][x] = 10·y): in GDAL north-up rasters the
    // row axis grows southward, so height increases southward and the
    // downhill direction is NORTH (azimuth 0°).
    std::vector<float> rampN( 25 );
    for ( int y = 0; y < 5; ++y )
        for ( int x = 0; x < 5; ++x )
            rampN[y * 5 + x] = 10.0f * y;
    const SlopeAspect saN = slopeAspectAt( rampN.data(), w, 2, 2, 10.0, 1.0 );
    REQUIRE( saN.slopeDeg == Approx( 45.0 ).margin( 1e-6 ) );
    REQUIRE( saN.aspectDeg == Approx( 0.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Local incidence angle reduces to theta0 - slope on look-aligned facets",
           "[sar][terrain]" )
{
    ensureApp();
    // Facet tilted toward the radar (aspect == from-azimuth, i.e. the
    // direction the illumination comes FROM): cos θi = cos(θ0 − α).
    REQUIRE( localIncidenceAngle( 30.0, 0.0, 45.0, 0.0 ) == Approx( 15.0 ).margin( 1e-6 ) );
    // Facet tilted away: θi = θ0 + α.
    REQUIRE( localIncidenceAngle( 30.0, 180.0, 45.0, 0.0 ) == Approx( 75.0 ).margin( 1e-6 ) );
    // Cross-slope facet (aspect ⊥ look): cos θi = cosα·cosθ0.
    const double expect =
        std::acos( std::cos( 30.0 * M_PI / 180.0 ) * std::cos( 45.0 * M_PI / 180.0 ) ) *
        180.0 / M_PI;
    REQUIRE( localIncidenceAngle( 30.0, 90.0, 45.0, 0.0 ) == Approx( expect ).margin( 1e-6 ) );
    // Flat facets report the scene incidence unchanged.
    REQUIRE( localIncidenceAngle( 0.0, -1.0, 38.0, 0.0 ) == Approx( 38.0 ).margin( 1e-12 ) );
}

TEST_CASE( "Layover/shadow threshold catches far and away-facing facets",
           "[sar][terrain]" )
{
    ensureApp();
    const double cosMax = std::cos( 85.0 * M_PI / 180.0 );
    REQUIRE_FALSE( isLayoverOrShadow( 45.0, cosMax ) );
    REQUIRE_FALSE( isLayoverOrShadow( 84.999, cosMax ) );
    REQUIRE( isLayoverOrShadow( 85.0, cosMax ) );
    REQUIRE( isLayoverOrShadow( 95.0, cosMax ) );
    REQUIRE( isLayoverOrShadow( 180.0, cosMax ) );
}

TEST_CASE( "GLCM measures match hand-computed Haralick values", "[sar][texture]" )
{
    ensureApp();
    // 2×2 checkerboard with two quantization levels, horizontal offset:
    //   [[0, 1],
    //    [1, 0]]
    // Symmetric co-occurrence over offset (1,0): P[0,1] = P[1,0] = 0.25 of 2
    // raw pairs → normalized pair mass 0.5 each (total 1.0).
    //   contrast = 1²·(0.5+0.5) = 1
    //   dissimilarity = 1
    //   homogeneity = 2·(0.5 / 2) = 0.5
    //   energy = 0.25 + 0.25 = 0.5
    //   entropy = −2·0.5·ln 0.5 = ln 2
    const float window[4] = { 0.f, 1.f, 1.f, 0.f };
    const std::vector<GlcmMeasure> measures = {
        GlcmMeasure::Contrast,     GlcmMeasure::Dissimilarity,
        GlcmMeasure::Homogeneity,  GlcmMeasure::Energy,
        GlcmMeasure::Entropy,      GlcmMeasure::Correlation,
    };
    std::vector<float> values( measures.size(), 0.f );
    glcmMeasuresForWindow( window, 2, 2, 1, 0, measures, values.data() );

    REQUIRE( values[0] == Approx( 1.0 ).margin( 1e-9 ) );   // contrast
    REQUIRE( values[1] == Approx( 1.0 ).margin( 1e-9 ) );   // dissimilarity
    REQUIRE( values[2] == Approx( 0.5 ).margin( 1e-9 ) );   // homogeneity
    REQUIRE( values[3] == Approx( 0.5 ).margin( 1e-9 ) );   // energy
    REQUIRE( values[4] == Approx( std::log( 2.0 ) ).margin( 1e-9 ) ); // entropy
    // Correlation of the anti-correlated checkerboard is -1.
    REQUIRE( values[5] == Approx( -1.0 ).margin( 1e-9 ) );

    // Uniform window: zero texture (degenerate-span path).
    const float uniform[4] = { 3.f, 3.f, 3.f, 3.f };
    std::vector<float> uniformValues( measures.size(), -1.f );
    glcmMeasuresForWindow( uniform, 2, 4, 1, 0, measures, uniformValues.data() );
    REQUIRE( uniformValues[0] == Approx( 0.0 ).margin( 1e-12 ) ); // contrast
    REQUIRE( uniformValues[2] == Approx( 1.0 ).margin( 1e-12 ) ); // homogeneity
    REQUIRE( uniformValues[3] == Approx( 1.0 ).margin( 1e-12 ) ); // energy
    REQUIRE( uniformValues[4] == Approx( 0.0 ).margin( 1e-12 ) ); // entropy
}

TEST_CASE( "Refined Lee keeps homogeneous regions at their mean", "[sar][speckle]" )
{
    ensureApp();
    // 8×8 homogeneous halo tile: every window is identical → the MMSE estimate
    // collapses to the mean for any direction selection.
    GdalBlockStream::Tile tile;
    tile.width = tile.height = 4;
    tile.halo = 1;
    tile.bufferWidth = tile.bufferHeight = 6;
    std::vector<float> halo( 36, 5.0f );
    std::vector<float> out( 16, 0.f );
    refinedLeeTile( tile, halo.data(), out.data(), 3, 1 );
    for ( float v : out )
        REQUIRE( v == Approx( 5.0f ).margin( 1e-6 ) );
}

TEST_CASE( "Speckle methods parse their documented vocabulary", "[sar][speckle]" )
{
    ensureApp();
    bool ok = false;
    REQUIRE( speckleMethodFromString( "lee", &ok ) == SpeckleMethod::Lee );
    REQUIRE( ok );
    REQUIRE( speckleMethodFromString( "gamma_map", &ok ) == SpeckleMethod::GammaMap );
    REQUIRE( speckleMethodFromString( "refined_lee", &ok ) == SpeckleMethod::RefinedLee );
    REQUIRE( speckleMethodFromString( "multitemporal", &ok ) == SpeckleMethod::Multitemporal );
    speckleMethodFromString( "median", &ok );
    REQUIRE_FALSE( ok );
}

TEST_CASE( "SAR metadata helpers read and write the documented keys",
           "[sar][metadata]" )
{
    ensureApp();
    REQUIRE( normalizeCalibration( "SIGMA_NAUGHT" ) == "sigma0" );
    REQUIRE( normalizeCalibration( "beta" ) == "beta0" );
    REQUIRE( normalizeCalibration( "unknown" ).isEmpty() );
    REQUIRE( isSarRadiometricState( "Sigma0" ) );
    REQUIRE( isSarRadiometricState( "dn" ) );
    REQUIRE_FALSE( isSarRadiometricState( "surface_reflectance" ) );
}

TEST_CASE( "Antenna look azimuth derives from flight heading + side, and"
           " geometry uses the look azimuth (not the heading)",
           "[sar][terrain]" )
{
    ensureApp();
    using namespace sicnu::sar;

    // #785: heading (flight direction) and look azimuth are orthogonal.
    // Right-looking: look = heading + 90; left-looking: look = heading - 90.
    REQUIRE( lookAzimuthFromHeading( 0.0, true ) == Approx( 90.0 ).margin( 1e-12 ) );
    REQUIRE( lookAzimuthFromHeading( 0.0, false ) == Approx( 270.0 ).margin( 1e-12 ) );
    REQUIRE( lookAzimuthFromHeading( 190.0, true ) == Approx( 280.0 ).margin( 1e-12 ) );
    // Wraps to [0, 360).
    REQUIRE( lookAzimuthFromHeading( 350.0, true ) == Approx( 80.0 ).margin( 1e-12 ) );
    REQUIRE( lookAzimuthFromHeading( 10.0, false ) == Approx( 280.0 ).margin( 1e-12 ) );

    // Surface rising toward the EAST (dzdx > 0, dzdy = 0): facing east,
    // slope 45°. A sensor looking WEST from the east (look azimuth 270°)
    // sees the east-facing slope rise toward it → layover at steep grades;
    // looking EAST (look azimuth 90°) sees it fall away → normal/shadow
    // side. The heading is 180° (flying south) either way — feeding the
    // heading instead of the look azimuth (the #785 defect) would swap the
    // classes.
    const double dzdx = 1.0; // 45° eastward slope
    const double dzdy = 0.0;
    const double incidence = 30.0;

    // Surface rising eastward at 45°, sensor incidence θ0 = 30°.
    //   beam east (look 90°): the pixel's flank FACES the incoming beam
    //     (gLook = dz along beam travel = +tan45°) and 45° > 30°, so the
    //     range folds back → LAYOVER (classic rule: slope facing the radar
    //     steeper than the incidence angle).
    //   beam west (look 270°): the same pixel is on the far side
    //     (gLook = dz going west = −tan45°); −45° is neither > 30° (layover)
    //     nor < 30°−90° = −60° (shadow) → Normal.
    // Adversarial review FINDING: the pre-6.0 branch (alpha = atan(−gLook))
    // flagged exactly the swapped classes; these are the physical ones.
    const double lookWest = lookAzimuthFromHeading( 180.0, true );
    REQUIRE( lookWest == Approx( 270.0 ).margin( 1e-12 ) );
    const TerrainGeometryResult westSide =
        terrainGeometry( dzdx, dzdy, incidence, lookWest );
    CHECK( westSide.maskClass == TerrainMaskClass::Normal );

    const double lookEast = lookAzimuthFromHeading( 180.0, false );
    REQUIRE( lookEast == Approx( 90.0 ).margin( 1e-12 ) );
    const TerrainGeometryResult eastSide =
        terrainGeometry( dzdx, dzdy, incidence, lookEast );
    CHECK( eastSide.maskClass == TerrainMaskClass::Layover );

    // The two look sides must disagree — a 90° orthogonality bug (feeding
    // heading = 180° directly) makes them identical and masks the error.
    CHECK( westSide.localIncidenceDeg != eastSide.localIncidenceDeg );
}
