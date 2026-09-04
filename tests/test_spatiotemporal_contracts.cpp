// tests/test_spatiotemporal_contracts.cpp — Platform 3.0 observation
// contracts: modality vocabulary, inference, STAC multimodal ingest, product
// metadata population, and the modality-scoped preflight gates (goal §5).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/algorithms/temporal/spatiotemporal_collection.h"
#include "processing/algorithms/temporal/spatiotemporal_contracts.h"
#include "processing/algorithms/temporal/temporal_preflight.h"
#include "processing/algorithms/temporal/temporal_stac_adapter.h"

using Catch::Approx;
using namespace sicnu::temporal;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spatiotemporal_contracts";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

/// Synthetic scene writer with explicit multimodal metadata knobs.
struct MultiSceneSpec
{
    QString path;
    QString acquisitionDate;
    QString modality;         // SICNU_MODALITY (optional)
    QString sensor;           // SICNU_SENSOR (optional)
    QString radiometricState; // SICNU_RADIOMETRIC_STATE (optional)
    QString polarizations;    // SICNU_POLARIZATIONS, comma separated (optional)
    QString platform;         // SICNU_SPACECRAFT (optional)
    QString demUnit;          // SICNU_DEM_UNIT (optional)
};

bool writeMultiScene( const MultiSceneSpec &spec )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, spec.path.toUtf8().constData(), 4, 4, 1, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double gt[6] = { 500000, 30, 0, 4500000, 0, -30 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
    }
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999.0 );
    if ( !spec.acquisitionDate.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", spec.acquisitionDate.toUtf8().constData(), nullptr );
    if ( !spec.modality.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_MODALITY", spec.modality.toUtf8().constData(), nullptr );
    if ( !spec.sensor.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_SENSOR", spec.sensor.toUtf8().constData(), nullptr );
    if ( !spec.radiometricState.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_RADIOMETRIC_STATE", spec.radiometricState.toUtf8().constData(), nullptr );
    if ( !spec.polarizations.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_POLARIZATIONS", spec.polarizations.toUtf8().constData(), nullptr );
    if ( !spec.platform.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_SPACECRAFT", spec.platform.toUtf8().constData(), nullptr );
    if ( !spec.demUnit.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_DEM_UNIT", spec.demUnit.toUtf8().constData(), nullptr );
    std::vector<float> values( 16, 1.0f );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 4, 4, values.data(), 4, 4, GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

Json::Value sarStacFeature( const char *id, const char *datetime, const char *pol0, const char *pol1 )
{
    Json::Value f( Json::objectValue );
    f["id"] = id;
    f["properties"]["datetime"] = datetime;
    f["properties"]["platform"] = "Sentinel-1A";
    f["properties"]["sar:polarizations"] = Json::Value( Json::arrayValue );
    f["properties"]["sar:polarizations"].append( pol0 );
    f["properties"]["sar:polarizations"].append( pol1 );
    f["properties"]["sar:instrument_mode"] = "IW";
    f["properties"]["eo:gsd"] = 10.0;
    Json::Value vv( Json::objectValue );
    vv["href"] = "file:///tmp/s1_vv.tif";
    vv["type"] = "image/tiff; application=geotiff; profile=cloud-optimized";
    Json::Value bands( Json::arrayValue );
    bands.append( "vv" );
    vv["eo:bands"] = bands;
    f["assets"]["vv"] = vv;
    return f;
}

bool hasIssue( const TemporalPreflightReport &report, const QString &code, bool blocking )
{
    for ( const auto &issue : report.issues )
        if ( issue.code == code && issue.blocking == blocking )
            return true;
    return false;
}

} // namespace

// ------------------------------------------------------------- vocabulary ----

TEST_CASE( "Modality vocabulary round-trips and tolerates unknowns",
           "[spatiotemporal][modality]" )
{
    ensureApp();
    REQUIRE( modalityToString( modalityFromString( "optical" ) ) == "optical" );
    REQUIRE( modalityToString( modalityFromString( "SAR" ) ) == "sar" );
    REQUIRE( modalityToString( modalityFromString( " Dem " ) ) == "dem" );
    REQUIRE( modalityToString( modalityFromString( "elevation" ) ) == "dem" );
    REQUIRE( modalityToString( modalityFromString( "auxiliary" ) ) == "auxiliary" );
    REQUIRE( modalityToString( modalityFromString( "model_derived" ) ) == "model_derived" );
    REQUIRE( modalityFromString( "multimodal" ) == Modality::Unknown );
    REQUIRE( modalityFromString( "" ) == Modality::Unknown );
    REQUIRE( modalityFromString( "hyper-spectral-hyperspace" ) == Modality::Unknown );
}

TEST_CASE( "Polarization normalization is canonical", "[spatiotemporal][polarization]" )
{
    ensureApp();
    REQUIRE( normalizePolarization( "vv" ) == "VV" );
    REQUIRE( normalizePolarization( " VH " ) == "VH" );
    REQUIRE( normalizePolarization( "co-pol" ) == "co-pol" );
    REQUIRE( normalizePolarization( "CROSSPOL" ) == "cross-pol" );
    const QStringList out = normalizePolarizations( QStringList{ "vv", "VV", "vh" } );
    REQUIRE( out == QStringList{ "VV", "VH" } );
}

TEST_CASE( "Modality inference from scene clues", "[spatiotemporal][modality]" )
{
    ensureApp();
    REQUIRE( inferModalityFromClues( "Sentinel-1A", "IW", "", {} ) == Modality::Sar );
    REQUIRE( inferModalityFromClues( "SENTINEL-2A", "MSI", "surface_reflectance", {} ) ==
             Modality::Optical );
    REQUIRE( inferModalityFromClues( "", "SRTM", "", { "elevation" } ) == Modality::Dem );
    REQUIRE( inferModalityFromClues( "", "", "gamma0", {} ) == Modality::Sar );
    REQUIRE( inferModalityFromClues( "", "", "", { "vv", "vh" } ) == Modality::Sar );
    REQUIRE( inferModalityFromClues( "UnknownSat", "", "", {} ) == Modality::Unknown );
    // Inference never claims optical on thin evidence.
    REQUIRE( inferModalityFromClues( "GF-3", "", "", {} ) == Modality::Sar );
}

// -------------------------------------------------------------- contracts ----

TEST_CASE( "Observation contracts derive identity, roles, and modality",
           "[spatiotemporal][contract]" )
{
    ensureApp();
    TemporalSceneRef scene;
    scene.path = "/data/s1_vv.tif";
    scene.assetId = "asset-123";
    scene.assetRevision = "4";
    scene.time = parseAcquisitionTime( "2025-03-01" );
    scene.platform = "Sentinel-1A";
    scene.bandOverrides["vv"] = 1;
    scene.polarizations = QStringList{ "vv", "VH" };
    scene.modality = "sar";

    const ObservationContract c = ObservationContract::fromSceneRef( scene, "coll-1", "7" );
    REQUIRE( c.observationId == "asset-123" );
    REQUIRE( c.collectionId == "coll-1" );
    REQUIRE( c.collectionRevision == "7" );
    REQUIRE( c.modality == Modality::Sar );
    REQUIRE( c.bandRoles.contains( "vv" ) );
    REQUIRE( c.polarizations == QStringList{ "VV", "VH" } );

    // Without an asset binding the id is a stable path digest, not empty.
    TemporalSceneRef unbound = scene;
    unbound.assetId.clear();
    const ObservationContract c2 = ObservationContract::fromSceneRef( unbound );
    REQUIRE( c2.observationId.size() == 16 );
    REQUIRE( c2.observationId == ObservationContract::fromSceneRef( unbound ).observationId );
}

TEST_CASE( "distinctModalities reports vocabulary order and unknown-only case",
           "[spatiotemporal][contract]" )
{
    ensureApp();
    QVector<ObservationContract> contracts( 2 );
    contracts[0].modality = Modality::Optical;
    contracts[1].modality = Modality::Sar;
    REQUIRE( distinctModalities( contracts ) == QStringList{ "optical", "sar" } );

    QVector<ObservationContract> unknowns( 1 );
    REQUIRE( distinctModalities( unknowns ) == QStringList{ "unknown" } );
}

// ------------------------------------------------------- STAC multimodal ----

TEST_CASE( "STAC ingest maps SAR polarizations, sensor, and modality",
           "[spatiotemporal][stac]" )
{
    ensureApp();
    Json::Value features( Json::arrayValue );
    features.append( sarStacFeature( "s1-2025-01", "2025-01-05T10:00:00Z", "VV", "VH" ) );
    features.append( sarStacFeature( "s1-2025-02", "2025-01-17T10:00:00Z", "vv", "vh" ) );
    Json::Value doc( Json::objectValue );
    doc["features"] = features;

    TemporalCollection collection;
    QString error;
    REQUIRE( temporalCollectionFromStacSearch( doc, "s1-stack", &collection, &error ) );
    REQUIRE( collection.sceneCount() == 2 );

    const auto contracts = contractsOf( collection );
    REQUIRE( contracts.size() == 2 );
    for ( const auto &c : contracts )
    {
        REQUIRE( c.modality == Modality::Sar );
        REQUIRE( c.polarizations == QStringList{ "VV", "VH" } );
        REQUIRE( c.sensor == "IW" );
        REQUIRE( c.resolutionMeters == Approx( 10.0 ) );
    }
    REQUIRE( distinctModalities( contracts ) == QStringList{ "sar" } );

    // Scene refs carry them into the descriptor too (serialized when claimed).
    const Json::Value descriptor = collection.toJson();
    const Json::Value scenes = descriptor["scenes"];
    REQUIRE( scenes.isArray() );
    const Json::Value first = scenes[0];
    REQUIRE( first.isMember( "modality" ) );
    REQUIRE( first["modality"].asString() == "sar" );
    REQUIRE( first.isMember( "polarizations" ) );
}

TEST_CASE( "STAC ingest recognizes DEM products by asset/properties tokens",
           "[spatiotemporal][stac]" )
{
    ensureApp();
    Json::Value f( Json::objectValue );
    f["id"] = "cop-dem-1";
    f["properties"]["datetime"] = "2021-01-01T00:00:00Z";
    f["properties"]["platform"] = "Unknown";
    Json::Value dem( Json::objectValue );
    dem["href"] = "file:///tmp/dem.tif";
    dem["type"] = "image/tiff; application=geotiff; profile=cloud-optimized";
    dem["roles"] = Json::Value( Json::arrayValue );
    dem["roles"].append( "data" );
    dem["title"] = "Copernicus DEM GLO-30";
    f["assets"]["dem"] = dem;

    StacItem item;
    QString error;
    REQUIRE( parseStacItem( f, &item, &error ) );
    REQUIRE( item.modality == "dem" );

    // data_type=dem property also maps.
    Json::Value f2 = f;
    f2["assets"]["dem"]["title"] = "terrain";
    f2["properties"]["data_type"] = "DEM";
    StacItem item2;
    REQUIRE( parseStacItem( f2, &item2, &error ) );
    REQUIRE( item2.modality == "dem" );
}

TEST_CASE( "STAC optical ingest stays optical and unchanged",
           "[spatiotemporal][stac]" )
{
    ensureApp();
    Json::Value f( Json::objectValue );
    f["id"] = "s2-1";
    f["properties"]["datetime"] = "2025-02-01T10:00:00Z";
    f["properties"]["platform"] = "Sentinel-2A";
    f["properties"]["eo:cloud_cover"] = 12.0;
    Json::Value asset( Json::objectValue );
    asset["href"] = "file:///tmp/s2.tif";
    asset["type"] = "image/tiff; application=geotiff; profile=cloud-optimized";
    Json::Value bands( Json::arrayValue );
    bands.append( "red" );
    bands.append( "nir" );
    asset["eo:bands"] = bands;
    f["assets"]["r10m"] = asset;

    StacItem item;
    QString error;
    REQUIRE( parseStacItem( f, &item, &error ) );
    REQUIRE( item.modality == "optical" );
    REQUIRE( item.polarizations.isEmpty() );
    REQUIRE( item.cloudCover == Approx( 12.0 ) );
}

// --------------------------------------------- product metadata populate ----

TEST_CASE( "inspectScene populates multimodal fields from product metadata",
           "[spatiotemporal][ingest]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // SAR scene with explicit product metadata.
    const QString sarPath = dir.filePath( "sar_scene.tif" );
    MultiSceneSpec sar;
    sar.path = sarPath;
    sar.acquisitionDate = "2025-03-01";
    sar.modality = "sar";
    sar.sensor = "C-SAR";
    sar.polarizations = "VV,VH";
    sar.platform = "SENTINEL-1A";
    sar.radiometricState = "sigma0";
    REQUIRE( writeMultiScene( sar ) );

    TemporalSceneRef scene;
    QString error;
    REQUIRE( inspectScene( sarPath, QString(), &scene, &error ) );
    REQUIRE( scene.modality == "sar" );
    REQUIRE( scene.sensor == "C-SAR" );
    REQUIRE( scene.polarizations == QStringList{ "VV", "VH" } );
    REQUIRE( scene.radiometricState == "sigma0" );
    REQUIRE( scene.time.valid );

    // DEM scene without declared modality: inferred from clues.
    const QString demPath = dir.filePath( "dem_scene.tif" );
    MultiSceneSpec dem;
    dem.path = demPath;
    dem.acquisitionDate = "2021-01-01";
    dem.modality = "dem";
    dem.demUnit = "meters";
    REQUIRE( writeMultiScene( dem ) );
    TemporalSceneRef demScene;
    REQUIRE( inspectScene( demPath, QString(), &demScene, &error ) );
    REQUIRE( demScene.modality == "dem" );

    // Optical scene without any multimodal metadata stays unclaimed — legacy
    // scenes must not silently gain a modality claim.
    const QString optPath = dir.filePath( "optical_scene.tif" );
    MultiSceneSpec opt;
    opt.path = optPath;
    opt.acquisitionDate = "2025-04-01";
    REQUIRE( writeMultiScene( opt ) );
    TemporalSceneRef optScene;
    REQUIRE( inspectScene( optPath, QString(), &optScene, &error ) );
    REQUIRE( optScene.modality.isEmpty() );
    REQUIRE( ObservationContract::fromSceneRef( optScene ).modality == Modality::Unknown );
}

TEST_CASE( "Explicit inline modality wins over product metadata",
           "[spatiotemporal][ingest]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( "ambiguous.tif" );
    MultiSceneSpec spec;
    spec.path = path;
    spec.acquisitionDate = "2025-03-01";
    spec.modality = "sar"; // product metadata claims sar…
    REQUIRE( writeMultiScene( spec ) );

    // …but the caller's inline claim takes precedence.
    const Json::Value entry = [ & ] {
        Json::Value v( Json::objectValue );
        v["path"] = path.toStdString();
        v["modality"] = "optical";
        return v;
    }();
    TemporalSceneRef out;
    QString error;
    REQUIRE( TemporalSceneRef::parseInline( entry, 0, &out, &error ) );
    REQUIRE( out.modality == "optical" );
}

// -------------------------------------------------- preflight modality ------

TEST_CASE( "Preflight blocks mixed-modality folds and reports the profile",
           "[spatiotemporal][preflight]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString sar1 = dir.filePath( "s1.tif" );
    const QString opt1 = dir.filePath( "o1.tif" );
    MultiSceneSpec a;
    a.path = sar1;
    a.acquisitionDate = "2025-03-01";
    a.modality = "sar";
    a.polarizations = "VV";
    REQUIRE( writeMultiScene( a ) );
    MultiSceneSpec b;
    b.path = opt1;
    b.acquisitionDate = "2025-03-05";
    b.modality = "optical";
    REQUIRE( writeMultiScene( b ) );

    TemporalCollection collection;
    collection = TemporalCollection::fromScenePaths( { sar1, opt1 }, {}, "mixed" );

    PreflightOptions options;
    const TemporalPreflightReport report = runPreflight( collection, options );
    REQUIRE( hasIssue( report, "temporal.modality_mismatch", true ) );
    REQUIRE( report.modality.mixed );
    REQUIRE( report.modality.modalities.contains( "sar" ) );
    REQUIRE( report.modality.modalities.contains( "optical" ) );

    // Multimodal consumers (feature stacking) explicitly opt out.
    PreflightOptions allowMixed;
    allowMixed.requireUniformModality = false;
    const TemporalPreflightReport report2 = runPreflight( collection, allowMixed );
    REQUIRE_FALSE( hasIssue( report2, "temporal.modality_mismatch", true ) );
}

TEST_CASE( "Preflight enforces SAR polarization uniformity",
           "[spatiotemporal][preflight]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString vv = dir.filePath( "vv.tif" );
    const QString vh = dir.filePath( "vh.tif" );
    MultiSceneSpec a;
    a.path = vv;
    a.acquisitionDate = "2025-03-01";
    a.modality = "sar";
    a.polarizations = "VV";
    REQUIRE( writeMultiScene( a ) );
    MultiSceneSpec b;
    b.path = vh;
    b.acquisitionDate = "2025-03-05";
    b.modality = "sar";
    b.polarizations = "VH";
    REQUIRE( writeMultiScene( b ) );

    const TemporalCollection collection = TemporalCollection::fromScenePaths( { vv, vh }, {}, "s1" );
    PreflightOptions options;
    const TemporalPreflightReport report = runPreflight( collection, options );
    REQUIRE( hasIssue( report, "temporal.polarization_mismatch", true ) );
    REQUIRE_FALSE( report.modality.polarizationUniform );

    // Explicit polarization-aware consumers may allow the mix.
    PreflightOptions allow;
    allow.allowMixedPolarization = true;
    const TemporalPreflightReport report2 = runPreflight( collection, allow );
    REQUIRE_FALSE( hasIssue( report2, "temporal.polarization_mismatch", true ) );
}

TEST_CASE( "Preflight warns on undeclared DEM units",
           "[spatiotemporal][preflight]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString dem1 = dir.filePath( "dem1.tif" );
    const QString dem2 = dir.filePath( "dem2.tif" );
    MultiSceneSpec a;
    a.path = dem1;
    a.acquisitionDate = "2021-01-01";
    a.modality = "dem";
    a.demUnit = "meters";
    REQUIRE( writeMultiScene( a ) );
    MultiSceneSpec b;
    b.path = dem2;
    b.acquisitionDate = "2021-06-01";
    b.modality = "dem"; // no SICNU_DEM_UNIT
    REQUIRE( writeMultiScene( b ) );

    const TemporalCollection collection = TemporalCollection::fromScenePaths( { dem1, dem2 }, {}, "dems" );
    PreflightOptions options;
    const TemporalPreflightReport report = runPreflight( collection, options );
    REQUIRE( hasIssue( report, "temporal.dem_unit_undeclared", false ) );
    REQUIRE( report.modality.demSceneCount == 2 );
}

TEST_CASE( "Preflight keeps single-modality SAR series clean",
           "[spatiotemporal][preflight]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString s1 = dir.filePath( "a.tif" );
    const QString s2 = dir.filePath( "b.tif" );
    MultiSceneSpec a;
    a.path = s1;
    a.acquisitionDate = "2025-03-01";
    a.modality = "sar";
    a.polarizations = "VV,VH";
    REQUIRE( writeMultiScene( a ) );
    MultiSceneSpec b;
    b.path = s2;
    b.acquisitionDate = "2025-03-13";
    b.modality = "sar";
    b.polarizations = "VV,VH";
    REQUIRE( writeMultiScene( b ) );

    const TemporalCollection collection = TemporalCollection::fromScenePaths( { s1, s2 }, {}, "s1" );
    PreflightOptions options;
    const TemporalPreflightReport report = runPreflight( collection, options );
    REQUIRE_FALSE( hasIssue( report, "temporal.modality_mismatch", true ) );
    REQUIRE_FALSE( hasIssue( report, "temporal.polarization_mismatch", true ) );
    REQUIRE_FALSE( hasIssue( report, "temporal.polarization_partial", false ) );
    REQUIRE( report.modality.commonPolarizationSet == "VV,VH" );
    REQUIRE( report.modality.sarSceneCount == 2 );
}
