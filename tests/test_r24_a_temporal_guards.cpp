// test_r24_a_temporal_guards.cpp — R24-A science track (WP-A2).
//
// RED provenance (origin/master@5fe642ec6): rs:temporal_phenology_multi
// builds its day axis straight from reader.sceneDayOffset() with no
// strictly-increasing validation, while its sibling operators
// rs:temporal_decompose and rs:temporal_smooth both reject a duplicated
// acquisition instant with a typed error ("... share an instant — use
// duplicate_policy=reject or run rs:temporal_regularize first"). With the
// default duplicate_policy=keep_all, a collection with two scenes on the
// same day reaches the kernel, validTimeAxis degenerates, the climatology
// comes back all-NaN and every pixel is refused with the misleading
// reason "degenerate_seasonal_component" instead of the real cause.
//
// Expected: the same typed contract as the siblings —
// ErrorCode::InvalidInputData with "strictly increasing" guidance.

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <array>
#include <memory>
#include <vector>

using namespace sicnu::operators;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r24_a_temporal_guards";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

/// Minimal single-band scene stamped with an acquisition date (same recipe
/// as the temporal operator suites).
bool writeScene( const QString &path, float value, const QString &date )
{
    ensureGdalInit();
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
        return false;
    char *wktOut = nullptr;
    srs.exportToWkt( &wktOut );
    const QString wkt = QString::fromUtf8( wktOut );
    CPLFree( wktOut );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 1, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", date.toUtf8().constData(),
                         nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999.0 );
    const std::vector<float> values( 2, value );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 1,
                                  const_cast<float *>( values.data() ), 2, 1,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

} // namespace

TEST_CASE( "rs:temporal_phenology_multi rejects duplicate acquisition instants",
           "[operators][temporal][phenology_multi][r24_a]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Nine scenes (the operator's own minimum), two of them on the same
    // day — keep_all (default) keeps both, the axis-degenerate case the
    // sibling operators already refuse typed.
    const char *dates[] = { "2024-03-01", "2024-03-17", "2024-04-02", "2024-04-18",
                            "2024-05-04", "2024-05-20", "2024-05-20", "2024-06-05",
                            "2024-06-21" };
    Json::Value scenes( Json::arrayValue );
    for ( int i = 0; i < 9; ++i )
    {
        const QString name = QStringLiteral( "s%1.tif" ).arg( i );
        REQUIRE( writeScene( dir.filePath( name ),
                             0.25f + 0.05f * static_cast<float>( i ),
                             QString::fromLatin1( dates[i] ) ) );
        scenes.append( dir.filePath( name ).toStdString() );
    }

    auto op = RSOperatorRegistry::instance().create( "rs:temporal_phenology_multi" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["scenes"] = scenes;
    params["band"] = 1;
    params["output"] = dir.filePath( "out.tif" ).toStdString();

    RSOperatorContext context;
    bool threw = false;
    std::string message;
    try
    {
        op->run( params, context );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        message = e.what();
    }
    REQUIRE( threw );
    INFO( "operator error: " << message );
    // Same guidance contract as rs:temporal_decompose / rs:temporal_smooth.
    CHECK( message.find( "strictly increasing" ) != std::string::npos );
    CHECK( message.find( "duplicate_policy" ) != std::string::npos );
    // The run must fail before creating the output raster.
    CHECK( !QFile::exists( dir.filePath( "out.tif" ) ) );
}
