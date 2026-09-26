/***************************************************************************
 * test_known_answer_corpus_r4.cpp — known-answer corpus, R4 expansion
 *
 * Track 7 (R4 operator oracles). Companion to test_known_answer_corpus.cpp
 * (7.0) and test_known_answer_corpus_8.cpp (8.0): operator-level end-to-end
 * analytic truths for families that previously had kernel-only or no
 * coverage. Every expectation is ANALYTICALLY DERIVABLE — the derivation
 * sits next to each assertion; implementation back-calculation is refused.
 *
 *   operator              | invariant
 *   ----------------------+--------------------------------------------------
 *   rs:threshold_raster   | manual threshold: mask = (v >= t), NaN → 255
 *   rs:sar_calibrate      | sigma0 = DN²/A² (linear_power), noise 0
 *   rs:continuum_removal  | convex-hull envelope on a triangle spectrum:
 *                         | hull vertices → CR = 1, interior → v/hull
 *   rs:spectral_resample  | linear interpolation onto explicit targets;
 *                         | out-of-range targets → NaN (#445)
 *   rs:spectral_similarity| SAM label of axis-separated references; declared
 *                         | sentinel pixels stay unlabelled (R4 fix)
 *   rs:mosaic             | last-valid-wins overlap, NoData background
 *   rs:extract_bands      | verbatim copy + sentinel re-declaration
 *   rs:apply_mask         | explicit no_data on undeclared bands is declared
 *                         | on the output (typed-refusal counterpart)
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "support/r4_operator_fixtures.h"

#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <cmath>
#include <string>
#include <vector>

using namespace r4fixtures;

namespace
{
/// Float32 GeoTIFF with per-band WAVELENGTH metadata stamped (nm).
QString writeSpectrumRaster( const QString &path, const std::vector<double> &wavelengthsNm,
                             const std::vector<float> &spectrum, bool hasNoData,
                             double nodata )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const int bands = static_cast<int>( wavelengthsNm.size() );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 1, 1, bands,
                                  GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    setGrid( ds, 1 );
    for ( int b = 1; b <= bands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b );
        const QString wl = QString::number( wavelengthsNm[static_cast<size_t>( b - 1 )] );
        GDALSetMetadataItem( band, "WAVELENGTH", wl.toUtf8().constData(), nullptr );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 1, 1,
                               const_cast<float *>( &spectrum[static_cast<size_t>( b - 1 )] ),
                               1, 1, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
    return path;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// rs:threshold_raster — manual threshold contract
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: threshold mask is (v >= t) with NoData → 255",
           "[r4][known][threshold]" )
{
    // 4x1 raster {1, 3, 5, 7} + sentinel at (3,0) replacing 7 → {1,3,5,-9999}.
    // threshold 4: mask = (v >= 4) → [0, 0, 1, 255]; the sentinel pixel is
    // NoData (255), never "changed" and never evaluated.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v = { 1, 3, 5, static_cast<float>( kSentinel ) };
    const QString input = writeFloatRaster( dir.filePath( "grid.tif" ), 4, 1, { v }, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( "mask.tif" ).toStdString();
    p["thresholdMethod"] = "manual";
    p["threshold"] = 4.0;
    const Json::Value result = runOperator( "rs:threshold_raster", p,
                                            dir.path().toStdString() );

    const auto mask = readByteBand( dir.filePath( "mask.tif" ), 1 );
    REQUIRE( mask[0] == 0 );
    REQUIRE( mask[1] == 0 );
    REQUIRE( mask[2] == 1 );
    REQUIRE( mask[3] == 255 );  // declared NoData → 255, never 1
    REQUIRE( result["maskedPixels"].asInt() == 1 );
    REQUIRE( result["totalPixels"].asInt() == 3 );  // sentinel excluded
}

// ---------------------------------------------------------------------------
// rs:sar_calibrate — sigma0 = DN²/A²
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: SAR calibration is the documented DN²/A² closed form",
           "[r4][known][sar]" )
{
    // linear_power domain, calibrationA = 2, noiseLinear = 0:
    //   DN 6 → 36/4 = 9;  DN 4 → 16/4 = 4;  declared sentinel → NaN (declared).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v = { 6, 4, static_cast<float>( kSentinel ) };
    const QString input = writeFloatRaster( dir.filePath( "dn.tif" ), 3, 1, { v }, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( "sig0.tif" ).toStdString();
    p["calibrationA"] = 2.0;
    p["noiseLinear"] = 0.0;
    p["outputDomain"] = "linear_power";
    runOperator( "rs:sar_calibrate", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "sig0.tif" ), 1, &hasNd, &declared );
    REQUIRE( nearRel( out[0], 9.0 ) );
    REQUIRE( nearRel( out[1], 4.0 ) );
    REQUIRE( std::isnan( out[2] ) );
    REQUIRE( hasNd );
    REQUIRE( std::isnan( declared ) );
}

// ---------------------------------------------------------------------------
// rs:continuum_removal — convex-hull envelope on a triangle spectrum
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: continuum removal divides by the convex hull envelope",
           "[r4][known][continuum]" )
{
    // 7 bands on 400..1000 nm (100 nm steps); spectrum traces a triangle
    // with apex (700, 0.8) over baseline 0.2:
    //   v = [0.20, 0.30, 0.55, 0.80, 0.55, 0.35, 0.20]
    // Upper hull: (400,0.2) → (700,0.8) → (1000,0.2).
    //   hull(500) = 0.2 + 100/300·0.6 = 0.4  → CR = 0.30/0.40 = 0.75
    //   hull(600) = 0.6                       → CR = 0.55/0.60 = 11/12
    //   hull(700) = apex                     → CR = 1
    //   hull(800) = 0.8 − 100/300·0.6 = 0.6  → CR = 0.55/0.60 = 11/12
    //   hull(900) = 0.8 − 200/300·0.6 = 0.4  → CR = 0.35/0.40 = 0.875
    //   endpoints sit on the hull             → CR = 1
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::vector<double> wl = { 400, 500, 600, 700, 800, 900, 1000 };
    const std::vector<float> spec = { 0.20f, 0.30f, 0.55f, 0.80f, 0.55f, 0.35f, 0.20f };
    const QString input = writeSpectrumRaster( dir.filePath( "spec.tif" ), wl, spec, true,
                                               kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( "cr.tif" ).toStdString();
    runOperator( "rs:continuum_removal", p, dir.path().toStdString() );

    const auto out = readBand( dir.filePath( "cr.tif" ), 1 );
    REQUIRE( out.size() == 7 );
    REQUIRE( nearRel( out[0], 1.0, 1e-6 ) );
    REQUIRE( nearRel( out[1], 0.75, 1e-6 ) );
    REQUIRE( nearRel( out[2], 11.0 / 12.0, 1e-6 ) );
    REQUIRE( nearRel( out[3], 1.0, 1e-6 ) );
    REQUIRE( nearRel( out[4], 11.0 / 12.0, 1e-6 ) );
    REQUIRE( nearRel( out[5], 0.875, 1e-6 ) );
    REQUIRE( nearRel( out[6], 1.0, 1e-6 ) );
}

// ---------------------------------------------------------------------------
// rs:spectral_resample — linear interpolation, out-of-range → NaN
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: spectral resample interpolates linearly and NaNs out-of-range",
           "[r4][known][resample]" )
{
    // Source bands at 500/600/700 nm carrying the linear ramp v = λ/100 − 4
    // → [1, 2, 3]. Targets [450, 550, 650]:
    //   450 < 500 → NaN (#445, no extrapolation)
    //   550 → (1+2)/2 = 1.5
    //   650 → (2+3)/2 = 2.5
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::vector<std::vector<float>> bands = { { 1.0f }, { 2.0f }, { 3.0f } };
    const QString input = writeFloatRaster( dir.filePath( "spec.tif" ), 1, 1, bands, true,
                                            kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( "rs.tif" ).toStdString();
    Json::Value src( Json::arrayValue );
    for ( const double w : { 500.0, 600.0, 700.0 } )
        src.append( w );
    p["sourceWavelengths"] = src;
    Json::Value dst( Json::arrayValue );
    for ( const double w : { 450.0, 550.0, 650.0 } )
        dst.append( w );
    p["wavelengths"] = dst;
    runOperator( "rs:spectral_resample", p, dir.path().toStdString() );

    const auto out = readBand( dir.filePath( "rs.tif" ), 1 );
    const auto out2 = readBand( dir.filePath( "rs.tif" ), 2 );
    const auto out3 = readBand( dir.filePath( "rs.tif" ), 3 );
    REQUIRE( std::isnan( out[0] ) );
    REQUIRE( nearRel( out2[0], 1.5, 1e-6 ) );
    REQUIRE( nearRel( out3[0], 2.5, 1e-6 ) );
}

// ---------------------------------------------------------------------------
// rs:spectral_similarity — SAM labelling + declared-sentinel unlabelled
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: similarity labels the angularly closest reference and honours "
           "the declared sentinel",
           "[r4][known][similarity]" )
{
    // References r0 = [2,1], r1 = [1,2] (non-negative per the contract).
    // Pixels: A = [2,1] → angle 0 to r0 → label 0; B = [1,2] → label 1;
    // declared-sentinel pixel → unlabelled (-9999) — the R4 declared-
    // sentinel fix (previously only the hardcoded -9999 was excluded).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::vector<float> b1 = { 2, 1, static_cast<float>( kSentinel ) };
    const std::vector<float> b2 = { 1, 2, static_cast<float>( kSentinel ) };
    const QString input = writeFloatRaster( dir.filePath( "img.tif" ), 3, 1, { b1, b2 },
                                            true, kSentinel );
    Json::Value refs( Json::arrayValue );
    Json::Value r0( Json::arrayValue );
    r0.append( 2 );
    r0.append( 1 );
    Json::Value r1( Json::arrayValue );
    r1.append( 1 );
    r1.append( 2 );
    refs.append( r0 );
    refs.append( r1 );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( "labels.tif" ).toStdString();
    p["refs"] = refs;
    runOperator( "rs:spectral_similarity", p, dir.path().toStdString() );

    const auto labels = readBand( dir.filePath( "labels.tif" ), 1 );
    REQUIRE( nearRel( labels[0], 0.0 ) );
    REQUIRE( nearRel( labels[1], 1.0 ) );
    REQUIRE( nearRel( labels[2], -9999.0 ) );  // sentinel → unlabelled
}

// ---------------------------------------------------------------------------
// rs:mosaic — last-valid-wins overlap, NoData never overwrites valid
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: mosaic overlap is last-valid-wins with a declared background",
           "[r4][known][mosaic]" )
{
    // A = identity i; B = 1000 + i with B(0,0) = sentinel. B is the later
    // input: every B-valid pixel wins (1000+i); at (0,0) B is NoData so A's
    // value shows through; output declares the common sentinel.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> a( 100 ), b( 100 );
    for ( int i = 0; i < 100; ++i )
    {
        a[static_cast<size_t>( i )] = static_cast<float>( i );
        b[static_cast<size_t>( i )] = static_cast<float>( 1000 + i );
    }
    b[0] = static_cast<float>( kSentinel );
    const QString inA = writeFloatRaster( dir.filePath( "a.tif" ), 10, 10, { a }, true,
                                          kSentinel );
    const QString inB = writeFloatRaster( dir.filePath( "b.tif" ), 10, 10, { b }, true,
                                          kSentinel );
    Json::Value p;
    Json::Value inputs( Json::arrayValue );
    inputs.append( inA.toStdString() );
    inputs.append( inB.toStdString() );
    p["inputs"] = inputs;
    p["output"] = dir.filePath( "mosaic.tif" ).toStdString();
    runOperator( "rs:mosaic", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "mosaic.tif" ), 1, &hasNd, &declared );
    REQUIRE( hasNd );
    REQUIRE( nearRel( declared, kSentinel ) );
    REQUIRE( nearRel( out[0], 0.0 ) );               // B invalid → A shows through
    REQUIRE( nearRel( out[42], 1042.0 ) );           // later input wins
    REQUIRE( nearRel( out[99], 1099.0 ) );
}

// ---------------------------------------------------------------------------
// rs:extract_bands — verbatim copy + sentinel re-declaration
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: extract_bands copies pixels verbatim and re-declares NoData",
           "[r4][known][extract]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> b1( 100 ), b2( 100 );
    for ( int i = 0; i < 100; ++i )
    {
        b1[static_cast<size_t>( i )] = static_cast<float>( i );
        b2[static_cast<size_t>( i )] = static_cast<float>( -i );
    }
    b1[17] = static_cast<float>( kSentinel );
    const QString input = writeFloatRaster( dir.filePath( "stack.tif" ), 10, 10,
                                            { b1, b2 }, true, kSentinel );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( "extracted.tif" ).toStdString();
    Json::Value bands( Json::arrayValue );
    bands.append( 1 );
    bands.append( 2 );
    p["bands"] = bands;
    runOperator( "rs:extract_bands", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto o1 = readBand( dir.filePath( "extracted.tif" ), 1, &hasNd, &declared );
    const auto o2 = readBand( dir.filePath( "extracted.tif" ), 2 );
    REQUIRE( o1 == b1 );
    REQUIRE( o2 == b2 );
    REQUIRE( hasNd );
    REQUIRE( nearRel( declared, kSentinel ) );
}

// ---------------------------------------------------------------------------
// rs:apply_mask — explicit no_data on undeclared bands
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: apply_mask declares the explicit no_data on undeclared bands",
           "[r4][known][mask_declare]" )
{
    // Input band declares no NoData; no_data = -7 supplies the fill. Masked
    // pixels must become -7 AND the output band must declare -7 (the typed
    // refusal when neither source exists is pinned in
    // test_operator_preflight_refusals.cpp).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v( 100, 3.0f );
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v },
                                             false, 0.0 );
    std::vector<uint8_t> mask( 100, 0 );
    for ( int i = 40; i < 50; ++i )
        mask[static_cast<size_t>( i )] = 1;
    const QString maskPath = writeByteMask( dir.filePath( "mask.tif" ), 10, 10, mask );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["mask"] = maskPath.toStdString();
    p["output"] = dir.filePath( "masked.tif" ).toStdString();
    p["no_data"] = -7.0;
    runOperator( "rs:apply_mask", p, dir.path().toStdString() );

    bool hasNd = false;
    double declared = 0.0;
    const auto out = readBand( dir.filePath( "masked.tif" ), 1, &hasNd, &declared );
    REQUIRE( hasNd );
    REQUIRE( nearRel( declared, -7.0 ) );
    for ( int i = 0; i < 100; ++i )
        REQUIRE( nearRel( out[static_cast<size_t>( i )],
                          mask[static_cast<size_t>( i )] ? -7.0 : 3.0 ) );
}
