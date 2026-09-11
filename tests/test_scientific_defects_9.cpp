// test_scientific_defects_9.cpp — Scientific Algorithms 9.0 (M0): regression
// tests for the verified scientific defects #848, #853, #854, #855, #856 and
// #873. Every case is written so the pre-fix code fails it (old-code-fails /
// new-code-passes): the assertions pin the closed-form or contract behavior
// the defect violated, not the implementation.

#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/sar/sar_terrain.h"
#include "processing/contracts/scientific_contracts.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace TerrainFlow;
using sicnu::processing::contracts::NumericScaleRegime;
using sicnu::processing::contracts::domainFromDeclaredScale;

namespace
{
constexpr float kNodata = -9999.0f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
} // namespace

// ============================================================================
// #848 — priority-flood fill on rasters whose rectangular border is NoData
// ============================================================================

TEST_CASE( "fillDepressions fills interior pits behind a NoData border (#848)",
           "[terrain][flow][nodata][issue848]" )
{
    // 5×5: the outer ring is NoData (a reprojected/clipped DEM), the inner
    // 3×3 is a plateau at 10 with a pit of 2 in the centre. The only drain
    // boundary is NoData adjacency. Pre-fix, the flood queue never received
    // a seed (no valid cell sits on the rectangular rim), the pit survived
    // at 2 and silently truncated downstream D8 routing.
    constexpr int kW = 5;
    constexpr int kH = 5;
    std::vector<float> dem( static_cast<size_t>( kW ) * kH, kNodata );
    for ( int y = 1; y <= 3; ++y )
        for ( int x = 1; x <= 3; ++x )
            dem[static_cast<size_t>( y ) * kW + x] = 10.0f;
    dem[static_cast<size_t>( 2 ) * kW + 2] = 2.0f; // interior pit

    std::vector<float> filled( static_cast<size_t>( kW ) * kH );
    REQUIRE( fillDepressions( dem.data(), filled.data(), kW, kH, kNodata ) );

    // The pit fills to the spill elevation of the NoData-adjacent seeds
    // (10 here): no interior depression survives the fill. Pre-fix it
    // stayed at 2.
    REQUIRE( filled[static_cast<size_t>( 2 ) * kW + 2] == Catch::Approx( 10.0f ) );
    // Seeds and NoData stay untouched.
    REQUIRE( filled[static_cast<size_t>( 1 ) * kW + 1] == Catch::Approx( 10.0f ) );
    REQUIRE( filled[0] == kNodata );
    REQUIRE( filled[static_cast<size_t>( 4 ) * kW + 4] == kNodata );

    // Downstream coherence on the filled surface: the interior is now a
    // fill flat draining across NoData (barrier), so D8 terminates — every
    // interior cell is its own sink (direction 0, self-inclusive acc 1),
    // and the #783 sentinel guarantees keep holding.
    std::vector<float> dir( static_cast<size_t>( kW ) * kH );
    std::vector<float> acc( static_cast<size_t>( kW ) * kH );
    REQUIRE( flowDirections( filled.data(), dir.data(), kW, kH, kNodata ) );
    REQUIRE( flowAccumulation( dir.data(), acc.data(), kW, kH, filled.data(), kNodata ) );
    for ( int y = 1; y <= 3; ++y )
        for ( int x = 1; x <= 3; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * kW + x;
            REQUIRE( dir[i] == 0.0f );
            REQUIRE( acc[i] == Catch::Approx( 1.0f ) );
        }
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            const bool border = x == 0 || y == 0 || x == kW - 1 || y == kH - 1;
            if ( border )
                REQUIRE( acc[static_cast<size_t>( y ) * kW + x] == kNodata );
        }
}

TEST_CASE( "fillDepressions spills a pit to the LOWEST NoData-adjacent seed (#848)",
           "[terrain][flow][nodata][issue848]" )
{
    // Full NoData border again, but non-uniform interior: a ring with two
    // notches at 5 and a pit of 2. The pit must fill to 5 — the elevation
    // of the lowest NoData-adjacent seed it can spill across — not to the
    // ring's maximum, and (pre-fix) not stay at 2.
    constexpr int kW = 5;
    constexpr int kH = 5;
    std::vector<float> dem( static_cast<size_t>( kW ) * kH, kNodata );
    const float interior[3][3] = {
        { 7, 5, 7 },
        { 7, 2, 7 },
        { 7, 5, 7 },
    };
    for ( int y = 0; y < 3; ++y )
        for ( int x = 0; x < 3; ++x )
            dem[static_cast<size_t>( y + 1 ) * kW + ( x + 1 )] = interior[y][x];

    std::vector<float> filled( static_cast<size_t>( kW ) * kH );
    REQUIRE( fillDepressions( dem.data(), filled.data(), kW, kH, kNodata ) );
    REQUIRE( filled[static_cast<size_t>( 2 ) * kW + 2] == Catch::Approx( 5.0f ) );
    REQUIRE( filled[static_cast<size_t>( 1 ) * kW + 2] == Catch::Approx( 5.0f ) ); // seed
    REQUIRE( filled[static_cast<size_t>( 1 ) * kW + 1] == Catch::Approx( 7.0f ) ); // seed

    // D8 on the filled surface: the filled pit and the two notch seeds are
    // flats at 5 (sinks); the four 7-corners drain into the adjacent notch
    // or pit flats; the 7-edge cells drain into the pit.
    std::vector<float> dir( static_cast<size_t>( kW ) * kH );
    std::vector<float> acc( static_cast<size_t>( kW ) * kH );
    REQUIRE( flowDirections( filled.data(), dir.data(), kW, kH, kNodata ) );
    REQUIRE( flowAccumulation( dir.data(), acc.data(), kW, kH, filled.data(), kNodata ) );
    REQUIRE( dir[static_cast<size_t>( 2 ) * kW + 2] == 0.0f );
    // Pit collects its two 7-edge upstream neighbours (E/W edges of row 2).
    REQUIRE( acc[static_cast<size_t>( 2 ) * kW + 2] == Catch::Approx( 3.0f ) );
    // Each notch seed collects its two 7-corner upstream neighbours.
    REQUIRE( acc[static_cast<size_t>( 1 ) * kW + 2] == Catch::Approx( 3.0f ) );
    REQUIRE( acc[static_cast<size_t>( 3 ) * kW + 2] == Catch::Approx( 3.0f ) );
}

// ============================================================================
// #853 — UB-free float→int casts on D8 direction buffers
// ============================================================================

TEST_CASE( "flowAccumulation classifies Float32 sentinels in dir as NoData (#853)",
           "[terrain][flow][ub][issue853]" )
{
    // A dir buffer holding GDAL Float32 sentinels: casting -3.4028235e38f
    // (or 1e30f) to int is undefined behavior ([conv.fpint]). The pre-fix
    // code cast before any range validation.
    constexpr int kW = 4;
    constexpr int kH = 1;
    constexpr float kHugeSentinel = -3.4028235e38f;
    std::vector<float> dir = { 1.0f, kHugeSentinel, 1.0f, 0.0f };
    std::vector<float> acc( 4 );
    REQUIRE( flowAccumulation( dir.data(), acc.data(), kW, kH ) );
    // Sentinel cell: excluded from the graph, carries its own value out.
    REQUIRE( acc[1] == kHugeSentinel );
    // Cell 0 drains east into the sentinel (NoData) — a terminal cell.
    REQUIRE( acc[0] == Catch::Approx( 1.0f ) );
    // Cell 2 drains east into the sink at 3: acc[3] = 1 + 1.
    REQUIRE( acc[2] == Catch::Approx( 1.0f ) );
    REQUIRE( acc[3] == Catch::Approx( 2.0f ) );

    // A finite-but-huge positive value is equally out of int range.
    std::vector<float> dir2 = { 1e30f, 0.0f };
    std::vector<float> acc2( 2 );
    REQUIRE( flowAccumulation( dir2.data(), acc2.data(), 2, 1 ) );
    REQUIRE( acc2[0] == 1e30f );
    REQUIRE( acc2[1] == Catch::Approx( 1.0f ) );
}

TEST_CASE( "watershedLabels terminates at out-of-range direction values without UB (#853)",
           "[terrain][watershed][ub][issue853]" )
{
    // Descending-east 5×1 with a corrupted (finite, huge) direction at
    // (3,0): the label BFS from pour (4,0) needs (3,0) to drain WEST into
    // it; the garbage direction terminates the path instead of being cast.
    constexpr int kW = 5;
    std::vector<float> dir = { 1.0f, 1.0f, 1.0f, 1e30f, 0.0f };
    std::vector<float> labels;
    REQUIRE( watershedLabels( dir.data(), kW, 1, { { 4, 0 } }, &labels ) );
    // Cells 0..2 point INTO the corrupted cell, whose own direction is
    // undecodable: their flow paths end there, so they never reach the
    // pour point and stay unlabelled (exactly like a NoData barrier).
    REQUIRE( labels[0] == 0.0f );
    REQUIRE( labels[1] == 0.0f );
    REQUIRE( labels[2] == 0.0f );
    REQUIRE( labels[3] == 0.0f );
    REQUIRE( labels[4] == 1.0f );
}

// ============================================================================
// #855 — Horn gradients on anisotropic pixel spacing (SAR terrain)
// ============================================================================

TEST_CASE( "slopeAspectAt normalizes each axis by its own spacing (#855)",
           "[sar][terrain][anisotropic][issue855]" )
{
    // DEM values (meters): z = 4·col + 3·row on a grid with cellX = 2 m,
    // cellY = 1 m (2:1 anisotropy, a common slant/ground-range ratio).
    // True gradients: dz/dx = 4/2 = 2, dz/dy = 3/1 = 3.
    //   slope   = atan(sqrt(2² + 3²))   ≈ 74.475°
    //   aspect  = atan2(−2, 3) + 360    ≈ 326.310°
    // The pre-fix code averaged the spacing to 1.5 m, computing
    // dz/dx = 2.667, dz/dy = 2 — slope 73.30° and, worse, aspect 306.87°
    // (a 19.4° rotation of the downslope azimuth).
    constexpr int kBuf = 4; // 3×3 window padded by 1
    std::vector<float> dem( static_cast<size_t>( kBuf ) * kBuf );
    for ( int y = 0; y < kBuf; ++y )
        for ( int x = 0; x < kBuf; ++x )
            dem[static_cast<size_t>( y ) * kBuf + x] =
                static_cast<float>( 4 * x + 3 * y );

    const sicnu::sar::SlopeAspect sa =
        sicnu::sar::slopeAspectAt( dem.data(), kBuf, 1, 1, 2.0, 1.0, 1.0 );
    REQUIRE( sa.valid );
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    REQUIRE( sa.slopeDeg ==
             Catch::Approx( std::atan( std::sqrt( 4.0 + 9.0 ) ) / kDegToRad )
                 .margin( 1e-9 ) );
    const double aspect = std::atan2( -2.0, 3.0 ) / kDegToRad;
    REQUIRE( sa.aspectDeg ==
             Catch::Approx( aspect < 0 ? aspect + 360.0 : aspect ).margin( 1e-9 ) );

    // East-facing plane on the same anisotropic grid: z = 4·col.
    // dz/dx = 2 → slope atan(2) ≈ 63.435°; the averaged-spacing code produced
    // atan(2.667) ≈ 69.44° (skewed by the axis-averaging).
    std::vector<float> demE( static_cast<size_t>( kBuf ) * kBuf );
    for ( int y = 0; y < kBuf; ++y )
        for ( int x = 0; x < kBuf; ++x )
            demE[static_cast<size_t>( y ) * kBuf + x] = static_cast<float>( 4 * x );
    const sicnu::sar::SlopeAspect saE =
        sicnu::sar::slopeAspectAt( demE.data(), kBuf, 1, 1, 2.0, 1.0, 1.0 );
    REQUIRE( saE.valid );
    REQUIRE( saE.slopeDeg == Catch::Approx( std::atan( 2.0 ) / kDegToRad ).margin( 1e-9 ) );
    // Downslope is west (270°) — the rotation bug showed up here as ~283°.
    REQUIRE( saE.aspectDeg == Catch::Approx( 270.0 ).margin( 1e-9 ) );

    // Isotropic spacing keeps the historical behavior bit-for-bit.
    std::vector<float> demI( static_cast<size_t>( kBuf ) * kBuf );
    for ( int y = 0; y < kBuf; ++y )
        for ( int x = 0; x < kBuf; ++x )
            demI[static_cast<size_t>( y ) * kBuf + x] = static_cast<float>( 2 * x );
    const sicnu::sar::SlopeAspect saI =
        sicnu::sar::slopeAspectAt( demI.data(), kBuf, 1, 1, 10.0, 10.0, 1.0 );
    REQUIRE( saI.slopeDeg == Catch::Approx( std::atan( 0.2 ) / kDegToRad ).margin( 1e-9 ) );
    REQUIRE( saI.aspectDeg == Catch::Approx( 270.0 ).margin( 1e-9 ) );
}

// ============================================================================
// #854 — per-band NoData on the SAR flatten output (gamma0 + validity mask)
// ============================================================================

namespace
{

// Run rs:sar_terrain_flatten on the given sigma0/DEM fixtures and open the
// result. Shared by the #854 and #855 E2E cases.
std::unique_ptr<GdalDatasetWrapper>
runFlatten( QTemporaryDir &dir, sicnu::testing::RsSyntheticRasterBuilder sigma,
            sicnu::testing::RsSyntheticRasterBuilder dem, double incidenceDeg,
            double lookAzimuthDeg )
{
    const QString sigmaPath = sigma.writeToDisk( dir.filePath( "sigma0.tif" ) );
    REQUIRE( !sigmaPath.isEmpty() );
    const QString demPath = dem.writeToDisk( dir.filePath( "dem.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:sar_terrain_flatten" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext context;
    Json::Value params( Json::objectValue );
    params["input"] = sigmaPath.toStdString();
    params["output"] = dir.filePath( "gamma0.tif" ).toStdString();
    params["dem"] = demPath.toStdString();
    params["band"] = 1;
    params["incidenceDeg"] = incidenceDeg;
    params["lookAzimuthDeg"] = lookAzimuthDeg;
    REQUIRE_NOTHROW( op->run( params, context ) );

    auto out = std::make_unique<GdalDatasetWrapper>();
    REQUIRE( out->open( dir.filePath( "gamma0.tif" ) ) );
    return out;
}

} // namespace

TEST_CASE( "rs:sar_terrain_flatten declares the Byte mask sentinel 255 (#854)",
           "[sar][operator][nodata][issue854]" )
{
    using namespace sicnu::testing;
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 6×6 sigma0 (linear power) with one NaN pixel — that pixel is what the
    // kernel flags 255 in the validity band; the DEM is a smooth ramp.
    constexpr int kW = 6;
    constexpr int kH = 6;
    RsSyntheticRasterBuilder sigma( kW, kH, 1 );
    sigma.withCrs( "EPSG:32650" );
    sigma.withGeoTransform( 500000.0, 10.0, 4000000.0, -20.0 );
    sigma.withConstantValue( 1, 0.25f );
    sigma.withPixel( 1, 4, 4, std::numeric_limits<float>::quiet_NaN() );

    RsSyntheticRasterBuilder dem( kW, kH, 1 );
    dem.withCrs( "EPSG:32650" );
    dem.withGeoTransform( 500000.0, 10.0, 4000000.0, -20.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            dem.withPixel( 1, x, y, static_cast<float>( 5 * x + 5 * y ) );

    std::unique_ptr<GdalDatasetWrapper> out = runFlatten( dir, sigma, dem, 30.0, 270.0 );
    REQUIRE( out->bandCount() == 2 );

    // Band 2 (the validity mask: 1 valid / 0 layover-shadow / 255 nodata)
    // must carry its integer sentinel on read-back. Pre-fix, the dataset-
    // wide NaN left the mask's 255 pixels readable as valid foreground.
    bool hasNd2 = false;
    const double nd2 = out->bandNoDataValue( 2, &hasNd2 );
    REQUIRE( hasNd2 );
    REQUIRE( nd2 == 255.0 );

    // The invalid sigma0 pixel carries mask value 255; a valid pixel carries 1.
    // (The mask band is a Float32 band carrying byte-valued 0/1/255 — see the
    // sar_terrain.h contract note.)
    std::vector<float> mask( static_cast<size_t>( kW ) * kH );
    REQUIRE( out->readBandData( 2, mask.data(), kW, kH ) );
    REQUIRE( mask[static_cast<size_t>( 4 ) * kW + 4] == 255.0f );
    REQUIRE( mask[0] == 1.0f );

    // gamma0 NoData itself is IEEE NaN in the data (self-describing on every
    // format). Format note: GeoTIFF serializes ONE GDAL_NODATA tag per
    // dataset — writing NaN on band 1 and 255 on band 2 in that order leaves
    // the mask sentinel as the persisted tag, which is the unambiguous
    // choice (a NaN tag would make the mask's 255 read back as valid).
    std::vector<float> gamma( static_cast<size_t>( kW ) * kH );
    REQUIRE( out->readBandData( 1, gamma.data(), kW, kH ) );
    REQUIRE( std::isnan( gamma[static_cast<size_t>( 4 ) * kW + 4] ) );
    REQUIRE( std::isfinite( gamma[0] ) );
}

// ============================================================================
// #855 E2E — anisotropic grid RTC through the production operator path
// ============================================================================

TEST_CASE( "rs:sar_terrain_flatten gamma0 matches the closed form on a 2:1 anisotropic grid (#855)",
           "[sar][operator][anisotropic][issue855]" )
{
    using namespace sicnu::testing;
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 2:1 anisotropic ground spacing: cellX = 10 m (gt[1]), cellY = 20 m
    // (gt[5]). DEM z = 20·x + 10·y (meters) ⇒ per-meter gradients
    // dz/dx = 20/10 = 2, dz/dy = 10/20 = 0.5. The averaged-spacing code
    // (cell = 15 m) computed dz/dx = 4/3, dz/dy = 2/3 — different facet,
    // different gamma0.
    constexpr int kW = 6;
    constexpr int kH = 6;
    RsSyntheticRasterBuilder sigma( kW, kH, 1 );
    sigma.withCrs( "EPSG:32650" );
    sigma.withGeoTransform( 500000.0, 10.0, 4000000.0, -20.0 );
    sigma.withConstantValue( 1, 0.25f );

    RsSyntheticRasterBuilder dem( kW, kH, 1 );
    dem.withCrs( "EPSG:32650" );
    dem.withGeoTransform( 500000.0, 10.0, 4000000.0, -20.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            dem.withPixel( 1, x, y, static_cast<float>( 20 * x + 10 * y ) );

    std::unique_ptr<GdalDatasetWrapper> out = runFlatten( dir, sigma, dem, 30.0, 0.0 );

    std::vector<float> gamma( static_cast<size_t>( kW ) * kH );
    REQUIRE( out->readBandData( 1, gamma.data(), kW, kH ) );

    // Closed form (independent of the kernel): Horn facet on the anisotropic
    // spacing, local incidence from the look geometry, gamma0 = σ0·cosθ0/cosθi.
    const double dzdx = 20.0 / 10.0; // per meter
    const double dzdy = 10.0 / 20.0;
    const double slope = std::atan( std::sqrt( dzdx * dzdx + dzdy * dzdy ) );
    double aspect = std::atan2( -dzdx, dzdy );
    if ( aspect < 0.0 )
        aspect += 2.0 * 3.14159265358979323846;
    const double kDegToRad = 3.14159265358979323846 / 180.0;
    const double theta0 = 30.0 * kDegToRad;
    const double fromAzimuth = 180.0 * kDegToRad; // look 0° + 180°
    const double cosThetaI =
        std::cos( slope ) * std::cos( theta0 ) +
        std::sin( slope ) * std::sin( theta0 ) * std::cos( aspect - fromAzimuth );
    REQUIRE( cosThetaI > 0.0 ); // facet is illuminated, mask must be 1
    const double expectedGamma = 0.25 * std::cos( theta0 ) / cosThetaI;

    // A comfortably interior pixel (away from border falloff of the window).
    const size_t probe = static_cast<size_t>( 3 ) * kW + 3;
    INFO( "gamma0=" << gamma[probe] << " expected=" << expectedGamma );
    REQUIRE( gamma[probe] ==
             Catch::Approx( static_cast<float>( expectedGamma ) ).margin( 1e-3f ) );

    // The validity mask stays 1 on this illuminated facet (byte-valued
    // Float32 band — see the #854 case).
    std::vector<float> mask( static_cast<size_t>( kW ) * kH );
    REQUIRE( out->readBandData( 2, mask.data(), kW, kH ) );
    REQUIRE( mask[probe] == 1.0f );
}

// ============================================================================
// #873 — declared numeric scale must be finite
// ============================================================================

TEST_CASE( "domainFromDeclaredScale refuses non-finite scales (#873)",
           "[contracts][scale][issue873]" )
{
    // +Inf used to pass `> 0.0`, dividing every pixel by +Inf — i.e. by zero.
    const sicnu::processing::contracts::NumericDomainContract infDomain =
        domainFromDeclaredScale( std::numeric_limits<double>::infinity() );
    REQUIRE( std::isfinite( infDomain.divisor ) );
    REQUIRE( infDomain.divisor == 1.0 );
    REQUIRE( infDomain.regime == NumericScaleRegime::UnitReflectance );

    // NaN is equally not a declaration (was already safe — pin it).
    const sicnu::processing::contracts::NumericDomainContract nanDomain =
        domainFromDeclaredScale( std::numeric_limits<double>::quiet_NaN() );
    REQUIRE( nanDomain.divisor == 1.0 );
    REQUIRE( nanDomain.regime == NumericScaleRegime::UnitReflectance );

    // Negative scales are not scales.
    const sicnu::processing::contracts::NumericDomainContract negDomain =
        domainFromDeclaredScale( -10000.0 );
    REQUIRE( negDomain.divisor == 1.0 );
    REQUIRE( negDomain.regime == NumericScaleRegime::UnitReflectance );

    // Genuine declarations are still honored verbatim.
    const sicnu::processing::contracts::NumericDomainContract dn =
        domainFromDeclaredScale( 10000.0 );
    REQUIRE( dn.isDnScale() );
    REQUIRE( dn.divisor == 10000.0 );
}

// ============================================================================
// #856 — undeclared negative sentinels must not flip the numeric domain
// ============================================================================

TEST_CASE( "rs:spectral_index EVI ignores undeclared negative sentinels in the scale probe (#856)",
           "[spectral][operator][scale][issue856]" )
{
    using namespace sicnu::testing;
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Unit-reflectance scene with an UNDECLARED -9999 sentinel corner: the
    // pre-fix probe took the sentinel's absolute magnitude as DN-scale
    // evidence and divided every band by the canonical DN divisor,
    // collapsing EVI's additive constants to ~0.
    constexpr int kW = 48;
    constexpr int kH = 48;
    RsSyntheticRasterBuilder b( kW, kH, 3 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 500000.0, 10.0, 4000000.0, -10.0 );
    b.withConstantValue( 1, 0.10f ); // blue
    b.withConstantValue( 2, 0.20f ); // red
    b.withConstantValue( 3, 0.80f ); // nir
    b.withPixel( 1, 0, 0, -9999.0f );
    b.withPixel( 2, 0, 0, -9999.0f );
    b.withPixel( 3, 0, 0, -9999.0f );
    const QString inputPath = b.writeToDisk( dir.filePath( "reflectance.tif" ) );
    REQUIRE( !inputPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    RSOperatorContext context;
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = dir.filePath( "evi.tif" ).toStdString();
    params["index"] = "EVI";
    params["nir"] = 3;
    params["red"] = 2;
    params["blue"] = 1;
    REQUIRE_NOTHROW( op->run( params, context ) );

    GdalDatasetWrapper out;
    REQUIRE( out.open( dir.filePath( "evi.tif" ) ) );
    std::vector<float> evi( static_cast<size_t>( kW ) * kH );
    REQUIRE( out.readBandData( 1, evi.data(), kW, kH ) );

    // Closed-form EVI on the constant background (unit reflectance):
    // 2.5·(0.8 − 0.2) / (0.8 + 6·0.2 − 7.5·0.1 + 1) = 1.5 / 2.25 ≈ 0.6667.
    const double expected =
        2.5 * ( 0.8 - 0.2 ) / ( 0.8 + 6.0 * 0.2 - 7.5 * 0.1 + 1.0 );
    const size_t probe = static_cast<size_t>( 24 ) * kW + 24;
    INFO( "evi=" << evi[probe] << " expected=" << expected );
    REQUIRE( evi[probe] == Catch::Approx( static_cast<float>( expected ) ).margin( 1e-4f ) );
}
