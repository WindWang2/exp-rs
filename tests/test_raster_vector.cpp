// tests/test_raster_vector.cpp — rs:rasterize + rs:zonal_stats
// (Scientific Processing 8.0, package D).
//
// Fixtures are GeoJSON zone collections (OGR GeoJSON driver, CRS84) over
// synthetic value rasters; every expectation is analytic (pixel-center
// membership against known grids, linear value fields with closed-form
// statistics). One shared rasterization seam means the two operators must
// agree cell-for-cell on what a geometry covers — that agreement is itself
// asserted.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <json/json.h>

#include <gdal.h>
#include <ogr_spatialref.h>

#include <cmath>#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "geospatial/crs/crs_policy.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::operators;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_raster_vector";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

bool writeRaster( const QString &path, const std::vector<float> &values, int width, int height,
                  const double gt[6], const char *wkt, float nodata = 0.0f,
                  bool withNodata = false )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    if ( wkt )
        GDALSetProjection( ds, wkt );
    if ( withNodata )
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), nodata );
    if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                       const_cast<float *>( values.data() ), width, height, GDT_Float32,
                       0, 0 ) != CE_None )
    {
        GDALClose( ds );
        return false;
    }
    GDALClose( ds );
    return true;
}

bool writeGeoJson( const QString &path, const std::string &geojson )
{
    QFile f( path );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
        return false;
    QTextStream ts( &f );
    ts << QString::fromStdString( geojson );
    return true;
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, out.data(), ds.width(), ds.height() ) )
        return {};
    return out;
}

std::string readCsv( const QString &path )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
        return {};
    QTextStream ts( &f );
    return ts.readAll().toStdString();
}

const char *epsg4326Wkt()
{
    static std::string wkt;
    if ( wkt.empty() )
    {
        OGRSpatialReference srs;
        srs.importFromEPSG( 4326 );
        char *out = nullptr;
        srs.exportToWkt( &out );
        wkt = out;
        CPLFree( out );
    }
    return wkt.c_str();
}

const char *epsg32650Wkt()
{
    static std::string wkt;
    if ( wkt.empty() )
    {
        OGRSpatialReference srs;
        srs.importFromEPSG( 32650 );
        char *out = nullptr;
        srs.exportToWkt( &out );
        wkt = out;
        CPLFree( out );
    }
    return wkt.c_str();
}

std::string rectFeature( const std::string &id, const std::string &zone,
                         const std::string &props, double minX, double minY,
                         double maxX, double maxY )
{
    // Proper GeoJSON polygon: array of rings, each ring an array of [x,y]
    // positions, closed.
    auto pos = []( double x, double y ) {
        return "[" + std::to_string( x ) + "," + std::to_string( y ) + "]";
    };
    const std::string ring = "[" + pos( minX, minY ) + "," + pos( maxX, minY ) + ","
                             + pos( maxX, maxY ) + "," + pos( minX, maxY ) + ","
                             + pos( minX, minY ) + "]";
    return R"( {"type":"Feature","id":)" + id + R"(,"properties":{)"
           + ( zone.empty() ? "" : R"("zone":")" + zone + R"(",)" ) + props
           + R"(},"geometry":{"type":"Polygon","coordinates":[)" + ring + "]} }";
}

std::string featureCollection( const std::vector<std::string> &features )
{
    std::string out = R"({"type":"FeatureCollection","features":[)";
    for ( size_t i = 0; i < features.size(); ++i )
    {
        if ( i )
            out += ",";
        out += features[i];
    }
    out += "]}";
    return out;
}

Json::Value parseJson( const std::string &text )
{
    Json::Value v;
    Json::Reader reader;
    reader.parse( text, v );
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// rs:rasterize
// ---------------------------------------------------------------------------

TEST_CASE( "rs:rasterize burns constant and attribute values with last-wins overlap",
           "[raster-vector][rasterize][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 10x10 grid, 1-degree cells, origin (10, 20).
    double gt[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    std::vector<float> dummy( 100, 0.0f );
    const QString ref = tmp.filePath( "ref.tif" );
    REQUIRE( writeRaster( ref, dummy, 10, 10, gt, epsg4326Wkt() ) );

    // Zone a covers cols {1,2,3} x rows {2,3,4}; zone b overlaps cols {2,3}
    // and wins there (later feature). Attribute `val`: 2.0 / 3.0.
    const std::string geojson = featureCollection( {
        rectFeature( "1", "a", R"("val":2.0)", 10.55, 14.60, 13.55, 17.60 ),
        rectFeature( "2", "b", R"("val":3.0)", 11.55, 14.60, 14.55, 17.60 ),
    } );
    const QString zones = tmp.filePath( "zones.geojson" );
    REQUIRE( writeGeoJson( zones, geojson ) );

    auto op = RSOperatorRegistry::instance().create( "rs:rasterize" );
    REQUIRE( op != nullptr );

    SECTION( "constant burn" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = ref.toStdString();
        params["vector"] = zones.toStdString();
        params["value"] = 5.0;
        params["output"] = tmp.filePath( "burn_const.tif" ).toStdString();
        RSOperatorContext ctx;
        Json::Value result;
        REQUIRE_NOTHROW( result = op->run( params, ctx ) );
        // a: 9 cells + b: 9 cells, overlap 2 cols x 3 rows = 6 → 12 unique.
        REQUIRE( result["burnedPixels"].asUInt64() == 12ULL );

        const auto burned = readBand( tmp.filePath( "burn_const.tif" ) );
        REQUIRE( burned.size() == 100 );
        int hits = 0;
        for ( int r = 0; r < 10; ++r )
            for ( int c = 0; c < 10; ++c )
            {
                const float v = burned[static_cast<size_t>( r ) * 10 + c];
                if ( std::isfinite( v ) )
                {
                    ++hits;
                    REQUIRE( v == 5.0f );
                }
            }
        REQUIRE( hits == 12 );
    }

    SECTION( "attribute burn, last wins" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = ref.toStdString();
        params["vector"] = zones.toStdString();
        params["field"] = "val";
        params["output"] = tmp.filePath( "burn_field.tif" ).toStdString();
        RSOperatorContext ctx;
        Json::Value result;
        REQUIRE_NOTHROW( result = op->run( params, ctx ) );

        const auto burned = readBand( tmp.filePath( "burn_field.tif" ) );
        auto at = [&]( int r, int c ) {
            return burned[static_cast<size_t>( r ) * 10 + c];
        };
        // a-only cells (col 1, rows 2..4) carry 2.0.
        for ( const int r : { 2, 3, 4 } )
            REQUIRE( at( r, 1 ) == 2.0f );
        // b-only cell (col 4, rows 2..4) and overlap cells carry 3.0.
        for ( const int r : { 2, 3, 4 } )
        {
            REQUIRE( at( r, 4 ) == 3.0f );
            REQUIRE( at( r, 2 ) == 3.0f );
            REQUIRE( at( r, 3 ) == 3.0f );
        }
        // Unburned cells are NaN, never zero.
        REQUIRE( std::isnan( at( 0, 0 ) ) );
        REQUIRE( result["burnedPixels"].asUInt64() == 12ULL );
    }

    SECTION( "allTouched extends the burn to boundary cells" )
    {
        // A polygon whose edge cuts through pixel centers: center-selection
        // burns only cells whose centers fall inside; ALL_TOUCHED burns
        // every cell the geometry touches.
        const std::string line = featureCollection( {
            rectFeature( "1", "edge", R"("val":1.0)", 10.5, 14.0, 13.5, 17.0 ),
        } );
        const QString edge = tmp.filePath( "edge.geojson" );
        REQUIRE( writeGeoJson( edge, line ) );
        RSOperatorContext ctx;

        Json::Value center( Json::objectValue );
        center["input"] = ref.toStdString();
        center["vector"] = edge.toStdString();
        center["output"] = tmp.filePath( "burn_center.tif" ).toStdString();
        Json::Value rCenter;
        REQUIRE_NOTHROW( rCenter = op->run( center, ctx ) );

        Json::Value touched( center );
        touched["allTouched"] = true;
        touched["output"] = tmp.filePath( "burn_touched.tif" ).toStdString();
        Json::Value rTouched;
        REQUIRE_NOTHROW( rTouched = op->run( touched, ctx ) );

        REQUIRE( rTouched["burnedPixels"].asUInt64() > rCenter["burnedPixels"].asUInt64() );
    }
}

TEST_CASE( "rs:rasterize refusals: non-numeric field, missing CRS, rotated grid",
           "[raster-vector][rasterize][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    double gt[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    std::vector<float> dummy( 100, 0.0f );
    const QString ref = tmp.filePath( "ref.tif" );
    REQUIRE( writeRaster( ref, dummy, 10, 10, gt, epsg4326Wkt() ) );

    auto op = RSOperatorRegistry::instance().create( "rs:rasterize" );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;

    // Non-numeric attribute is a typed refusal, never a silent coercion.
    const std::string bad = featureCollection( {
        rectFeature( "1", "", R"("val":"high")", 10.6, 14.6, 13.4, 17.4 ),
    } );
    const QString badZones = tmp.filePath( "bad.geojson" );
    REQUIRE( writeGeoJson( badZones, bad ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = ref.toStdString();
        p["vector"] = badZones.toStdString();
        p["field"] = "val";
        p["output"] = tmp.filePath( "out1.tif" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }

    // Reference raster without CRS.
    const QString noCrs = tmp.filePath( "nocrs.tif" );
    REQUIRE( writeRaster( noCrs, dummy, 10, 10, gt, nullptr ) );
    const std::string good = featureCollection( {
        rectFeature( "1", "", R"("val":1.0)", 10.6, 14.6, 13.4, 17.4 ),
    } );
    const QString zones = tmp.filePath( "zones.geojson" );
    REQUIRE( writeGeoJson( zones, good ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = noCrs.toStdString();
        p["vector"] = zones.toStdString();
        p["output"] = tmp.filePath( "out2.tif" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }

    // Rotated geotransform.
    double rot[6] = { 10.0, 1.0, 0.1, 20.0, 0.2, -1.0 };
    const QString rotated = tmp.filePath( "rot.tif" );
    REQUIRE( writeRaster( rotated, dummy, 10, 10, rot, epsg4326Wkt() ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = rotated.toStdString();
        p["vector"] = zones.toStdString();
        p["output"] = tmp.filePath( "out3.tif" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }
}

// ---------------------------------------------------------------------------
// rs:zonal_stats
// ---------------------------------------------------------------------------

TEST_CASE( "rs:zonal_stats computes exact statistics; zones spanning windows accumulate",
           "[raster-vector][zonal][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 512x512 grid (4 windows), 0.01-degree cells, v = col + 1000·row.
    constexpr int kW = 512;
    constexpr int kH = 512;
    double gt[6] = { 10.0, 0.01, 0.0, 40.0, 0.0, -0.01 };
    std::vector<float> values( static_cast<size_t>( kW ) * kH );
    for ( int r = 0; r < kH; ++r )
        for ( int c = 0; c < kW; ++c )
            values[static_cast<size_t>( r ) * kW + c] = static_cast<float>( c + 1000 * r );
    const QString ref = tmp.filePath( "values.tif" );
    REQUIRE( writeRaster( ref, values, kW, kH, gt, epsg4326Wkt() ) );

    // One zone spanning the window boundary at col 256: lon (12.0, 13.0)
    // → cols with centers in the open interval: center = 10.005 + 0.01·c
    // ∈ (12.0, 13.0) → c ∈ (199.5, 299.5) → {200..299}. lat (35.0, 36.0)
    // → centers 39.995 − 0.01·r ∈ (35.0, 36.0) → r ∈ (399.5, 499.5) →
    // {400..499}.
    const std::string geojson = featureCollection( {
        rectFeature( "1", "span", R"("note":"crosses windows")", 12.0, 35.0, 13.0, 36.0 ),
    } );
    const QString zones = tmp.filePath( "zones.geojson" );
    REQUIRE( writeGeoJson( zones, geojson ) );

    auto op = RSOperatorRegistry::instance().create( "rs:zonal_stats" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = ref.toStdString();
    params["vector"] = zones.toStdString();
    params["zoneField"] = "zone";
    params["output"] = tmp.filePath( "stats.csv" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );
    REQUIRE( result["zones"].asUInt64() == 1ULL );
    REQUIRE( result["rows"].asUInt64() == 1ULL );

    // Analytic expectations over 100x100 = 10000 cells: each column value
    // appears 100x and each row value 100x, so the mean is the mean of the
    // column values plus 1000x the mean of the row values.
    const double count = 10000.0;
    const double meanC = ( 200 + 299 ) / 2.0;  // 249.5
    const double meanR = ( 400 + 499 ) / 2.0;  // 449.5
    const double expectedMean = meanC + 1000.0 * meanR;
    const double expectedMin = 200.0 + 1000.0 * 400.0;
    const double expectedMax = 299.0 + 1000.0 * 499.0;

    const std::string csv = readCsv( tmp.filePath( "stats.csv" ) );
    REQUIRE_FALSE( csv.empty() );
    const Json::Value rows = [ &csv ] {
        // Minimal CSV parse of the single data row.
        Json::Value out( Json::arrayValue );
        std::istringstream stream( csv );
        std::string line;
        std::getline( stream, line ); // header
        while ( std::getline( stream, line ) )
        {
            Json::Value row( Json::arrayValue );
            std::string field;
            std::istringstream ls( line );
            while ( std::getline( ls, field, ',' ) )
                row.append( field );
            out.append( row );
        }
        return out;
    }();
    REQUIRE( rows.size() == 1 );
    const Json::Value &row = rows[0];
    REQUIRE( row[0].asString() == "span" );
    REQUIRE( row[1].asString() == "1" );
    REQUIRE( row[2].asString() == "10000" );
    REQUIRE( row[3].asString() == "0" );
    REQUIRE( std::fabs( std::stod( row[4].asString() ) - expectedMin ) < 1e-3 );
    REQUIRE( std::fabs( std::stod( row[5].asString() ) - expectedMax ) < 1e-3 );
    REQUIRE( std::fabs( std::stod( row[6].asString() ) - expectedMean ) < 1e-3 );
    // Population stddev computed from the same closed form.
    double sumSq = 0.0;
    for ( double c = 200; c <= 299; ++c )
        for ( double r = 400; r <= 499; ++r )
        {
            const double d = c + 1000.0 * r - expectedMean;
            sumSq += d * d;
        }
    const double expectedStd = std::sqrt( sumSq / count );
    REQUIRE( std::fabs( std::stod( row[7].asString() ) - expectedStd ) < 1e-3 );
    // Median of an even count = mean of the two middle values.
    REQUIRE( std::fabs( std::stod( row[8].asString() ) - 0.5 * ( expectedMin + expectedMax ) )
             < 1e-3 );
    REQUIRE( row[9].asString() == "0" ); // median_truncated
}

TEST_CASE( "rs:zonal_stats: overlapping zones last-wins, sentinel exclusion, "
           "empty zones report, geometryless features counted",
           "[raster-vector][zonal][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 10x10 grid; v = col + 10·row; declared sentinel -9999.
    double gt[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    std::vector<float> values( 100 );
    for ( int r = 0; r < 10; ++r )
        for ( int c = 0; c < 10; ++c )
            values[static_cast<size_t>( r ) * 10 + c] =
                ( r == 3 && c == 2 ) ? -9999.0f : static_cast<float>( c + 10 * r );
    const QString ref = tmp.filePath( "values.tif" );
    REQUIRE( writeRaster( ref, values, 10, 10, gt, epsg4326Wkt(), -9999.0f, true ) );

    // a: cols {1..3} x rows {2..4}; b (subset rect) overlaps cols {2,3}
    // and wins there; c: exactly one grid cell; n: a zone over ONLY the
    // sentinel cell (zero valid pixels — still reports, count 0);
    // d: geometryless feature.
    const std::string geojson = featureCollection( {
        rectFeature( "1", "a", R"("val":1.0)", 10.55, 14.60, 13.55, 17.60 ),
        rectFeature( "2", "b", R"("val":2.0)", 11.55, 14.60, 13.6, 17.60 ),
        rectFeature( "3", "c", R"("val":3.0)", 15.1, 14.1, 15.9, 14.9 ),
        rectFeature( "4", "n", R"("val":9.0)", 12.1, 16.1, 12.9, 16.9 ),
        R"( {"type":"Feature","id":5,"properties":{"zone":"d","val":4.0},"geometry":null} )",
    } );
    const QString zones = tmp.filePath( "zones.geojson" );
    REQUIRE( writeGeoJson( zones, geojson ) );

    auto op = RSOperatorRegistry::instance().create( "rs:zonal_stats" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = ref.toStdString();
    params["vector"] = zones.toStdString();
    params["zoneField"] = "zone";
    params["output"] = tmp.filePath( "stats.csv" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );

    REQUIRE( result["geometrylessFeatures"].asUInt64() == 1ULL );
    REQUIRE( result["zones"].asUInt64() == 4ULL ); // a, b, c, n
    REQUIRE( result["rows"].asUInt64() == 4ULL );

    const std::string csv = readCsv( tmp.filePath( "stats.csv" ) );
    // Zone a: cols {1,2,3} x rows {2,3,4} = 9 cells; the overlap with b
    // (cols {2,3}) belongs to b (last wins) → a keeps col 1: values
    // 21/31/41 → 3 valid, 0 nodata, mean 31, median 31.
    {
        const size_t pos = csv.find( "a,1,3,0," );
        REQUIRE( pos != std::string::npos );
        const size_t eol = csv.find( '\n', pos );
        const std::string row = csv.substr( pos, eol - pos );
        const std::string expectedPrefix = "a,1,3,0,21,41,31,8.164965809,31,0";
        REQUIRE( row.rfind( expectedPrefix, 0 ) == 0 );
    }
    // Zone b: cols {2,3} x rows {2,3,4} = 6 cells; the sentinel cell is
    // re-assigned to the later zone n (last wins) → 5 valid, 0 nodata;
    // mean of 22,23,33,42,43 = 32.6.
    {
        const size_t pos = csv.find( "b,1,5,0," );
        REQUIRE( pos != std::string::npos );
        const size_t eol = csv.find( '\n', pos );
        const std::string row = csv.substr( pos, eol - pos );
        const std::string expectedPrefix = "b,1,5,0,22,43,32.6,8.957678271,33,0";
        REQUIRE( row.rfind( expectedPrefix, 0 ) == 0 );
    }
    // Zone c: exactly one cell (row center 14.5, col center 15.5) → v=55;
    // stddev of a single sample is 0.
    REQUIRE( csv.find( "c,1,1,0,55,55,55,0,55,0" ) != std::string::npos );
    // Zone n: covers only the sentinel cell → count 0 row still reports.
    REQUIRE( csv.find( "n,1,0,1,nan,nan,nan,nan,nan,0" ) != std::string::npos );
}


TEST_CASE( "rs:rasterize + rs:zonal_stats handle vectors beyond one batch (>1024 features)",
           "[raster-vector][regression]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 10x10 grid, v = col + 10*row; 1200 point features over distinct
    // cells (40 x 30 = 1200 of the 100 cells — points share cells, so
    // expected valid_pixels counts the DISTINCT burned cells with
    // last-wins semantics: every cell of the grid is covered).
    double gt[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    std::vector<float> values( 100 );
    for ( int r = 0; r < 10; ++r )
        for ( int c = 0; c < 10; ++c )
            values[static_cast<size_t>( r ) * 10 + c] = static_cast<float>( c + 10 * r );
    const QString ref = tmp.filePath( "values.tif" );
    REQUIRE( writeRaster( ref, values, 10, 10, gt, epsg4326Wkt() ) );

    std::vector<std::string> feats;
    for ( int i = 0; i < 1200; ++i )
    {
        const int cell = i % 100;
        const int c = cell % 10;
        const int r = cell / 10;
        const double x = 10.5 + c; // cell centers
        const double y = 19.5 - r;
        feats.push_back( R"( {"type":"Feature","id":)" + std::to_string( i )
                         + R"(,"properties":{"zone":"p","val":1.0},)"
                         + R"("geometry":{"type":"Point","coordinates":[)"
                         + std::to_string( x ) + "," + std::to_string( y ) + "]} }" );
    }
    const QString zones = tmp.filePath( "points.geojson" );
    REQUIRE( writeGeoJson( zones, featureCollection( feats ) ) );

    auto rasterize = RSOperatorRegistry::instance().create( "rs:rasterize" );
    auto zonal = RSOperatorRegistry::instance().create( "rs:zonal_stats" );
    REQUIRE( rasterize != nullptr );
    REQUIRE( zonal != nullptr );
    RSOperatorContext ctx;

    // Points burn with ALL_TOUCHED semantics via GDAL (a point touches the
    // pixel containing it); center-selection would drop points between
    // centers — the operator default burns points reliably.
    Json::Value rp( Json::objectValue );
    rp["input"] = ref.toStdString();
    rp["vector"] = zones.toStdString();
    rp["value"] = 5.0;
    rp["allTouched"] = true;
    rp["output"] = tmp.filePath( "points_burn.tif" ).toStdString();
    Json::Value rResult;
    REQUIRE_NOTHROW( rResult = rasterize->run( rp, ctx ) );
    // 1200 features streamed across 2 batches: exactly 1200 cached (the
    // batch.append bug would re-process earlier batches and inflate this).
    REQUIRE( rResult["features"].asUInt64() == 1200ULL );
    REQUIRE( rResult["burnedPixels"].asUInt64() == 100ULL );

    Json::Value zp( Json::objectValue );
    zp["input"] = ref.toStdString();
    zp["vector"] = zones.toStdString();
    zp["zoneField"] = "zone";
    zp["output"] = tmp.filePath( "points_stats.csv" ).toStdString();
    Json::Value zResult;
    REQUIRE_NOTHROW( zResult = zonal->run( zp, ctx ) );
    REQUIRE( zResult["features"].asUInt64() == 1200ULL );
    // The point zone (all 100 cells) is handled through the shared seam.
    const std::string csv = readCsv( tmp.filePath( "points_stats.csv" ) );
    REQUIRE( csv.find( "p,1,100,0," ) != std::string::npos );
}

TEST_CASE( "rs:zonal_stats transforms zone CRS through the foundation policy",
           "[raster-vector][zonal][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // UTM 50N grid, 10 m cells, origin (300000, 2500000).
    double gt[6] = { 300000.0, 10.0, 0.0, 2500000.0, 0.0, -10.0 };
    constexpr int kW = 100;
    constexpr int kH = 100;
    std::vector<float> values( static_cast<size_t>( kW ) * kH );
    for ( int r = 0; r < kH; ++r )
        for ( int c = 0; c < kW; ++c )
            values[static_cast<size_t>( r ) * kW + c] = 7.0f;
    const QString ref = tmp.filePath( "utm.tif" );
    REQUIRE( writeRaster( ref, values, kW, kH, gt, epsg32650Wkt() ) );

    // Zone: the projected rectangle covering cols {1,2,3} x rows {1,2,3}
    // (eastings 300010..300040, northings 2499960..2499990), transformed
    // to CRS84 through OGR with the same foundation policy the operator
    // uses. The geographic bbox of the four corners bounds the rectangle
    // (the box is tiny), so the zone covers exactly those 9 cells.
    double minX = 300010.0, minY = 2499960.0, maxX = 300040.0, maxY = 2499990.0;
    // Transform through the FOUNDATION policy (which normalizes axis order)
    // — raw OCT handles would carry authority-order lat/lon on the
    // geographic end and swap the polygon coordinates.
    const sicnu::geo::CrsTransform toGeographic = sicnu::geo::CrsTransform::create(
        sicnu::geo::Crs::fromAuthid( "EPSG:32650" ),
        sicnu::geo::Crs::fromAuthid( "EPSG:4326" ),
        sicnu::geo::AxisOrder::TraditionalGis );
    double minXg = 1e300, minYg = 1e300, maxXg = -1e300, maxYg = -1e300;
    const double corners[4][2] = { { minX, minY }, { maxX, minY },
                                   { maxX, maxY }, { minX, maxY } };
    for ( const auto &corner : corners )
    {
        const sicnu::geo::CrsPoint p = toGeographic.forward( { corner[0], corner[1] } );
        minXg = std::min( minXg, p.x );
        maxXg = std::max( maxXg, p.x );
        minYg = std::min( minYg, p.y );
        maxYg = std::max( maxYg, p.y );
    }

    const std::string geojson = featureCollection( {
        rectFeature( "1", "u", R"("val":1.0)", minXg, minYg, maxXg, maxYg ),
    } );
    const QString zones = tmp.filePath( "zones.geojson" );
    REQUIRE( writeGeoJson( zones, geojson ) );

    auto op = RSOperatorRegistry::instance().create( "rs:zonal_stats" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = ref.toStdString();
    params["vector"] = zones.toStdString();
    params["zoneField"] = "zone";
    params["output"] = tmp.filePath( "stats.csv" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );

    // The CRS84 zone transforms back onto the projected rectangle: exactly
    // the 9 interior cells report, all with the constant value 7.
    const std::string csv = readCsv( tmp.filePath( "stats.csv" ) );
    REQUIRE( csv.find( "u,1,9,0," ) != std::string::npos );
    REQUIRE( csv.find( ",7,7,7,0,7,0\n" ) != std::string::npos );
    REQUIRE( result["outsideGridFeatures"].asUInt64() == 0ULL );
}

TEST_CASE( "rs:zonal_stats refusals: missing zone field, empty vector, no CRS",
           "[raster-vector][zonal][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    double gt[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
    std::vector<float> values( 100, 1.0f );
    const QString ref = tmp.filePath( "values.tif" );
    REQUIRE( writeRaster( ref, values, 10, 10, gt, epsg4326Wkt() ) );

    auto op = RSOperatorRegistry::instance().create( "rs:zonal_stats" );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;

    // A feature lacking the declared zone field is a typed refusal.
    const std::string missing = featureCollection( {
        rectFeature( "1", "a", R"("val":1.0)", 10.6, 14.6, 13.4, 17.4 ),
        rectFeature( "2", "", R"("val":1.0)", 10.6, 14.6, 13.4, 17.4 ),
    } );
    const QString missingZones = tmp.filePath( "missing.geojson" );
    REQUIRE( writeGeoJson( missingZones, missing ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = ref.toStdString();
        p["vector"] = missingZones.toStdString();
        p["zoneField"] = "zone";
        p["output"] = tmp.filePath( "s1.csv" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }

    // Empty feature collection.
    const QString empty = tmp.filePath( "empty.geojson" );
    REQUIRE( writeGeoJson( empty, featureCollection( {} ) ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = ref.toStdString();
        p["vector"] = empty.toStdString();
        p["output"] = tmp.filePath( "s2.csv" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }

    // Value raster without CRS.
    const QString noCrs = tmp.filePath( "nocrs.tif" );
    REQUIRE( writeRaster( noCrs, values, 10, 10, gt, nullptr ) );
    const std::string good = featureCollection( {
        rectFeature( "1", "a", R"("val":1.0)", 10.6, 14.6, 13.4, 17.4 ),
    } );
    const QString zones = tmp.filePath( "zones.geojson" );
    REQUIRE( writeGeoJson( zones, good ) );
    {
        Json::Value p( Json::objectValue );
        p["input"] = noCrs.toStdString();
        p["vector"] = zones.toStdString();
        p["output"] = tmp.filePath( "s3.csv" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }
}
