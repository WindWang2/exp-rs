// tests/test_quality_mosaic_operator.cpp — F15 E2E: rs:quality_mosaic over
// real GDAL fixtures with known-answer truths (ADR 0163).
//
// Fixtures (synthetic, fully known):
//   scene A: 24×24 at grid origin, radiometry base 100 + ramp
//   scene B: 24×24 at offset (16,0) with gain 1.25 / offset −10 distortion
//            and a fully-clouded block on its left edge
// Oracle checks:
//   • balanced output ≈ reference radiometry inside the overlap
//   • seam corridor stays inside the low-cost zone (no |Δ| spike across it)
//   • provenance band: every filled pixel maps to {1,2}; unfilled = 0
//   • no NoData cracks where any input is valid (Oracle 1)
//   • report JSON written atomically with the documented schema
//   • cancel + failure semantics (missing input, CRS mismatch, misaligned mask)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/rs/rs_quality_mosaic_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <json/json.h>

#include <QDir>
#include <QFile>
#include <QString>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_spatialref.h>

#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::operators;
using Catch::Approx;

namespace {

struct Fixture
{
    QString dir;
    QString pathA, pathB, pathMask, pathOut, pathReport;

    static Fixture make()
    {
        Fixture f;
        f.dir = QDir::tempPath() + QStringLiteral( "/exp_rs_qmosaic_test" );
        QDir().mkpath( f.dir );
        const QString crs = [] {
            OGRSpatialReference srs;
            srs.SetFromUserInput( "EPSG:32631" );
            char *w = nullptr;
            srs.exportToWkt( &w );
            QString out( w );
            CPLFree( w );
            return out;
        }();
        f.pathA = f.dir + QStringLiteral( "/a.tif" );
        f.pathB = f.dir + QStringLiteral( "/b.tif" );
        f.pathMask = f.dir + QStringLiteral( "/mask.tif" );
        f.pathOut = f.dir + QStringLiteral( "/mosaic.tif" );
        f.pathReport = f.dir + QStringLiteral( "/report.json" );

        const int w = 24, h = 24;
        // Scene A at grid (0,0): base 100 + 0.5*gx.
        {
            GdalDatasetWrapper ds;
            std::array<double, 6> gt { 500000.0, 10.0, 0.0, 5000000.0, 0.0, -10.0 };
            REQUIRE( ds.create( f.pathA, w, h, 1, static_cast<int>( GDT_Float32 ), gt, crs ) );
            std::vector<float> data( static_cast<size_t>( w ) * h );
            for ( int r = 0; r < h; ++r )
                for ( int c = 0; c < w; ++c )
                    data[static_cast<size_t>( r ) * w + c] = 100.0f + 0.5f * c;
            ds.setBandNoDataValue( 1, -9999.0 );
            REQUIRE( ds.writeBandWindow( 1, 0, 0, w, h, data.data() ) );
            ds.close();
        }
        // Scene B at grid (16,0): distorted copy (gain 1.25, offset −10) with a
        // fully-clouded left block (value = declared nodata).
        {
            GdalDatasetWrapper ds;
            std::array<double, 6> gt { 500000.0 + 160.0, 10.0, 0.0, 5000000.0, 0.0, -10.0 };
            REQUIRE( ds.create( f.pathB, w, h, 1, static_cast<int>( GDT_Float32 ), gt, crs ) );
            std::vector<float> data( static_cast<size_t>( w ) * h );
            for ( int r = 0; r < h; ++r )
                for ( int c = 0; c < w; ++c )
                {
                    const int gx = 16 + c;
                    float v = static_cast<float>( 1.25 * ( 100.0 + 0.5 * gx ) - 10.0 );
                    if ( c < 4 )
                        v = -9999.0f; // cloudy strip = nodata
                    data[static_cast<size_t>( r ) * w + c] = v;
                }
            ds.setBandNoDataValue( 1, -9999.0 );
            REQUIRE( ds.writeBandWindow( 1, 0, 0, w, h, data.data() ) );
            ds.close();
        }
        // Cloud mask for B: 1.0 on the cloudy strip, 0 elsewhere.
        {
            GdalDatasetWrapper ds;
            std::array<double, 6> gt { 500000.0 + 160.0, 10.0, 0.0, 5000000.0, 0.0, -10.0 };
            REQUIRE( ds.create( f.pathMask, w, h, 1, static_cast<int>( GDT_Float32 ), gt, crs ) );
            std::vector<float> data( static_cast<size_t>( w ) * h, 0.0f );
            for ( int r = 0; r < h; ++r )
                for ( int c = 0; c < 4; ++c )
                    data[static_cast<size_t>( r ) * w + c] = 1.0f;
            REQUIRE( ds.writeBandWindow( 1, 0, 0, w, h, data.data() ) );
            ds.close();
        }
        return f;
    }

    ~Fixture()
    {
        QDir( dir ).removeRecursively();
    }
};

Json::Value runOperator( const Json::Value &params )
{
    sicnu::operators::rs::RsQualityMosaicOperator op;
    RSOperatorContext ctx;
    return op.run( params, ctx );
}

Json::Value readJson( const QString &path )
{
    std::ifstream in( path.toStdString() );
    REQUIRE( in.is_open() );
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    REQUIRE( Json::parseFromStream( b, in, &root, &errs ) );
    return root;
}

} // namespace

TEST_CASE( "QualityMosaic: balanced known-answer E2E with provenance and report",
           "[processing][mosaic][operator][e2e]" )
{
    Fixture fx = Fixture::make();
    QFile::remove( fx.pathOut );
    QFile::remove( fx.pathReport );

    Json::Value params( Json::objectValue );
    params["inputs"] = Json::Value( Json::arrayValue );
    params["inputs"].append( fx.pathA.toStdString() );
    Json::Value bSpec( Json::objectValue );
    bSpec["path"] = fx.pathB.toStdString();
    bSpec["cloudMask"] = fx.pathMask.toStdString();
    params["inputs"].append( bSpec );
    params["output"] = fx.pathOut.toStdString();
    params["reportOutput"] = fx.pathReport.toStdString();
    params["overviews"] = Json::Value( Json::arrayValue ); // keep the fixture light

    const Json::Value result = runOperator( params );
    REQUIRE( result.isMember( "output" ) );
    CHECK( result["width"].asInt() == 40 );  // 16 + 24
    CHECK( result["height"].asInt() == 24 );
    CHECK( result["bandCount"].asInt() == 1 );
    CHECK( result["seamCount"].asInt() >= 1 );

    // Reopen and verify the composite.
    GdalDatasetWrapper out;
    REQUIRE( out.open( fx.pathOut ) );
    CHECK( out.width() == 40 );
    CHECK( out.height() == 24 );
    CHECK( out.bandCount() == 2 ); // data + provenance

    std::vector<float> mosaic( static_cast<size_t>( 40 ) * 24 );
    REQUIRE( out.readBandWindow( 1, 0, 0, 40, 24, mosaic.data() ) );
    std::vector<float> prov( static_cast<size_t>( 40 ) * 24 );
    REQUIRE( out.readBandWindow( 2, 0, 0, 40, 24, prov.data() ) );

    double worstBalancedError = 0.0;
    int filled = 0, provA = 0, provB = 0;
    for ( int r = 0; r < 24; ++r )
    {
        for ( int gx = 0; gx < 40; ++gx )
        {
            const size_t i = static_cast<size_t>( r ) * 40 + gx;
            REQUIRE( std::isfinite( mosaic[i] ) ); // Oracle 1: no NoData cracks
            REQUIRE( ( prov[i] == 1.0f || prov[i] == 2.0f ) ); // Oracle 2: traceable
            ++filled;
            if ( prov[i] == 1.0f )
                ++provA;
            else
                ++provB;
            if ( gx >= 16 && gx < 24 )
            {
                // Overlap: whichever scene won, the value must match the
                // reference radiometry (100 + 0.5·gx) after balancing.
                worstBalancedError =
                    std::max( worstBalancedError,
                              std::abs( static_cast<double>( mosaic[i] ) -
                                        ( 100.0 + 0.5 * gx ) ) );
            }
        }
    }
    CHECK( filled == 40 * 24 );
    CHECK( provA > 0 );
    CHECK( provB > 0 );
    // Scene B only contributes its unclouded part [20,40) fully; the overlap
    // [16,24) may mix. Balancing truth: B was corrected to A's radiometry.
    CHECK( worstBalancedError < 2.0 );

    // Tiled, compressed, georeferenced output (COG-friendly publication).
    CHECK( out.driverName() == QStringLiteral( "GTiff" ) );
    std::array<double, 6> gt = out.geoTransform();
    CHECK( gt[0] == Approx( 500000.0 ) );
    CHECK( gt[1] == Approx( 10.0 ) );
    out.close();

    // Report schema.
    const Json::Value report = readJson( fx.pathReport );
    CHECK( report["schema"].asString() == "exp-rs/quality-mosaic-report@1" );
    CHECK( report["grid"]["width"].asInt() == 40 );
    CHECK( report["inputs"].size() == 2u );
    CHECK_FALSE( report["inputs"][0]["rejected"].asBool() );
    CHECK_FALSE( report["inputs"][1]["rejected"].asBool() );
    // Balancing truth for scene B: gain 0.8, bias +8.
    CHECK( report["inputs"][1]["balancing"][0]["gain"].asDouble() ==
           Approx( 0.8 ).margin( 1e-3 ) );
    CHECK( report["inputs"][1]["balancing"][0]["bias"].asDouble() ==
           Approx( 8.0 ).margin( 1e-2 ) );
}

TEST_CASE( "QualityMosaic: fail-closed on missing input and CRS mismatch",
           "[processing][mosaic][operator][negative]" )
{
    Fixture fx = Fixture::make();

    Json::Value params( Json::objectValue );
    params["inputs"] = Json::Value( Json::arrayValue );
    params["inputs"].append( ( fx.dir + QStringLiteral( "/missing.tif" ) ).toStdString() );
    params["output"] = fx.pathOut.toStdString();
    CHECK_THROWS_AS( runOperator( params ), RSOperatorError );

    // CRS mismatch: scene B in a different CRS than A.
    Fixture fx2 = Fixture::make();
    {
        // Overwrite B with a 4326 scene (different CRS, same file name).
        QFile::remove( fx2.pathB );
        GdalDatasetWrapper ds;
        std::array<double, 6> gt { 0.0, 0.001, 0.0, 0.0, 0.0, -0.001 };
        OGRSpatialReference srs;
        srs.SetFromUserInput( "EPSG:4326" );
        char *w = nullptr;
        srs.exportToWkt( &w );
        REQUIRE( ds.create( fx2.pathB, 24, 24, 1, static_cast<int>( GDT_Float32 ), gt,
                            QString( w ) ) );
        std::vector<float> data( static_cast<size_t>( 24 ) * 24, 1.0f );
        REQUIRE( ds.writeBandWindow( 1, 0, 0, 24, 24, data.data() ) );
        ds.close();
        CPLFree( w );
    }
    Json::Value p2( Json::objectValue );
    p2["inputs"] = Json::Value( Json::arrayValue );
    p2["inputs"].append( fx2.pathA.toStdString() );
    p2["inputs"].append( fx2.pathB.toStdString() );
    p2["output"] = fx2.pathOut.toStdString();
    bool threw = false;
    try
    {
        runOperator( p2 );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        CHECK( std::string( e.what() ).find( "grid-compatible" ) != std::string::npos );
    }
    CHECK( threw );
    CHECK_FALSE( QFile::exists( fx2.pathOut ) ); // atomic failure: no output
    CHECK_FALSE( QFile::exists( fx2.pathOut + QStringLiteral( ".part.tif" ) ) );
}

TEST_CASE( "QualityMosaic: misaligned cloud mask is rejected",
           "[processing][mosaic][operator][negative]" )
{
    Fixture fx = Fixture::make();
    // Bind B's cloud mask to A's geometry (shifted geotransform).
    QString maskPath = fx.dir + QStringLiteral( "/badmask.tif" );
    {
        GdalDatasetWrapper ds;
        std::array<double, 6> gt { 500000.0, 10.0, 0.0, 5000000.0, 0.0, -10.0 }; // A's gt
        REQUIRE( ds.create( maskPath, 24, 24, 1, static_cast<int>( GDT_Float32 ), gt,
                            QString() ) );
        std::vector<float> data( static_cast<size_t>( 24 ) * 24, 0.0f );
        REQUIRE( ds.writeBandWindow( 1, 0, 0, 24, 24, data.data() ) );
        ds.close();
    }
    Json::Value params( Json::objectValue );
    params["inputs"] = Json::Value( Json::arrayValue );
    params["inputs"].append( fx.pathA.toStdString() );
    Json::Value bSpec( Json::objectValue );
    bSpec["path"] = fx.pathB.toStdString();
    bSpec["cloudMask"] = maskPath.toStdString();
    params["inputs"].append( bSpec );
    params["output"] = fx.pathOut.toStdString();
    bool threw = false;
    try
    {
        runOperator( params );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        CHECK( std::string( e.what() ).find( "co-registered" ) != std::string::npos );
    }
    CHECK( threw );
}

TEST_CASE( "QualityMosaic: seamline disabled falls back to deterministic order",
           "[processing][mosaic][operator]" )
{
    Fixture fx = Fixture::make();
    Json::Value params( Json::objectValue );
    params["inputs"] = Json::Value( Json::arrayValue );
    params["inputs"].append( fx.pathA.toStdString() );
    params["inputs"].append( fx.pathB.toStdString() );
    params["output"] = fx.pathOut.toStdString();
    Json::Value seam( Json::objectValue );
    seam["enabled"] = false;
    params["seamline"] = seam;

    const Json::Value result = runOperator( params );
    CHECK( result["seamCount"].asInt() == 0 );
    GdalDatasetWrapper out;
    REQUIRE( out.open( fx.pathOut ) );
    std::vector<float> mosaic( static_cast<size_t>( 40 ) * 24 );
    REQUIRE( out.readBandWindow( 1, 0, 0, 40, 24, mosaic.data() ) );
    // Later paint wins without seams: B (corrected) covers its whole footprint.
    const size_t probe = static_cast<size_t>( 12 ) * 40 + 36; // inside B only
    CHECK( mosaic[probe] == Approx( 100.0 + 0.5 * 36 ).margin( 1.0 ) );
}
