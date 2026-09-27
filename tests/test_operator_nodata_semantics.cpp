/***************************************************************************
 * test_operator_nodata_semantics.cpp — R4 NoData/mask semantics oracle
 *
 * Track 7 (R4 operator oracles). Every expectation is ANALYTICALLY
 * DERIVABLE; the derivation sits next to each assertion. Fixtures are
 * synthetic Float32/UInt16 GeoTIFFs with a declared -9999 (or NaN) sentinel.
 *
 * Families covered (matrix rows in
 * .planning/rs-operator-oracles-r4/NODATA_SEMANTIC_MATRIX.md):
 *
 *   rs:zonal_stats        — 17/100 sentinel pixels; closed-form moments
 *                           (mean 59, pop-σ² 574, median 59) exclude them
 *   rs:focal_stats        — sentinel neighbours excluded, sentinel centre
 *                           → NaN; window means stay closed-form
 *   rs:apply_mask         — masked pixels → declared NoData; pre-existing
 *                           sentinels pass through; downstream zonal mean
 *                           shifts by the closed form
 *   rs:qa_mask            — declared-NoData QA sample fails closed (mask=1)
 *   rs:spectral_derivative— declared sentinels NaN-ized before differencing
 *                           (R4 defect fix regression: they used to enter
 *                           the finite differences as real reflectance)
 *   rs:image_enhancement  — ratio path masks declared sentinels (R4 defect
 *                           fix regression: sentinel pair → garbage finite
 *                           ratio); output NoData is declared
 *   rs:image_enhancement  — stretch holes carry the input sentinel AND the
 *   (stretch)             — output band declares it (R4 declaration fix)
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "support/r4_operator_fixtures.h"

#include <QTemporaryDir>

#include <cmath>

using namespace r4fixtures;

// ---------------------------------------------------------------------------
// rs:zonal_stats — closed-form moments over 83 valid of 100 pixels
// ---------------------------------------------------------------------------

TEST_CASE( "zonal statistics exclude declared NoData from closed-form moments",
           "[r4][nodata][zonal]" )
{
    // Grid value = row-major index + 1 (1..100). The first 17 pixels (values
    // 1..17) are declared-NoData. The remaining 83 values are 18..100:
    //   count   = 83
    //   Σ       = 5050 − (1+..+17) = 5050 − 153 = 4897
    //   mean    = 4897 / 83 = 59            (83 · 59 = 4897)
    //   Σx²     = 338350 − 1785 = 336565    (Σ1..100² − Σ1..17²)
    //   pop-σ²  = 336565/83 − 59² = 4055 − 3481 = 574
    //   median  = 59 (middle of 18..100)
    //   min/max = 18 / 100
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = writeFloatRaster(
        dir.filePath( "grid.tif" ), 10, 10,
        { [&] {
            std::vector<float> v( 100 );
            for ( int i = 0; i < 100; ++i )
                v[static_cast<size_t>( i )] = static_cast<float>( i + 1 );
            for ( int i = 0; i < 17; ++i )
                v[static_cast<size_t>( i )] = static_cast<float>( kSentinel );
            return v;
        }() },
        true, kSentinel );
    const QString zones = writeZoneGeoJson( dir.filePath( "zones.geojson" ), "zone" );
    const QString csv = dir.filePath( "zonal.csv" );

    Json::Value params;
    params["input"] = raster.toStdString();
    params["vector"] = zones.toStdString();
    params["zoneField"] = "zone";
    params["output"] = csv.toStdString();
    const Json::Value result = runOperator( "rs:zonal_stats", params, dir.path().toStdString() );
    REQUIRE( result["rows"].asInt() == 1 );

    const auto rows = readCsv( csv );
    REQUIRE( rows.size() == 2 );
    REQUIRE( rows[0][0] == "zone_key" );
    // zone_key,band,valid_pixels,nodata_pixels,min,max,mean,stddev,median,median_truncated
    REQUIRE( rows[1][2] == "83" );   // valid pixels
    REQUIRE( rows[1][3] == "17" );   // nodata pixels
    REQUIRE( nearRel( std::stod( rows[1][4] ), 18.0 ) );               // min
    REQUIRE( nearRel( std::stod( rows[1][5] ), 100.0 ) );              // max
    REQUIRE( nearRel( std::stod( rows[1][6] ), 59.0 ) );               // mean (closed form)
    REQUIRE( nearRel( std::stod( rows[1][7] ), std::sqrt( 574.0 ) ) ); // pop-σ
    REQUIRE( nearRel( std::stod( rows[1][8] ), 59.0 ) );               // median
    REQUIRE( rows[1][9] == "0" );
}

// ---------------------------------------------------------------------------
// rs:focal_stats — NoData neighbours excluded, NoData centre → NaN
// ---------------------------------------------------------------------------

TEST_CASE( "focal mean excludes declared-NoData neighbours and NaNs invalid centres",
           "[r4][nodata][focal]" )
{
    // value = 10·row + col (0..99): for an interior pixel the 3×3 window is
    // centre-symmetric, so the 9-value mean equals the centre value.
    // With (5,5) = -9999:
    //   out(7,7): window rows/cols 6..8, 9 valid → 77
    //   out(4,4): window rows/cols 3..5 touches the sentinel → 8 valid:
    //             (33+34+35 + 43+44+45 + 53+54)/8 = 341/8 = 42.625
    //             (counting the sentinel as data would give ≈ −1067)
    //   out(5,5): invalid centre → NaN (declared on the output band)
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    auto ramp = [] {
        std::vector<float> v( 100 );
        for ( int r = 0; r < 10; ++r )
            for ( int c = 0; c < 10; ++c )
                v[static_cast<size_t>( r ) * 10 + c] = static_cast<float>( 10 * r + c );
        return v;
    };

    const QString clean = writeFloatRaster( dir.filePath( "clean.tif" ), 10, 10, { ramp() },
                                            true, kSentinel );
    Json::Value p1;
    p1["input"] = clean.toStdString();
    p1["output"] = dir.filePath( "focal_clean.tif" ).toStdString();
    p1["window"] = 3;
    p1["stat"] = "mean";
    runOperator( "rs:focal_stats", p1, dir.path().toStdString() );
    const auto cleanOut = readBand( dir.filePath( "focal_clean.tif" ), 1 );
    REQUIRE( nearRel( cleanOut[55], 55.0 ) ); // (5,5), no sentinels anywhere

    auto holey = ramp();
    holey[55] = static_cast<float>( kSentinel );
    const QString holeyPath = writeFloatRaster( dir.filePath( "holey.tif" ), 10, 10,
                                                { holey }, true, kSentinel );
    Json::Value p2;
    p2["input"] = holeyPath.toStdString();
    p2["output"] = dir.filePath( "focal_holey.tif" ).toStdString();
    p2["window"] = 3;
    p2["stat"] = "mean";
    runOperator( "rs:focal_stats", p2, dir.path().toStdString() );
    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "focal_holey.tif" ), 1, &hasNd, &declared );
    REQUIRE( nearRel( out[77], 77.0 ) );            // untouched interior
    REQUIRE( nearRel( out[44], 341.0 / 8.0 ) );     // sentinel neighbour excluded
    REQUIRE( std::isnan( out[55] ) );               // invalid centre → NaN
    REQUIRE( hasNd );
    REQUIRE( std::isnan( declared ) );              // output declares NaN
}

// ---------------------------------------------------------------------------
// rs:apply_mask — declared-NoData fill + pre-existing sentinel pass-through
// ---------------------------------------------------------------------------

TEST_CASE( "apply_mask writes declared NoData and shifts downstream zonal mean by the "
           "closed form",
           "[r4][nodata][mask]" )
{
    // value = index + 1 (1..100), declared NoData -9999. Mask covers columns
    // 0..2 (30 pixels): Σ_masked = Σ_r (30r + 1+2+3) = 1350 + 60 = 1410.
    // Pre-existing sentinel at (0,9) replaces value 10 and passes through
    // unmasked. Downstream zonal mean over the masked product:
    //   valid count = 100 − 30 − 1 = 69
    //   Σ_valid     = 5050 − 1410 − 10 = 3630
    //   mean        = 3630 / 69 = 52.6086956521739...
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v( 100 );
    for ( int i = 0; i < 100; ++i )
        v[static_cast<size_t>( i )] = static_cast<float>( i + 1 );
    v[9] = static_cast<float>( kSentinel ); // (0,9): value 10
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v },
                                             true, kSentinel );

    std::vector<uint8_t> mask( 100, 0 );
    for ( int r = 0; r < 10; ++r )
        for ( int c = 0; c < 3; ++c )
            mask[static_cast<size_t>( r ) * 10 + static_cast<size_t>( c )] = 1;
    const QString maskPath = writeByteMask( dir.filePath( "mask.tif" ), 10, 10, mask );

    Json::Value p;
    p["input"] = raster.toStdString();
    p["mask"] = maskPath.toStdString();
    p["output"] = dir.filePath( "masked.tif" ).toStdString();
    const Json::Value result = runOperator( "rs:apply_mask", p, dir.path().toStdString() );
    REQUIRE( nearRel( result["maskedPercent"].asDouble(), 30.0, 1e-9 ) );

    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "masked.tif" ), 1, &hasNd, &declared );
    REQUIRE( hasNd );
    REQUIRE( nearRel( declared, kSentinel ) );
    int maskedCount = 0;
    for ( int i = 0; i < 100; ++i )
    {
        const bool inMaskedCols = ( i % 10 ) < 3;
        if ( inMaskedCols )
        {
            ++maskedCount;
            REQUIRE( nearRel( out[static_cast<size_t>( i )], kSentinel ) );
        }
        else if ( i == 9 )
        {
            // Pre-existing sentinel passes through unmasked, unchanged.
            REQUIRE( nearRel( out[static_cast<size_t>( i )], kSentinel ) );
        }
        else
        {
            REQUIRE( nearRel( out[static_cast<size_t>( i )],
                              static_cast<double>( i + 1 ) ) );
        }
    }
    REQUIRE( maskedCount == 30 );

    // Downstream: zonal stats over the masked product carry the closed form.
    const QString zones = writeZoneGeoJson( dir.filePath( "zones.geojson" ), "zone" );
    Json::Value zp;
    zp["input"] = dir.filePath( "masked.tif" ).toStdString();
    zp["vector"] = zones.toStdString();
    zp["zoneField"] = "zone";
    zp["output"] = dir.filePath( "zonal.csv" ).toStdString();
    runOperator( "rs:zonal_stats", zp, dir.path().toStdString() );
    const auto rows = readCsv( dir.filePath( "zonal.csv" ) );
    REQUIRE( rows.size() == 2 );
    REQUIRE( rows[1][2] == "69" );  // valid
    REQUIRE( rows[1][3] == "31" );  // 30 masked + 1 pre-existing sentinel
    REQUIRE( nearRel( std::stod( rows[1][6] ), 3630.0 / 69.0 ) );
}

// ---------------------------------------------------------------------------
// rs:qa_mask — declared-NoData QA samples fail closed
// ---------------------------------------------------------------------------

TEST_CASE( "qa_mask fails closed on declared-NoData QA samples", "[r4][nodata][qa]" )
{
    // SCL words: 4 vegetation (clear), 8 cloud medium (masked), 3 cloud
    // shadow (masked), 255 undeclared class with the band declaring
    // NoData=255 — an unreadable classification word masks even though
    // cloud_and_shadow does not select class 255 (F-OPS-3 fail-closed).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString qa = writeU16Raster( dir.filePath( "scl.tif" ), 4, 1,
                                       { 4, 8, 3, 255 }, true, 255.0 );
    Json::Value p;
    p["input"] = qa.toStdString();
    p["output"] = dir.filePath( "mask.tif" ).toStdString();
    p["qa_band"] = 1;
    p["source"] = "sentinel2_scl";
    p["mask"] = "cloud_and_shadow";
    const Json::Value result = runOperator( "rs:qa_mask", p, dir.path().toStdString() );
    REQUIRE( result["totalPixels"].asInt() == 4 );

    const auto mask = readByteBand( dir.filePath( "mask.tif" ), 1 );
    REQUIRE( mask[0] == 0 );  // vegetation stays clear
    REQUIRE( mask[1] == 1 );  // cloud medium
    REQUIRE( mask[2] == 1 );  // cloud shadow
    REQUIRE( mask[3] == 1 );  // declared NoData → fail-closed masked
}

// ---------------------------------------------------------------------------
// rs:spectral_derivative — declared sentinels NaN-ize before differencing
// ---------------------------------------------------------------------------

TEST_CASE( "spectral derivative treats declared sentinels like NaN",
           "[r4][nodata][derivative]" )
{
    // 4 bands on wavelengths 100/200/300/400 nm; spectrum v_b = 1 + λ_b/100
    // = 2,3,4,5 → every first derivative is (v[i+1]−v[i])/100 = 0.01.
    // A pixel with band 3 = -9999 must produce NaN in the derivatives that
    // touch band 3 (positions 1 and 2) while position 0 stays 0.01 — before
    // the R4 fix the sentinel entered as reflectance (−100.02 / 100.04).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const double wl[4] = { 100.0, 200.0, 300.0, 400.0 };
    std::vector<std::vector<float>> bands;
    for ( int b = 1; b <= 4; ++b )
    {
        std::vector<float> v( 9, static_cast<float>( 1.0 + wl[b - 1] / 100.0 ) );
        if ( b == 3 )
            v[5] = static_cast<float>( kSentinel ); // pixel (2,2) of the 3×3 grid
        bands.push_back( std::move( v ) );
    }
    const QString raster = writeFloatRaster( dir.filePath( "spec.tif" ), 3, 3, bands, true,
                                             kSentinel );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["output"] = dir.filePath( "d1.tif" ).toStdString();
    p["order"] = 1;
    Json::Value axis( Json::arrayValue );
    for ( const double w : wl )
        axis.append( w );
    p["wavelengths"] = axis;
    runOperator( "rs:spectral_derivative", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto d1 = readBand( dir.filePath( "d1.tif" ), 1, &hasNd, &declared );
    const auto d2 = readBand( dir.filePath( "d1.tif" ), 2 );
    const auto d3 = readBand( dir.filePath( "d1.tif" ), 3 );
    for ( size_t i = 0; i < d1.size(); ++i )
    {
        if ( i == 5 )
            continue;
        REQUIRE( nearRel( d1[i], 0.01 ) );
    }
    REQUIRE( nearRel( d1[5], 0.01 ) );  // band pair (1,2) never touches band 3
    REQUIRE( std::isnan( d2[5] ) );     // pair (2,3) touches the sentinel
    REQUIRE( std::isnan( d3[5] ) );     // pair (3,4) touches the sentinel
    REQUIRE( hasNd );
    REQUIRE( std::isnan( declared ) );
}

// ---------------------------------------------------------------------------
// rs:image_enhancement (ratio) — declared sentinels never produce a ratio
// ---------------------------------------------------------------------------

TEST_CASE( "image_enhancement ratio masks declared sentinels and declares NaN output",
           "[r4][nodata][ratio]" )
{
    // band 1 = 6, band 2 = 3 → ratio 2 everywhere; band 2 = -9999 at (2,2)
    // must yield NaN (pre-R4: 6/−9999 ≈ −6.0006e−3, a finite wrong answer)
    // and the output band must DECLARE NaN (pre-R4: no declaration at all).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> b1( 9, 6.0f );
    std::vector<float> b2( 9, 3.0f );
    b2[5] = static_cast<float>( kSentinel );
    const QString raster = writeFloatRaster( dir.filePath( "pair.tif" ), 3, 3, { b1, b2 },
                                             true, kSentinel );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["output"] = dir.filePath( "ratio.tif" ).toStdString();
    p["method"] = "ratio_ihs";
    p["transform"] = "ratio";
    p["band1"] = 1;
    p["band2"] = 2;
    runOperator( "rs:image_enhancement", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "ratio.tif" ), 1, &hasNd, &declared );
    for ( size_t i = 0; i < out.size(); ++i )
    {
        if ( i == 5 )
            continue;
        REQUIRE( nearRel( out[i], 2.0 ) );
    }
    REQUIRE( std::isnan( out[5] ) );
    REQUIRE( hasNd );
    REQUIRE( std::isnan( declared ) );
}

// ---------------------------------------------------------------------------
// rs:image_enhancement (stretch) — declared sentinel holes
// ---------------------------------------------------------------------------

TEST_CASE( "stretch output declares the sentinel holes it writes",
           "[r4][nodata][stretch]" )
{
    // v = index (0..99) with (0,0) = -9999. Statistics exclude the sentinel:
    // min 1, max 99, so v=50 maps to (50−1)/98·255 = 127.5. The hole keeps
    // the input sentinel AND the output band declares it (pre-R4: written
    // but undeclared).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v( 100 );
    for ( int i = 0; i < 100; ++i )
        v[static_cast<size_t>( i )] = static_cast<float>( i );
    v[0] = static_cast<float>( kSentinel );
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v }, true,
                                             kSentinel );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["output"] = dir.filePath( "stretched.tif" ).toStdString();
    p["method"] = "stretch";
    p["stretchType"] = "linear";
    runOperator( "rs:image_enhancement", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "stretched.tif" ), 1, &hasNd, &declared );
    REQUIRE( hasNd );
    REQUIRE( nearRel( declared, kSentinel ) );
    REQUIRE( nearRel( out[0], kSentinel ) );  // hole value
    REQUIRE( nearRel( out[50], ( 50.0 - 1.0 ) / 98.0 * 255.0, 1e-6 ) );
}
