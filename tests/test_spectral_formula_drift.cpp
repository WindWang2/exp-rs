// tests/test_spectral_formula_drift.cpp — mechanical formula/catalog drift
// guard (Scientific Processing 8.0, package E).
//
// Every index in the rs:spectral_index schema enum must have a row in the
// drift table below: an independent implementation of the DOCUMENTED
// formula (literature constants, written here — never copied from the
// kernels) plus an operator-level probe. The test then enforces:
//
//   1. schema enum == table coverage (adding an index without pinning its
//      formula, or deleting a kernel without dropping the row, fails);
//   2. operator output == independent formula on unit-reflectance probes
//      (constant changes in the kernels are drift and fail);
//   3. NaN contracts on degenerate denominators ("undefined ≠ zero").
//
// Changing a formula constant now requires updating BOTH the kernel AND the
// independent probe here — an intentional two-key rule.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
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
char appArgv0[] = "test_spectral_formula_drift";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

// Band values for the unit-reflectance probe raster (7 bands, 2x2 each):
// blue, green, red, nir, swir, swir2, rededge — chosen so every index
// denominator is nonzero and no index degenerates.
struct BandValues
{
    double blue = 0.10;
    double green = 0.20;
    double red = 0.30;
    double nir = 0.40;
    double swir = 0.50;
    double swir2 = 0.60;
    double rededge = 0.55;
};

double nd( double a, double b ) { return ( a + b ) == 0.0 ? nan( "" ) : ( a - b ) / ( a + b ); }

using Formula = std::function<double( const BandValues & )>;

struct DriftRow
{
    const char *name;
    const char *formula; // documented formula (human-readable authority)
    Formula expected;
};

const std::vector<DriftRow> &driftTable()
{
    static const std::vector<DriftRow> table = {
        { "NDVI", "(NIR-Red)/(NIR+Red)",
          []( const BandValues &v ) { return nd( v.nir, v.red ); } },
        { "EVI", "2.5(NIR-Red)/(NIR+6Red-7.5Blue+1)",
          []( const BandValues &v ) { return 2.5 * ( v.nir - v.red ) / ( v.nir + 6.0 * v.red - 7.5 * v.blue + 1.0 ); } },
        { "SAVI", "1.5(NIR-Red)/(NIR+Red+0.5)",
          []( const BandValues &v ) { return 1.5 * ( v.nir - v.red ) / ( v.nir + v.red + 0.5 ); } },
        { "NDWI", "(Green-NIR)/(Green+NIR)",
          []( const BandValues &v ) { return nd( v.green, v.nir ); } },
        { "NDBI", "(SWIR-NIR)/(SWIR+NIR)",
          []( const BandValues &v ) { return nd( v.swir, v.nir ); } },
        { "MNDWI", "(Green-SWIR)/(Green+SWIR)",
          []( const BandValues &v ) { return nd( v.green, v.swir ); } },
        { "NBR", "(NIR-SWIR2)/(NIR+SWIR2)",
          []( const BandValues &v ) { return nd( v.nir, v.swir2 ); } },
        { "BSI", "((SWIR+Red)-(NIR+Blue))/((SWIR+Red)+(NIR+Blue))",
          []( const BandValues &v ) {
              const double num = ( v.swir + v.red ) - ( v.nir + v.blue );
              const double den = ( v.swir + v.red ) + ( v.nir + v.blue );
              return den == 0.0 ? nan( "" ) : num / den;
          } },
        { "NDRE", "(NIR-RedEdge)/(NIR+RedEdge)",
          []( const BandValues &v ) { return nd( v.nir, v.rededge ); } },
        { "CI", "NIR/RedEdge - 1",
          []( const BandValues &v ) { return v.rededge == 0.0 ? nan( "" ) : v.nir / v.rededge - 1.0; } },
        { "NDSI", "(Green-SWIR)/(Green+SWIR)",
          []( const BandValues &v ) { return nd( v.green, v.swir ); } },
        { "NDTI", "(SWIR-SWIR2)/(SWIR+SWIR2)",
          []( const BandValues &v ) { return nd( v.swir, v.swir2 ); } },
        { "GNDVI", "(NIR-Green)/(NIR+Green)",
          []( const BandValues &v ) { return nd( v.nir, v.green ); } },
        { "NDMI", "(NIR-SWIR)/(NIR+SWIR)",
          []( const BandValues &v ) { return nd( v.nir, v.swir ); } },
        { "MSAVI", "(2NIR+1-sqrt((2NIR+1)^2-8(NIR-Red)))/2",
          []( const BandValues &v ) {
              const double term = 2.0 * v.nir + 1.0;
              const double discr = term * term - 8.0 * ( v.nir - v.red );
              return discr < 0.0 ? nan( "" ) : ( term - std::sqrt( discr ) ) / 2.0;
          } },
        { "ARVI", "(NIR-(2Red-Blue))/(NIR+(2Red-Blue))",
          []( const BandValues &v ) {
              const double rb = 2.0 * v.red - v.blue;
              return nd( v.nir, rb );
          } },
        { "EVI2", "2.5(NIR-Red)/(NIR+2.4Red+1)",
          []( const BandValues &v ) { return 2.5 * ( v.nir - v.red ) / ( v.nir + 2.4 * v.red + 1.0 ); } },
        { "BAI", "1/((0.1-Red)^2+(0.06-NIR)^2)",
          []( const BandValues &v ) {
              const double den = ( 0.1 - v.red ) * ( 0.1 - v.red )
                                 + ( 0.06 - v.nir ) * ( 0.06 - v.nir );
              return den == 0.0 ? nan( "" ) : 1.0 / den;
          } },
        { "UI", "(SWIR2-NIR)/(SWIR2+NIR)",
          []( const BandValues &v ) { return nd( v.swir2, v.nir ); } },
        { "BUI", "NDBI - NDVI",
          []( const BandValues &v ) { return nd( v.swir, v.nir ) - nd( v.nir, v.red ); } },
        // dNBR = NBR_pre - NBR_post spans two rasters; probed separately
        // below, but its row must exist so the schema enum stays covered.
        { "dNBR", "NBR(pre) - NBR(post)",
          []( const BandValues & ) { return nan( "" ); } },
    };
    return table;
}

bool writeMultiBandConst( const QString &path, const std::vector<double> &bandValues,
                          int width, int height )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height,
                                  static_cast<int>( bandValues.size() ), GDT_Float32, nullptr );
    if ( !ds )
        return false;
    for ( size_t b = 0; b < bandValues.size(); ++b )
    {
        std::vector<float> plane( static_cast<size_t>( width ) * height,
                                  static_cast<float>( bandValues[b] ) );
        if ( GDALRasterIO( GDALGetRasterBand( ds, static_cast<int>( b ) + 1 ), GF_Write, 0, 0,
                           width, height, plane.data(), width, height, GDT_Float32, 0, 0 )
             != CE_None )
        {
            GDALClose( ds );
            return false;
        }
    }
    GDALClose( ds );
    return true;
}

std::vector<float> readBand( const QString &path, int band )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, out.data(), ds.width(), ds.height() ) )
        return {};
    return out;
}

} // namespace

TEST_CASE( "spectral formula drift guard: schema enum and drift table agree",
           "[spectral][drift][guard]" )
{
    const AppInit app;
    auto op = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    const Json::Value schema = op->schema();
    REQUIRE( schema["properties"]["index"]["enum"].isArray() );
    std::set<std::string> enumSet;
    for ( const auto &e : schema["properties"]["index"]["enum"] )
        enumSet.insert( e.asString() );

    std::set<std::string> tableSet;
    for ( const DriftRow &row : driftTable() )
        tableSet.insert( row.name );

    // The dispatch handles dNBR across two rasters (not a single-grid
    // formula probe) — it is pinned separately below; every OTHER enum
    // value needs a table row, and vice versa.
    std::set<std::string> expectedTable = enumSet;
    expectedTable.insert( "dNBR" );
    REQUIRE( tableSet == expectedTable );
}

TEST_CASE( "spectral formula drift guard: operator matches the documented formulas",
           "[spectral][drift][guard][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const BandValues v;
    const QString input = tmp.filePath( "probe.tif" );
    REQUIRE( writeMultiBandConst( input,
                                  { v.blue, v.green, v.red, v.nir, v.swir, v.swir2,
                                    v.rededge },
                                  2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( op != nullptr );

    for ( const DriftRow &row : driftTable() )
    {
        if ( std::string( row.name ) == "dNBR" )
            continue; // two-raster probe below
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = ( tmp.filePath( "out_" ) + row.name + ".tif" ).toStdString();
        params["index"] = row.name;
        params["blue"] = 1;
        params["green"] = 2;
        params["red"] = 3;
        params["nir"] = 4;
        params["swir"] = 5;
        params["swir2"] = 6;
        params["rededge"] = 7;
        RSOperatorContext ctx;
        Json::Value result;
        INFO( "index " << row.name << " (" << row.formula << ")" );
        REQUIRE_NOTHROW( result = op->run( params, ctx ) );
        const auto values = readBand( QString::fromStdString( params["output"].asString() ), 1 );
        REQUIRE( values.size() == 4 );
        const double expected = row.expected( v );
        for ( float value : values )
        {
            if ( std::isnan( expected ) )
                REQUIRE( std::isnan( value ) );
            else
                REQUIRE( value == Approx( static_cast<float>( expected ) ).epsilon( 1e-5 ) );
        }
    }
}

TEST_CASE( "spectral formula drift guard: dNBR is pre-NBR minus post-NBR",
           "[spectral][drift][guard]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const BandValues v;
    const QString pre = tmp.filePath( "pre.tif" );
    const QString post = tmp.filePath( "post.tif" );
    REQUIRE( writeMultiBandConst( pre,
                                  { v.blue, v.green, v.red, v.nir, v.swir, v.swir2,
                                    v.rededge },
                                  2, 2 ) );
    BandValues postV;
    postV.nir = 0.25; // burned: NIR drops, SWIR2 rises
    postV.swir2 = 0.70;
    postV.rededge = 0.35;
    REQUIRE( writeMultiBandConst( post,
                                  { postV.blue, postV.green, postV.red, postV.nir, postV.swir,
                                    postV.swir2, postV.rededge },
                                  2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = pre.toStdString();
    params["postfire"] = post.toStdString();
    params["output"] = tmp.filePath( "dnbr.tif" ).toStdString();
    params["index"] = "dNBR";
    params["blue"] = 1;
    params["green"] = 2;
    params["red"] = 3;
    params["nir"] = 4;
    params["swir"] = 5;
    params["swir2"] = 6;
    params["rededge"] = 7;
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );

    const auto values = readBand( tmp.filePath( "dnbr.tif" ), 1 );
    REQUIRE( values.size() == 4 );
    const double expected = nd( v.nir, v.swir2 ) - nd( postV.nir, postV.swir2 );
    for ( float value : values )
        REQUIRE( value == Approx( static_cast<float>( expected ) ).epsilon( 1e-5 ) );
}

TEST_CASE( "spectral formula drift guard: degenerate denominators are NaN, never zero",
           "[spectral][drift][guard]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // red == -nir AND red == nir == 0 makes the NDVI denominator exactly 0.
    const QString input = tmp.filePath( "degenerate.tif" );
    REQUIRE( writeMultiBandConst( input, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }, 2, 2 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "ndvi_nan.tif" ).toStdString();
    params["index"] = "NDVI";
    params["red"] = 3;
    params["nir"] = 4;
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );
    const auto values = readBand( tmp.filePath( "ndvi_nan.tif" ), 1 );
    REQUIRE( values.size() == 4 );
    for ( float value : values )
        REQUIRE( std::isnan( value ) );
}
