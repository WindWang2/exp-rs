/***************************************************************************
 * test_operator_preflight_refusals.cpp — R4 fail-fast input validation
 *
 * Track 7 (R4 operator oracles). The RSOperator seam documents
 * "@throws RSOperatorError on validation or execution failure"
 * (rs_operator.h). Each case pins one precondition to a typed refusal with
 * the expected ErrorCode class, so silent-swallow behavior (empty product
 * instead of an error) cannot creep back in. Truth source: the documented
 * refusal contract per operator family.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "support/r4_operator_fixtures.h"

#include <QTemporaryDir>

#include <gdal_priv.h>

#include <string>
#include <vector>

using namespace r4fixtures;
using sicnu::operators::ErrorCode;
using sicnu::operators::RSOperatorError;

namespace
{
/// Float32 GeoTIFF with NO projection and NO geotransform (CRS-less fixture).
QString writeCrsLessRaster( const QString &path, int w, int h,
                            const std::vector<float> &values )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32,
                                  nullptr );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                           const_cast<float *>( values.data() ), w, h, GDT_Float32, 0, 0 )
             == CE_None );
    GDALClose( ds );
    return path;
}

/// Empty FeatureCollection (zero features).
QString writeEmptyGeoJson( const QString &path )
{
    std::ofstream out( path.toStdString() );
    REQUIRE( out.is_open() );
    out << "{\"type\":\"FeatureCollection\",\"features\":[]}";
    return path;
}

} // anonymous namespace

TEST_CASE( "zonal_stats refuses out-of-range bands, CRS-less rasters and empty vectors",
           "[r4][refusal][zonal]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v( 100, 1.0f );
    v[0] = static_cast<float>( kSentinel );
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v }, true,
                                             kSentinel );
    const QString zones = writeZoneGeoJson( dir.filePath( "zones.geojson" ), "zone" );

    // Band 5 of a 1-band raster → typed InvalidParameter naming the domain.
    bool threw = false;
    try
    {
        Json::Value p;
        p["input"] = raster.toStdString();
        p["vector"] = zones.toStdString();
        p["output"] = dir.filePath( "z.csv" ).toStdString();
        Json::Value bands( Json::arrayValue );
        bands.append( 5 );
        p["bands"] = bands;
        runOperator( "rs:zonal_stats", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidParameter;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "band" ) != std::string::npos );
    }
    REQUIRE( threw );

    // CRS-less raster → typed InvalidInputData (CRS/geotransform are the
    // raster-vector family's stated prerequisites).
    const QString crsLess = writeCrsLessRaster( dir.filePath( "nocrs.tif" ), 10, 10, v );
    threw = false;
    try
    {
        Json::Value p;
        p["input"] = crsLess.toStdString();
        p["vector"] = zones.toStdString();
        p["output"] = dir.filePath( "z2.csv" ).toStdString();
        runOperator( "rs:zonal_stats", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidInputData;
        INFO( "message: " << e.message() );
        REQUIRE( ( e.message().find( "CRS" ) != std::string::npos
                   || e.message().find( "geotransform" ) != std::string::npos ) );
    }
    REQUIRE( threw );

    // Empty zone vector → typed InvalidInputData, never a silent empty CSV.
    const QString empty = writeEmptyGeoJson( dir.filePath( "empty.geojson" ) );
    threw = false;
    try
    {
        Json::Value p;
        p["input"] = raster.toStdString();
        p["vector"] = empty.toStdString();
        p["output"] = dir.filePath( "z3.csv" ).toStdString();
        runOperator( "rs:zonal_stats", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidInputData;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "no features" ) != std::string::npos );
    }
    REQUIRE( threw );
}

TEST_CASE( "apply_mask refuses undeclared-NoData bands without an explicit no_data",
           "[r4][refusal][mask]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v( 100, 3.0f );
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v }, false,
                                             0.0 );
    std::vector<uint8_t> mask( 100, 0 );
    mask[4] = 1;
    const QString maskPath = writeByteMask( dir.filePath( "mask.tif" ), 10, 10, mask );

    bool threw = false;
    try
    {
        Json::Value p;
        p["input"] = raster.toStdString();
        p["mask"] = maskPath.toStdString();
        p["output"] = dir.filePath( "out.tif" ).toStdString();
        runOperator( "rs:apply_mask", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        INFO( "message: " << e.message() );
        REQUIRE( ( e.message().find( "NoData" ) != std::string::npos
                   || e.message().find( "no_data" ) != std::string::npos ) );
    }
    REQUIRE( threw );
}

TEST_CASE( "focal_stats and spectral_derivative refuse structurally invalid requests",
           "[r4][refusal][window]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<float> v( 100, 5.0f );
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v }, true,
                                             kSentinel );

    // Even window side → typed InvalidParameter (the contract demands odd).
    bool threw = false;
    try
    {
        Json::Value p;
        p["input"] = raster.toStdString();
        p["output"] = dir.filePath( "f.tif" ).toStdString();
        p["window"] = 4;
        runOperator( "rs:focal_stats", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidParameter;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "odd" ) != std::string::npos );
    }
    REQUIRE( threw );

    // Derivative without WAVELENGTH metadata and without an explicit axis →
    // typed InvalidInputData (a derivative against the band index is not a
    // spectral derivative).
    const std::vector<std::vector<float>> bands = { v, v, v };
    const QString spec = writeFloatRaster( dir.filePath( "spec.tif" ), 10, 10, bands, true,
                                           kSentinel );
    threw = false;
    try
    {
        Json::Value p;
        p["input"] = spec.toStdString();
        p["output"] = dir.filePath( "d.tif" ).toStdString();
        p["order"] = 1;
        runOperator( "rs:spectral_derivative", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidInputData;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "WAVELENGTH" ) != std::string::npos );
    }
    REQUIRE( threw );
}

TEST_CASE( "missing inputs and degenerate band counts are typed refusals",
           "[r4][refusal][preflight]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Non-existent input path → typed FileNotFound, before any GDAL work.
    bool threw = false;
    try
    {
        Json::Value p;
        p["input"] = ( dir.filePath( "missing.tif" ) ).toStdString();
        p["output"] = dir.filePath( "p.tif" ).toStdString();
        runOperator( "rs:pca", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::FileNotFound;
    }
    REQUIRE( threw );

    // PCA on a single-band raster → typed InvalidInputData (needs >= 2 bands).
    std::vector<float> v( 100, 2.0f );
    const QString single = writeFloatRaster( dir.filePath( "single.tif" ), 10, 10, { v },
                                             false, 0.0 );
    threw = false;
    try
    {
        Json::Value p;
        p["input"] = single.toStdString();
        p["output"] = dir.filePath( "p2.tif" ).toStdString();
        runOperator( "rs:pca", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidInputData;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "2 bands" ) != std::string::npos );
    }
    REQUIRE( threw );
}

TEST_CASE( "mask-grid CRS mismatch and missing QA band roles are typed refusals",
           "[r4][refusal][grid]" )
{
    // Closes review P2-6: the two WP-E refusal classes the suite originally
    // dropped — a mask raster on a different CRS (apply_mask aligns
    // nearest-neighbour only within one CRS) and a qa_mask request that
    // needs product band roles the raster does not carry.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // (a) apply_mask: input in EPSG:4326, mask deliberately shifted to a
    // different CRS (UTM 33N WKT) → typed refusal, never a silent align.
    std::vector<float> v( 100, 3.0f );
    const QString raster = writeFloatRaster( dir.filePath( "grid.tif" ), 10, 10, { v },
                                             true, kSentinel );
    std::vector<uint8_t> mask( 100, 0 );
    mask[4] = 1;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const QString maskCrs = dir.filePath( "mask_utm.tif" );
    GDALDatasetH ds = GDALCreate( driver, maskCrs.toUtf8().constData(), 10, 10, 1, GDT_Byte,
                                  nullptr );
    REQUIRE( ds != nullptr );
    const char *utmWkt =
        "PROJCS[\"WGS 84 / UTM zone 33N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
        "SPHEROID[\"WGS 84\",6378137,298.257223563,AUTHORITY[\"EPSG\",\"7030\"]],"
        "AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],"
        "UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],"
        "AUTHORITY[\"EPSG\",\"4326\"]],PROJECTION[\"Transverse_Mercator\"],"
        "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",15],"
        "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
        "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1,AUTHORITY[\"EPSG\",\"9001\"]],"
        "AUTHORITY[\"EPSG\",\"32633\"]]";
    const double utmGt[6] = { 500000.0, 1.0, 0.0, 4000010.0, 0.0, -1.0 };
    REQUIRE( GDALSetGeoTransform( ds, utmGt ) == CE_None );
    REQUIRE( GDALSetProjection( ds, utmWkt ) == CE_None );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 10, 10, mask.data(), 10, 10, GDT_Byte, 0, 0 )
             == CE_None );
    GDALClose( ds );

    bool threw = false;
    try
    {
        Json::Value p;
        p["input"] = raster.toStdString();
        p["mask"] = maskCrs.toStdString();
        p["output"] = dir.filePath( "masked.tif" ).toStdString();
        runOperator( "rs:apply_mask", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "CRS" ) != std::string::npos );
    }
    REQUIRE( threw );

    // (b) qa_mask without qa_band on a raster without product band roles →
    // typed refusal (role resolution has nothing to resolve).
    const QString roleLess = writeU16Raster( dir.filePath( "scl.tif" ), 4, 1,
                                             { 4, 8, 3, 255 }, false, 0.0 );
    threw = false;
    try
    {
        Json::Value p;
        p["input"] = roleLess.toStdString();
        p["output"] = dir.filePath( "qa.tif" ).toStdString();
        p["source"] = "sentinel2_scl";
        runOperator( "rs:qa_mask", p, dir.path().toStdString() );
    }
    catch ( const RSOperatorError &e )
    {
        threw = e.code() == ErrorCode::InvalidParameter;
        INFO( "message: " << e.message() );
        REQUIRE( e.message().find( "QA band" ) != std::string::npos );
    }
    REQUIRE( threw );
}
