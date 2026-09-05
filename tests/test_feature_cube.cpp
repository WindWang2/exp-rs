// tests/test_feature_cube.cpp — Platform 3.0 feature cube contract tests
// (goal §8): JSON round-trip, GDAL metadata persistence, sidecar spill, and
// model-input matching verdicts.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <limits>

#include "processing/features/feature_cube.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::features;

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_feature_cube";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

bool writeRaster( const QString &path, int bands, int width, int height )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, bands,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double gt[6] = { 500000, 10, 0, 4500000, 0, -10 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
    }
    for ( int b = 1; b <= bands; ++b )
    {
        std::vector<float> values( static_cast<size_t>( width ) * height,
                                   static_cast<float>( b ) );
        if ( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                           values.data(), width, height, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( ds );
            return false;
        }
    }
    GDALClose( ds );
    return true;
}

FeatureCubeContract sampleContract( int bandCount )
{
    FeatureCubeContract c;
    c.featureId = QStringLiteral( "s2_demo_features" );
    c.generator = QStringLiteral( "rs:feature_stack" );
    const char *roles[] = { "red", "nir", "vv" };
    const char *modalities[] = { "optical", "optical", "sar" };
    for ( int b = 0; b < bandCount; ++b )
    {
        FeatureBand band;
        band.id = QString::fromUtf8( roles[b] );
        band.semanticRole = QString::fromUtf8( roles[b] );
        band.unit = b == 2 ? QStringLiteral( "db" ) : QStringLiteral( "reflectance" );
        band.band = b + 1;
        band.modality = QString::fromUtf8( modalities[b] );
        band.time.kind = b == 0 ? QStringLiteral( "single" ) : QStringLiteral( "none" );
        band.time.startIso = b == 0 ? QStringLiteral( "2025-06-01T10:00:00Z" ) : QString();
        c.bands.push_back( band );
    }
    return c;
}
} // namespace

TEST_CASE( "Feature cube contract JSON round-trips", "[features][contract]" )
{
    ensureApp();
    const FeatureCubeContract original = sampleContract( 3 );
    const Json::Value json = original.toJson();

    FeatureCubeContract parsed;
    QString error;
    REQUIRE( FeatureCubeContract::fromJson( json, &parsed, &error ) );
    REQUIRE( error.isEmpty() );
    REQUIRE( parsed.featureId == original.featureId );
    REQUIRE( parsed.generator == original.generator );
    REQUIRE( parsed.bands.size() == 3 );
    REQUIRE( parsed.bands[2].semanticRole == "vv" );
    REQUIRE( parsed.bands[2].unit == "db" );
    REQUIRE( parsed.bands[2].modality == "sar" );
    REQUIRE( parsed.bands[0].time.kind == "single" );
    REQUIRE( parsed.bands[0].time.startIso == "2025-06-01T10:00:00Z" );
}

TEST_CASE( "Feature cube contract rejects duplicate ids and missing bands",
           "[features][contract]" )
{
    ensureApp();
    FeatureCubeContract bad = sampleContract( 3 );
    bad.bands[1].id = bad.bands[0].id;
    FeatureCubeContract parsed;
    QString error;
    REQUIRE_FALSE( FeatureCubeContract::fromJson( bad.toJson(), &parsed, &error ) );
    REQUIRE( error.contains( "duplicate" ) );

    FeatureCubeContract empty;
    REQUIRE_FALSE( FeatureCubeContract::fromJson( empty.toJson(), &parsed, &error ) );

    // Wrong version is refused.
    Json::Value wrongVersion = sampleContract( 1 ).toJson();
    wrongVersion["version"] = 99;
    REQUIRE_FALSE( FeatureCubeContract::fromJson( wrongVersion, &parsed, &error ) );
}

TEST_CASE( "Feature cube metadata persists through a GDAL dataset",
           "[features][persistence]" )
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( "cube.tif" );
    REQUIRE( writeRaster( path, 3, 4, 4 ) );

    const FeatureCubeContract contract = sampleContract( 3 );
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( path ) );
        // Opened read-only: metadata writes need an update-mode handle.
    }
    REQUIRE_FALSE( isFeatureCube( path ) ); // not yet tagged

    // Write via a fresh update-mode dataset handle.
    {
        GDALDatasetH h = GDALOpen( path.toUtf8().constData(), GA_Update );
        REQUIRE( h != nullptr );
        REQUIRE( writeFeatureCubeMetadata( h, contract ) );
        GDALClose( h );
    }
    REQUIRE( isFeatureCube( path ) );

    FeatureCubeContract read;
    QString error;
    REQUIRE( readFeatureCubeMetadata( path, &read, &error ) );
    REQUIRE( read.bands.size() == contract.bands.size() );
    REQUIRE( read.bands[1].semanticRole == "nir" );

    // Per-band introspection items ride for quick reads.
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    GDALDatasetH h = static_cast<GDALDatasetH>( ds.dataset() );
    const char *bandId = GDALGetMetadataItem( GDALGetRasterBand( h, 2 ),
                                              "SICNU_FEATURE_ID", nullptr );
    REQUIRE( bandId != nullptr );
    REQUIRE( std::string( bandId ) == "nir" );
}

TEST_CASE( "Feature cube matching reports missing roles and modality problems",
           "[features][matching]" )
{
    ensureApp();
    const FeatureCubeContract cube = sampleContract( 3 );

    // Everything present → ok.
    const ModelInputMatch ok = matchesModelInput( cube, { "red", "nir" }, 0, QString() );
    REQUIRE( ok.ok );

    // Missing role + band count delta.
    const ModelInputMatch missing = matchesModelInput( cube, { "red", "swir1" }, 4, QString() );
    REQUIRE_FALSE( missing.ok );
    REQUIRE( missing.missingRoles == 1 );
    REQUIRE( missing.bandCountDelta == -1 );
    REQUIRE( missing.problems.size() == 2 );

    // Modality coverage: the cube carries optical+sar, not "dem".
    const ModelInputMatch modality = matchesModelInput( cube, { "red" }, 0, "dem" );
    REQUIRE_FALSE( modality.ok );
}

TEST_CASE( "Oversized contracts spill to the sidecar", "[features][persistence]" )
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( "big_cube.tif" );
    REQUIRE( writeRaster( path, 2, 2, 2 ) );

    // A contract with huge per-band descriptions overflows the dataset item.
    FeatureCubeContract big;
    big.featureId = "big";
    for ( int b = 0; b < 2; ++b )
    {
        FeatureBand band;
        band.id = QStringLiteral( "feature_%1" ).arg( b );
        band.band = b + 1;
        // Pad a long source path so the JSON exceeds the item cap.
        band.sourcePath = QString( 70000, QLatin1Char( 'x' ) );
        big.bands.push_back( band );
    }
    const QString sidecar = tmp.filePath( "big_cube.features.json" );
    GDALDatasetH h = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( h != nullptr );
    REQUIRE( writeFeatureCubeMetadata( h, big, sidecar ) );
    GDALClose( h );

    REQUIRE( isFeatureCube( path ) );
    FeatureCubeContract read;
    QString error;
    REQUIRE( readFeatureCubeMetadata( path, &read, &error ) );
    REQUIRE( read.bands.size() == 2 );
    REQUIRE( read.bands[0].id == "feature_0" );
}
