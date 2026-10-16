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
#include "operators/rs/rs_spectral_similarity_operator.h"
#include "operators/rs/rs_continuum_removal_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;

using sicnu::operators::RSOperatorContext;
using sicnu::operators::rs::RsContinuumRemovalOperator;
using sicnu::operators::rs::RsSamClassifyOperator;
using sicnu::operators::rs::RsSpectralSimilarityOperator;

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

TEST_CASE( "rs:sam_classify supports the SID metric", "[sid][gdal]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString inputPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "class.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 1x2 raster, 3 bands — positive reflectance-like spectra.
    GDALDatasetH inDs = createOutputTiff( inputPath, 2, 1, 3, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    std::vector<float> b1 = { 0.6f, 0.1f };
    std::vector<float> b2 = { 0.2f, 0.4f };
    std::vector<float> b3 = { 0.2f, 0.5f };
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
    params["metric"] = "sid";
    Json::Value refs( Json::arrayValue );
    Json::Value c0( Json::arrayValue );
    c0.append( 0.6 );
    c0.append( 0.2 );
    c0.append( 0.2 );
    Json::Value c1( Json::arrayValue );
    c1.append( 0.1 );
    c1.append( 0.4 );
    c1.append( 0.5 );
    refs.append( c0 );
    refs.append( c1 );
    params["refs"] = refs;

    RsSamClassifyOperator op;
    RSOperatorContext ctx;
    Json::Value result = op.run( params, ctx );

    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( result["metric"].asString() == "sid" );
    REQUIRE( result["classes"].asInt() == 2 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> labels( 2 );
    REQUIRE( out.readBandData( 1, labels.data(), 2, 1 ) );
    REQUIRE( labels[0] == Approx( 0.0f ).margin( 0.5f ) );
    REQUIRE( labels[1] == Approx( 1.0f ).margin( 0.5f ) );
}

namespace
{
// GTiff stores ONE NoData tag per dataset, so heterogeneous per-band
// declarations ride on a VRT wrapper (same fixture strategy as
// test_r5_nodata_contracts). An empty string leaves that band undeclared.
bool writeVrtWithBandNodata( const QString &vrtPath, const QString &rawName, int width, int height,
                             const std::vector<QString> &bandNodata )
{
    QFile vrt( vrtPath );
    if ( !vrt.open( QIODevice::WriteOnly | QIODevice::Text ) )
        return false;
    QTextStream out( &vrt );
    out << "<VRTDataset rasterXSize=\"" << width << "\" rasterYSize=\"" << height << "\">\n";
    for ( int b = 0; b < static_cast<int>( bandNodata.size() ); ++b )
    {
        out << "  <VRTRasterBand dataType=\"Float32\" band=\"" << b + 1 << "\">\n";
        if ( !bandNodata[static_cast<size_t>( b )].isEmpty() )
            out << "    <NoDataValue>" << bandNodata[static_cast<size_t>( b )]
                << "</NoDataValue>\n";
        out << "    <SimpleSource><SourceFilename relativeToVRT=\"1\">" << rawName
            << "</SourceFilename><SourceBand>" << b + 1
            << "</SourceBand></SimpleSource>\n";
        out << "  </VRTRasterBand>\n";
    }
    out << "</VRTDataset>\n";
    return true;
}
} // namespace

TEST_CASE( "rs:sam_classify honors heterogeneous per-band NoData [r14a]", "[sam][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString rawPath = dir.filePath( QStringLiteral( "het_raw.tif" ) );
    const QString inputPath = dir.filePath( QStringLiteral( "het.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "het_classes.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 3x1 raster, 2 bands with DIFFERENT declared sentinels: band 1 voids at
    // 0.0, band 2 voids at -9999. Pixel 0 is clean; pixel 1 is void in band 1;
    // pixel 2 is void in band 2. Under the old first-band-only sentinel,
    // pixel 2's -9999 passed as data and was classified.
    GDALDatasetH inDs = createOutputTiff( rawPath, 3, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    std::vector<float> b1 = { 0.8f, 0.0f, 0.8f };
    std::vector<float> b2 = { 0.2f, 0.2f, -9999.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 1 ), GF_Write, 0, 0, 3, 1,
                           b1.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 2 ), GF_Write, 0, 0, 3, 1,
                           b2.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( inDs );
    REQUIRE( writeVrtWithBandNodata( inputPath, QStringLiteral( "het_raw.tif" ), 3, 1,
                                     { QStringLiteral( "0" ), QStringLiteral( "-9999" ) } ) );
    // Fixture sanity: the wrapper must expose the two DIFFERENT declarations.
    {
        GdalDatasetWrapper vds;
        REQUIRE( vds.open( inputPath ) );
        bool h1 = false, h2 = false;
        vds.bandNoDataValue( 1, &h1 );
        vds.bandNoDataValue( 2, &h2 );
        REQUIRE( h1 );
        REQUIRE( h2 );
    }

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    Json::Value refs( Json::arrayValue );
    Json::Value c0( Json::arrayValue );
    c0.append( 0.8 );
    c0.append( 0.2 );
    Json::Value c1( Json::arrayValue );
    c1.append( 0.2 );
    c1.append( 0.8 );
    refs.append( c0 );
    refs.append( c1 );
    params["refs"] = refs;

    RsSamClassifyOperator op;
    RSOperatorContext ctx;
    const Json::Value result = op.run( params, ctx );
    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( result["classes"].asInt() == 2 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> labels( 3 );
    REQUIRE( out.readBandData( 1, labels.data(), 3, 1 ) );
    REQUIRE( labels[0] == Approx( 0.0f ).margin( 0.5f ) ); // clean pixel classifies
    REQUIRE( labels[1] == Approx( -9999.0f ) );            // band-1 void: unclassified
    REQUIRE( labels[2] == Approx( -9999.0f ) );            // band-2 void: unclassified
}

// ===========================================================================
// R15-A (O2): undeclared-band NoData dialect — sam_classify (NaN-only) vs
// rs:spectral_similarity (-9999 family fallback). The two sibling spectral
// operators answer the SAME scientific question differently for a raster
// whose bands declare no NoData and whose samples carry -9999: sam treats
// the sentinel as data and classifies the pixel; spectral_similarity voids
// it. R14-A deliberately preserved the difference to avoid altering
// existing outputs and recorded it for a future policy round. This test
// dual-locks BOTH policies so the difference is explicit, regression-pinned,
// and safe to flip in a future policy round; sam's metadata() documents the
// policy the same way spectral_similarity's does. Unifying is a one-line
// change (sam's bandSentinels default), gated on this test being updated in
// the same commit.
// ===========================================================================

TEST_CASE( "rs: sam_classify and rs:spectral_similarity pin the undeclared-band NoData dialect [r15a][nodata]",
           "[sam][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 3x1 raster, 2 bands, NO NoData declaration anywhere (plain GTiff).
    // Pixel 0 is clean; pixels 1-2 are clean in band 1; pixel 2 carries a
    // -9999 sample in the UNDECLARED band 2 — the dialect probe.
    const QString inputPath = dir.filePath( QStringLiteral( "dialect.tif" ) );
    const QString samOut = dir.filePath( QStringLiteral( "dialect_sam.tif" ) );
    const QString simOut = dir.filePath( QStringLiteral( "dialect_sim.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH inDs = createOutputTiff( inputPath, 3, 1, 2, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    const std::vector<float> b1 = { 0.8f, 0.2f, 0.8f };
    const std::vector<float> b2 = { 0.2f, 0.8f, -9999.0f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 1 ), GF_Write, 0, 0, 3, 1,
                           const_cast<float *>( b1.data() ), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 2 ), GF_Write, 0, 0, 3, 1,
                           const_cast<float *>( b2.data() ), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( inDs );
    // Fixture sanity: neither band declares NoData.
    {
        GdalDatasetWrapper vds;
        REQUIRE( vds.open( inputPath ) );
        bool h1 = false, h2 = false;
        vds.bandNoDataValue( 1, &h1 );
        vds.bandNoDataValue( 2, &h2 );
        REQUIRE_FALSE( h1 );
        REQUIRE_FALSE( h2 );
    }

    Json::Value refs( Json::arrayValue );
    Json::Value c0( Json::arrayValue );
    c0.append( 0.8 );
    c0.append( 0.2 );
    Json::Value c1( Json::arrayValue );
    c1.append( 0.2 );
    c1.append( 0.8 );
    refs.append( c0 );
    refs.append( c1 );

    // sam: NaN-only policy — the undeclared -9999 is DATA, so pixel 2 still
    // receives a class label (its garbage angle happens to favor class 0).
    {
        Json::Value params( Json::objectValue );
        params["input"] = inputPath.toStdString();
        params["output"] = samOut.toStdString();
        params["refs"] = refs;

        RsSamClassifyOperator op;
        RSOperatorContext ctx;
        const Json::Value result = op.run( params, ctx );
        REQUIRE( QFile::exists( samOut ) );
        REQUIRE( result["classes"].asInt() == 2 );

        GdalDatasetWrapper out;
        REQUIRE( out.open( samOut ) );
        std::vector<float> labels( 3 );
        REQUIRE( out.readBandData( 1, labels.data(), 3, 1 ) );
        REQUIRE( labels[0] == Approx( 0.0f ).margin( 0.5f ) ); // clean: class 0
        REQUIRE( labels[1] == Approx( 1.0f ).margin( 0.5f ) ); // clean: class 1
        // The dialect lock: -9999 in an UNDECLARED band is classified, not
        // voided. If sam ever adopts the -9999 family fallback, this becomes
        // -9999 and this test must be updated in the same commit.
        REQUIRE( labels[2] != Approx( -9999.0f ) );
    }

    // spectral_similarity: -9999 family fallback — the same pixel is void.
    {
        Json::Value params( Json::objectValue );
        params["input"] = inputPath.toStdString();
        params["output"] = simOut.toStdString();
        params["refs"] = refs;

        RsSpectralSimilarityOperator op;
        RSOperatorContext ctx;
        const Json::Value result = op.run( params, ctx );
        REQUIRE( QFile::exists( simOut ) );
        REQUIRE( result["refs"].asInt() == 2 );

        GdalDatasetWrapper out;
        REQUIRE( out.open( simOut ) );
        std::vector<float> labels( 3 );
        REQUIRE( out.readBandData( 1, labels.data(), 3, 1 ) );
        REQUIRE( labels[0] == Approx( 0.0f ).margin( 0.5f ) ); // clean: class 0
        REQUIRE( labels[1] == Approx( 1.0f ).margin( 0.5f ) ); // clean: class 1
        // The dialect lock: undeclared -9999 is voided under the family
        // fallback. If the family policy is ever retired, update together
        // with the sam side above.
        REQUIRE( labels[2] == Approx( -9999.0f ) );
    }
}

TEST_CASE( "rs:continuum_removal honors heterogeneous per-band NoData [r14a]",
           "[continuum][gdal][nodata]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString rawPath = dir.filePath( QStringLiteral( "cr_het_raw.tif" ) );
    const QString inputPath = dir.filePath( QStringLiteral( "cr_het.vrt" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "cr_het_out.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 3x1 raster, 3 bands: band 1 voids at 0.0, band 2 voids at -9999, band 3
    // undeclared. Pixel 0 clean flat spectrum (Rc == 1); pixel 1 void in band
    // 2; pixel 2 void in band 1. Under the old band-1-only sentinel, pixel 1's
    // -9999 entered the convex hull as reflectance and produced finite (wrong)
    // Rc values on the clean bands, and pixel 2's void was written back as the
    // sentinel 0.0 instead of the family's NaN void.
    GDALDatasetH inDs = createOutputTiff( rawPath, 3, 1, 3, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    std::vector<float> b1 = { 0.5f, 0.5f, 0.0f };
    std::vector<float> b2 = { 0.5f, -9999.0f, 0.5f };
    std::vector<float> b3 = { 0.5f, 0.5f, 0.5f };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 1 ), GF_Write, 0, 0, 3, 1,
                           b1.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 2 ), GF_Write, 0, 0, 3, 1,
                           b2.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( inDs, 3 ), GF_Write, 0, 0, 3, 1,
                           b3.data(), 3, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( inDs );
    // Per-band declarations ride on a VRT wrapper (GTiff stores one NoData
    // tag per dataset); band 3 stays undeclared via an empty entry.
    REQUIRE( writeVrtWithBandNodata( inputPath, QStringLiteral( "cr_het_raw.tif" ), 3, 1,
                                     { QStringLiteral( "0" ), QStringLiteral( "-9999" ),
                                       QString() } ) );
    {
        GdalDatasetWrapper vds;
        REQUIRE( vds.open( inputPath ) );
        bool h1 = false, h2 = false, h3 = true;
        vds.bandNoDataValue( 1, &h1 );
        vds.bandNoDataValue( 2, &h2 );
        vds.bandNoDataValue( 3, &h3 );
        REQUIRE( h1 );
        REQUIRE( h2 );
        REQUIRE_FALSE( h3 );
    }

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();

    RsContinuumRemovalOperator op;
    RSOperatorContext ctx;
    const Json::Value result = op.run( params, ctx );
    REQUIRE( QFile::exists( outputPath ) );
    REQUIRE( result["bands"].asInt() == 3 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 3 );
    for ( int b = 1; b <= 3; ++b )
    {
        std::vector<float> band( 3 );
        REQUIRE( out.readBandData( b, band.data(), 3, 1 ) );
        REQUIRE( band[0] == Approx( 1.0f ).margin( 1e-5 ) ); // clean flat spectrum
        REQUIRE( std::isnan( band[1] ) );                    // band-2 void voids the spectrum
        REQUIRE( std::isnan( band[2] ) );                    // band-1 void voids the spectrum
    }
    // Output declares NaN voids (spectral-family convention after the wash).
    bool hasNd = false;
    const double ndTag = out.bandNoDataValue( 1, &hasNd );
    CHECK( hasNd );
    CHECK( std::isnan( ndTag ) );
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


// ===========================================================================
// Spectral Information Divergence (SID)
// ===========================================================================

TEST_CASE( "spectralDivergence: identical spectra yield zero", "[sid]" )
{
    const float t[] = { 0.2f, 0.4f, 0.4f };
    const float r[] = { 0.2f, 0.4f, 0.4f };
    CHECK( SpectralClassification::spectralDivergence( t, r, 3, NODATA )
           == Catch::Approx( 0.0 ).margin( 1e-9 ) );
    // Scale-invariance: identical shape, different brightness.
    const float bright[] = { 0.5f, 1.0f, 1.0f };
    CHECK( SpectralClassification::spectralDivergence( t, bright, 3, NODATA )
           == Catch::Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "spectralDivergence: known asymmetric divergence", "[sid]" )
{
    // p = (0.5, 0.5), q = (0.9, 0.1)
    // SID = 0.5*ln(0.5/0.9) + 0.5*ln(0.5/0.1) + 0.9*ln(0.9/0.5) + 0.1*ln(0.1/0.5)
    const float t[] = { 0.5f, 0.5f };
    const float r[] = { 0.9f, 0.1f };
    const double expected = 0.5 * std::log( 0.5 / 0.9 ) + 0.5 * std::log( 0.5 / 0.1 )
                            + 0.9 * std::log( 0.9 / 0.5 ) + 0.1 * std::log( 0.1 / 0.5 );
    CHECK( SpectralClassification::spectralDivergence( t, r, 2, NODATA )
           == Catch::Approx( expected ).margin( 1e-9 ) );
}

TEST_CASE( "spectralDivergence: invalid spectra yield NaN", "[sid]" )
{
    SECTION( "Zero band contributes nothing (finite SID)" )
    {
        // A band present in only one spectrum is skipped by the KL sum; the
        // remaining bands still produce a finite divergence.
        const float t[] = { 0.0f, 0.5f };
        const float r[] = { 0.5f, 0.5f };
        CHECK( std::isfinite( SpectralClassification::spectralDivergence( t, r, 2, NODATA ) ) );
    }
    SECTION( "Negative band" )
    {
        const float t[] = { -0.1f, 0.5f };
        const float r[] = { 0.5f, 0.5f };
        CHECK( std::isnan( SpectralClassification::spectralDivergence( t, r, 2, NODATA ) ) );
    }
    SECTION( "Nodata band" )
    {
        const float t[] = { NODATA, 0.5f };
        const float r[] = { 0.5f, 0.5f };
        CHECK( std::isnan( SpectralClassification::spectralDivergence( t, r, 2, NODATA ) ) );
    }
    SECTION( "Zero-sum spectrum" )
    {
        const float t[] = { 0.0f, 0.0f };
        const float r[] = { 0.5f, 0.5f };
        CHECK( std::isnan( SpectralClassification::spectralDivergence( t, r, 2, NODATA ) ) );
    }
}

TEST_CASE( "SID: classifies pixels to the nearest reference spectrum", "[sid]" )
{
    // Two references: vegetation-like (0.6, 0.2, 0.2) and water-like (0.1, 0.4, 0.5).
    std::vector<float> refs = { 0.6f, 0.2f, 0.2f, 0.1f, 0.4f, 0.5f };
    // Two pixels: pixel 0 close to vegetation, pixel 1 close to water.
    std::vector<float> pixels = { 0.7f, 0.1f, 0.2f, 0.1f, 0.45f, 0.45f };

    std::vector<int> labels( 2 );
    std::vector<float> divergences( 2, 0.0f );
    REQUIRE( SpectralClassification::sidClassify(
        pixels.data(), 2, 3, refs.data(), 2, labels.data(), divergences.data(), NODATA ) );
    CHECK( labels[0] == 0 );
    CHECK( labels[1] == 1 );
    CHECK( divergences[0] >= 0.0f );
}

TEST_CASE( "SID: nodata pixel labelled -1", "[sid]" )
{
    std::vector<float> refs = { 0.6f, 0.2f, 0.2f };
    std::vector<float> pixels = { NODATA, 0.2f, 0.2f };
    std::vector<int> labels( 1 );
    REQUIRE( SpectralClassification::sidClassify(
        pixels.data(), 1, 3, refs.data(), 1, labels.data(), nullptr, NODATA ) );
    CHECK( labels[0] == -1 );
}

TEST_CASE( "SAM: numerical boundary clamping on collinear spectra", "[sam][numerical]" )
{
    // Test exact collinear spectra where dot / (normT * normR) could slightly overshoot 1.0 due to FP rounding
    std::vector<float> refs = { 1.234567f, 2.345678f, 3.456789f, 4.567890f };
    std::vector<float> pixels = { 1.234567f, 2.345678f, 3.456789f, 4.567890f };
    std::vector<int> labels( 1 );
    std::vector<float> angles( 1 );

    REQUIRE( SpectralClassification::samClassify(
        pixels.data(), 1, 4, refs.data(), 1, labels.data(), angles.data(), NODATA ) );
    CHECK( labels[0] == 0 );
    CHECK( std::isfinite( angles[0] ) );
    CHECK( angles[0] == Approx( 0.0f ).margin( 1e-6f ) );

    // Test zero-norm pixel
    std::vector<float> zeroPixel = { 0.0f, 0.0f, 0.0f, 0.0f };
    REQUIRE( SpectralClassification::samClassify(
        zeroPixel.data(), 1, 4, refs.data(), 1, labels.data(), angles.data(), NODATA ) );
    CHECK( labels[0] == -1 );
    CHECK( std::isnan( angles[0] ) );
}

TEST_CASE( "SpectralClassification::samClassify Handles Infinite Values", "[spectral_classification][sam][inf]" )
{
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> refs = { 10.0f, 20.0f, 30.0f };
    std::vector<float> pixels = { 10.0f, inf, 30.0f,
                                  -inf, 20.0f, 30.0f };
    std::vector<int> labels( 2, -99 );
    std::vector<float> angles( 2, 0.0f );
    bool ok = SpectralClassification::samClassify( pixels.data(), 2, 3,
                                                   refs.data(), 1,
                                                   labels.data(), angles.data(), NODATA );
    REQUIRE( ok );
    CHECK( labels[0] == -1 );
    CHECK( labels[1] == -1 );
    CHECK( std::isnan( angles[0] ) );
    CHECK( std::isnan( angles[1] ) );
}


TEST_CASE( "rs:continuum_removal re-orders bands onto an unsorted WAVELENGTH axis (#700)", "[continuum][gdal][700]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString inputPath = dir.filePath( QStringLiteral( "wl_in.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "wl_cr.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    // 1 pixel, 4 bands whose band order disagrees with wavelength order:
    //   band:    1     2     3     4
    //   wl(nm): 1400   800  1100  1700
    //   refl:  0.6   1.0   0.5   1.0
    // Sorted onto the wavelength axis the spectrum is (800,1.0) (1100,0.5)
    // (1400,0.6) (1700,1.0): the upper hull is the 1.0 tie-line from 800 to
    // 1700, so the removed values equal the input reflectances. The old
    // unsalted hull over the raw band order returned 1.0/2.5/1.0/1.0.
    const float vals[4] = { 0.6f, 1.0f, 0.5f, 1.0f };
    const char *wls[4] = { "1400.0", "800.0", "1100.0", "1700.0" };

    GDALDatasetH inDs = createOutputTiff( inputPath, 1, 1, 4, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    for ( int b = 0; b < 4; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( inDs, b + 1 );
        std::vector<float> cell = { vals[b] };
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 1, 1, cell.data(), 1, 1,
                               GDT_Float32, 0, 0 ) == CE_None );
        REQUIRE( GDALSetMetadataItem( band, "WAVELENGTH", wls[b], nullptr ) == CE_None );
    }
    GDALClose( inDs );

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();

    RsContinuumRemovalOperator op;
    RSOperatorContext ctx;
    Json::Value result = op.run( params, ctx );
    REQUIRE( result["bands"].asInt() == 4 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    REQUIRE( out.bandCount() == 4 );
    // Output bands keep the input band order, values = input / continuum(1.0).
    const float expected[4] = { 0.6f, 1.0f, 0.5f, 1.0f };
    for ( int b = 1; b <= 4; ++b )
    {
        std::vector<float> cell( 1 );
        REQUIRE( out.readBandData( b, cell.data(), 1, 1 ) );
        CHECK( cell[0] == Approx( expected[b - 1] ).margin( 1e-3 ) );
    }
}

TEST_CASE( "rs:continuum_removal rejects duplicate WAVELENGTH values (#700)", "[continuum][gdal][700]" )
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString inputPath = dir.filePath( QStringLiteral( "dup_in.tif" ) );
    const QString outputPath = dir.filePath( QStringLiteral( "dup_cr.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    GDALDatasetH inDs = createOutputTiff( inputPath, 1, 1, 3, GDT_Float32, gt, QString() );
    REQUIRE( inDs != nullptr );
    const char *wls[3] = { "900.0", "700.0", "900.0" };  // duplicate across bands
    for ( int b = 0; b < 3; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( inDs, b + 1 );
        std::vector<float> cell = { 0.5f };
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 1, 1, cell.data(), 1, 1,
                               GDT_Float32, 0, 0 ) == CE_None );
        REQUIRE( GDALSetMetadataItem( band, "WAVELENGTH", wls[b], nullptr ) == CE_None );
    }
    GDALClose( inDs );

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();

    RsContinuumRemovalOperator op;
    RSOperatorContext ctx;
    REQUIRE_THROWS( op.run( params, ctx ) );
}
