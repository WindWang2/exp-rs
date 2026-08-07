// test_spectral_classification.cpp — SAM + Continuum Removal kernel tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/spectral_classification.h"

#include <cmath>
#include <vector>

#include <QFile>
#include <QTemporaryDir>
#include <gdal.h>
#include <json/json.h>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_sam_classify_operator.h"
#include "operators/rs/rs_continuum_removal_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;

using sicnu::operators::RSOperatorContext;
using sicnu::operators::rs::RsContinuumRemovalOperator;
using sicnu::operators::rs::RsSamClassifyOperator;

static const float NODATA = -9999.0f;

// ===========================================================================
// spectralAngle
// ===========================================================================

TEST_CASE( "spectralAngle: identical vectors yield zero angle", "[sam]" )
{
    std::vector<float> t = { 1.0f, 2.0f, 3.0f };
    std::vector<float> r = { 1.0f, 2.0f, 3.0f };
    double ang = SpectralClassification::spectralAngle( t.data(), r.data(), 3, NODATA );
    REQUIRE( ang == Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "spectralAngle: orthogonal vectors yield pi/2", "[sam]" )
{
    std::vector<float> t = { 1.0f, 0.0f };
    std::vector<float> r = { 0.0f, 1.0f };
    double ang = SpectralClassification::spectralAngle( t.data(), r.data(), 2, NODATA );
    REQUIRE( ang == Approx( M_PI / 2.0 ).margin( 1e-6 ) );
}

TEST_CASE( "spectralAngle: zero-norm vector yields NaN", "[sam]" )
{
    std::vector<float> t = { 0.0f, 0.0f, 0.0f };
    std::vector<float> r = { 1.0f, 2.0f, 3.0f };
    double ang = SpectralClassification::spectralAngle( t.data(), r.data(), 3, NODATA );
    REQUIRE( std::isnan( ang ) );
}

TEST_CASE( "spectralAngle: nodata band yields NaN", "[sam]" )
{
    std::vector<float> t = { 1.0f, NODATA, 3.0f };
    std::vector<float> r = { 1.0f, 2.0f, 3.0f };
    double ang = SpectralClassification::spectralAngle( t.data(), r.data(), 3, NODATA );
    REQUIRE( std::isnan( ang ) );
}

TEST_CASE( "spectralAngle: known angle", "[sam]" )
{
    // t = (1,0), r = (1,1) → angle = arccos(1/sqrt(2)) = 45deg
    std::vector<float> t = { 1.0f, 0.0f };
    std::vector<float> r = { 1.0f, 1.0f };
    double ang = SpectralClassification::spectralAngle( t.data(), r.data(), 2, NODATA );
    REQUIRE( ang == Approx( M_PI / 4.0 ).margin( 1e-6 ) );
}

// ===========================================================================
// SAM classify
// ===========================================================================

TEST_CASE( "SAM: classifies pixels to nearest reference spectrum", "[sam]" )
{
    // 2 classes, 3 bands, 2 pixels
    // class 0 reference: (10, 20, 30); class 1 reference: (30, 20, 10)
    std::vector<float> refs = { 10.0f, 20.0f, 30.0f,  30.0f, 20.0f, 10.0f };
    // pixel 0 close to class 0; pixel 1 close to class 1
    std::vector<float> pixels = { 11.0f, 19.0f, 31.0f,   29.0f, 21.0f, 9.0f };
    std::vector<int> labels( 2, -99 );
    std::vector<float> angles( 2, 0.0f );

    bool ok = SpectralClassification::samClassify( pixels.data(), 2, 3,
                                                   refs.data(), 2,
                                                   labels.data(), angles.data(), NODATA );
    REQUIRE( ok );
    REQUIRE( labels[0] == 0 );
    REQUIRE( labels[1] == 1 );
    REQUIRE( angles[0] == Approx( 0.0 ).margin( 0.05 ) );
    REQUIRE( angles[1] == Approx( 0.0 ).margin( 0.05 ) );
}

TEST_CASE( "SAM: nodata pixel labelled -1", "[sam]" )
{
    std::vector<float> refs = { 10.0f, 20.0f, 30.0f };
    std::vector<float> pixels = { 10.0f, NODATA, 30.0f };
    std::vector<int> labels( 1, -99 );
    bool ok = SpectralClassification::samClassify( pixels.data(), 1, 3,
                                                   refs.data(), 1,
                                                   labels.data(), nullptr, NODATA );
    REQUIRE( ok );
    REQUIRE( labels[0] == -1 );
}

TEST_CASE( "SAM: rejects invalid arguments", "[sam]" )
{
    std::vector<int> labels( 1 );
    // null pixels
    REQUIRE_FALSE( SpectralClassification::samClassify( nullptr, 1, 3, nullptr, 1,
                                                        labels.data(), nullptr, NODATA ) );
    // zero count
    float p = 1.0f, r = 1.0f;
    REQUIRE_FALSE( SpectralClassification::samClassify( &p, 0, 3, &r, 1,
                                                        labels.data(), nullptr, NODATA ) );
    // zero refs
    REQUIRE_FALSE( SpectralClassification::samClassify( &p, 1, 3, &r, 0,
                                                        labels.data(), nullptr, NODATA ) );
}

TEST_CASE( "SAM: degenerate reference is skipped, not fatal", "[sam]" )
{
    // A zero-norm reference (all zeros) yields a NaN angle. The pixel must
    // still be classified to the nearest VALID reference — the degenerate
    // reference is skipped, not treated as a reason to discard the pixel.
    //   ref 0: valid, target-ish for pixel
    //   ref 1: zero-norm (degenerate) → must be skipped
    //   ref 2: valid, far from pixel
    std::vector<float> refs = { 10.0f, 20.0f, 30.0f,   // ref 0
                                0.0f, 0.0f, 0.0f,       // ref 1 (degenerate)
                                30.0f, 20.0f, 10.0f };  // ref 2
    std::vector<float> pixels = { 11.0f, 19.0f, 31.0f }; // close to ref 0
    std::vector<int> labels( 1, -99 );
    bool ok = SpectralClassification::samClassify( pixels.data(), 1, 3,
                                                   refs.data(), 3,
                                                   labels.data(), nullptr, NODATA );
    REQUIRE( ok );
    REQUIRE( labels[0] == 0 ); // classified to the nearest valid ref, not -1
}

TEST_CASE( "SAM: pixel with a nodata band stays unclassifiable", "[sam]" )
{
    // Even if some references are valid, a nodata band in the pixel itself
    // makes the whole pixel unclassifiable (label -1).
    std::vector<float> refs = { 10.0f, 20.0f, 30.0f };
    std::vector<float> pixels = { 10.0f, NODATA, 30.0f }; // nodata band
    std::vector<int> labels( 1, -99 );
    bool ok = SpectralClassification::samClassify( pixels.data(), 1, 3,
                                                   refs.data(), 1,
                                                   labels.data(), nullptr, NODATA );
    REQUIRE( ok );
    REQUIRE( labels[0] == -1 );
}

// ===========================================================================
// Continuum Removal
// ===========================================================================

TEST_CASE( "ContinuumRemoval: endpoints are 1.0", "[continuum]" )
{
    // A linearly-spaced increasing spectrum: the upper hull is exactly the
    // endpoints' tie-line, so every sample lies on or below it (ratio <= 1.0)
    // and the two endpoints equal 1.0.
    std::vector<float> s = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
    std::vector<float> out( 5, 0.0f );
    bool ok = SpectralClassification::continuumRemoval( s.data(), out.data(), 5, NODATA );
    REQUIRE( ok );
    REQUIRE( out[0] == Approx( 1.0f ).margin( 1e-5 ) );
    REQUIRE( out[4] == Approx( 1.0f ).margin( 1e-5 ) );
    // All samples of a linear ramp lie on the continuum tie-line → ratio 1.0
    for ( int i = 1; i < 4; ++i )
        REQUIRE( out[i] == Approx( 1.0f ).margin( 1e-5 ) );
}

TEST_CASE( "ContinuumRemoval: concave interior drops below 1.0", "[continuum]" )
{
    // Endpoints high, interior dipping below the tie-line: the hull is the
    // single tie-line (0→4), interior points fall strictly under it.
    std::vector<float> s = { 0.5f, 0.2f, 0.1f, 0.2f, 0.5f };
    std::vector<float> out( 5, 0.0f );
    bool ok = SpectralClassification::continuumRemoval( s.data(), out.data(), 5, NODATA );
    REQUIRE( ok );
    REQUIRE( out[0] == Approx( 1.0f ).margin( 1e-5 ) );
    REQUIRE( out[4] == Approx( 1.0f ).margin( 1e-5 ) );
    // Hull = endpoints (0,4); tie-line is flat at y=0.5. Interior ratios:
    // 0.2/0.5=0.4, 0.1/0.5=0.2, 0.2/0.5=0.4 — a clear absorption valley.
    REQUIRE( out[1] == Approx( 0.4f ).margin( 1e-4 ) );
    REQUIRE( out[2] == Approx( 0.2f ).margin( 1e-4 ) );
    REQUIRE( out[3] == Approx( 0.4f ).margin( 1e-4 ) );
}

TEST_CASE( "ContinuumRemoval: absorption feature normalizes to a valley", "[continuum]" )
{
    // Flat plateau with a single absorption dip in the middle.
    // Continuum (hull) = flat at 1.0 everywhere; the dip becomes a clear valley.
    std::vector<float> s = { 1.0f, 1.0f, 0.5f, 1.0f, 1.0f };
    std::vector<float> out( 5, 0.0f );
    bool ok = SpectralClassification::continuumRemoval( s.data(), out.data(), 5, NODATA );
    REQUIRE( ok );
    // Hull endpoints and the flat regions are on the continuum → ~1.0
    REQUIRE( out[0] == Approx( 1.0f ).margin( 1e-5 ) );
    REQUIRE( out[4] == Approx( 1.0f ).margin( 1e-5 ) );
    REQUIRE( out[1] == Approx( 1.0f ).margin( 1e-5 ) );
    REQUIRE( out[3] == Approx( 1.0f ).margin( 1e-5 ) );
    // The absorption centre is halved: 0.5 / 1.0 = 0.5
    REQUIRE( out[2] == Approx( 0.5f ).margin( 1e-5 ) );
}

TEST_CASE( "ContinuumRemoval: nodata input rejected", "[continuum]" )
{
    std::vector<float> s = { 0.1f, NODATA, 0.3f };
    std::vector<float> out( 3, 0.0f );
    bool ok = SpectralClassification::continuumRemoval( s.data(), out.data(), 3, NODATA );
    REQUIRE_FALSE( ok );
}

TEST_CASE( "ContinuumRemoval: single point edge case", "[continuum]" )
{
    std::vector<float> s = { 0.42f };
    std::vector<float> out( 1, 0.0f );
    bool ok = SpectralClassification::continuumRemoval( s.data(), out.data(), 1, NODATA );
    // Degenerate (one band) — the only sample is on the hull.
    REQUIRE( ok );
    REQUIRE( out[0] == Approx( 1.0f ).margin( 1e-5 ) );
}

// ===========================================================================
// Operator-level (file I/O) tests
// ===========================================================================

TEST_CASE( "rs:sam_classify writes a single-band classified raster", "[sam][gdal]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString inputPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "class.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 1x2 raster, 3 bands. Pixel column 0 ≈ class 0 spectrum; column 1 ≈ class 1.
    GDALDatasetH inDs = createOutputTiff( inputPath, 2, 1, 3, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    std::vector<float> b1 = { 10.0f, 30.0f };
    std::vector<float> b2 = { 20.0f, 20.0f };
    std::vector<float> b3 = { 30.0f, 10.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 1 ), GF_Write, 0, 0, 2, 1,
                           b1.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 2 ), GF_Write, 0, 0, 2, 1,
                           b2.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 3 ), GF_Write, 0, 0, 2, 1,
                           b3.data(), 2, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( inDs );

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    Json::Value refs( Json::arrayValue );
    Json::Value c0( Json::arrayValue );
    c0.append( 10.0 );
    c0.append( 20.0 );
    c0.append( 30.0 );
    Json::Value c1( Json::arrayValue );
    c1.append( 30.0 );
    c1.append( 20.0 );
    c1.append( 10.0 );
    refs.append( c0 );
    refs.append( c1 );
    params["refs"] = refs;

    RsSamClassifyOperator op;
    RSOperatorContext ctx;
    Json::Value result = op.run( params, ctx );

    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( result["classes"].asInt() == 2 );
    REQUIRE( result["bands"].asInt() == 3 );

    // Read back and verify labels.
    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 1 );
    std::vector<float> labels( 2 );
    REQUIRE( out.readBandData( 1, labels.data(), 2, 1 ) );
    REQUIRE( labels[0] == Approx( 0.0f ).margin( 0.5f ) );
    REQUIRE( labels[1] == Approx( 1.0f ).margin( 0.5f ) );
}

TEST_CASE( "rs:continuum_removal normalizes a multi-band raster", "[continuum][gdal]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString inputPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "cr.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 1 pixel, 5 bands: plateau with absorption dip → continuum-removed centre = 0.5.
    GDALDatasetH inDs = createOutputTiff( inputPath, 1, 1, 5, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    std::vector<float> vals = { 1.0f, 1.0f, 0.5f, 1.0f, 1.0f };
    for ( int b = 0; b < 5; ++b )
    {
        std::vector<float> band = { vals[b] };
        REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, b + 1 ), GF_Write, 0, 0, 1, 1,
                               band.data(), 1, 1, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( inDs );

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();

    RsContinuumRemovalOperator op;
    RSOperatorContext ctx;
    Json::Value result = op.run( params, ctx );

    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( result["bands"].asInt() == 5 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 5 );
    std::vector<float> centre( 1 );
    REQUIRE( out.readBandData( 3, centre.data(), 1, 1 ) );
    REQUIRE( centre[0] == Approx( 0.5f ).margin( 1e-4 ) );
}

