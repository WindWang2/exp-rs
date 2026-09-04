// tests/test_temporal_workspace.cpp — TemporalCollection as a first-class
// workspace asset: DataManager records, asset binding + revision-sensitive
// fingerprints, STAC ingestion, and the execution-cache clear() regression
// (#720).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDomDocument>
#include <QFile>
#include <QMap>
#include <QObject>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>

#include "data/data_manager.h"
#include "data/execution_fingerprint.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_stac_adapter.h"
#include "processing/algorithms/temporal/temporal_workspace.h"

using namespace sicnu::temporal;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_workspace";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

/// A 2x2 Float32 scene with an acquisition date + a red band role, on a
/// static grid so temporal preflight sees a same-grid collection.
bool writeScene( const QString &path, const QString &date, float value )
{
    GDALAllRegister();
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
        return false;
    char *wktOut = nullptr;
    srs.exportToWkt( &wktOut );
    const QString wkt = QString::fromUtf8( wktOut );
    CPLFree( wktOut );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 1, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    double gt[6] = {500000, 30, 0, 4500000, 0, -30};
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection( ds, wkt.toUtf8().constData() );
    GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE", date.toUtf8().constData(), nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999 );
    GDALSetMetadataItem( band, "SICNU_BAND_ROLE", "red", nullptr );
    const float values[4] = {value, value, value, value};
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 2, 2, const_cast<float *>( values ), 2, 2,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

/// Three same-grid scenes in a fresh temp dir; returns their paths.
QStringList makeScenes( QTemporaryDir &dir )
{
    QStringList paths;
    for ( int i = 0; i < 3; ++i )
    {
        const QString path = dir.filePath( QStringLiteral( "scene_%1.tif" ).arg( i ) );
        REQUIRE( writeScene( path, QStringLiteral( "2024-0%1-15T10:00:00" ).arg( i + 1 ),
                             10.0f * ( i + 1 ) ) );
        paths.append( path );
    }
    return paths;
}

/// Registers every scene as a DataManager asset; returns asset ids in order.
QList<sicnu::data::AssetId> registerScenes( sicnu::data::DataManager &dm, const QStringList &paths )
{
    QList<sicnu::data::AssetId> ids;
    for ( const QString &path : paths )
    {
        sicnu::data::SourceDescriptor source;
        source.providerKey = QStringLiteral( "gdal" );
        source.canonicalSource = path;
        const auto registered = dm.registerSource( sicnu::data::RegisterRequest{source} );
        REQUIRE_FALSE( registered.assetId.isNull() );
        ids.append( registered.assetId );
    }
    return ids;
}

Json::Value stacSearchDoc()
{
    // Five synthetic items: Sentinel-2-style assets, cloud cover, platform,
    // one item WITHOUT a datetime (must be rejected) and one with no raster
    // asset (must be rejected). Ordered newest-first on purpose — the
    // adapter must produce chronological order.
    Json::Value doc( Json::objectValue );
    Json::Value features( Json::arrayValue );

    auto makeItem = []( const char *id, const char *datetime, const char *platform,
                        double cloud, const char *href ) {
        Json::Value item( Json::objectValue );
        item["id"] = id;
        item["type"] = "Feature";
        item["properties"]["datetime"] = datetime;
        item["properties"]["platform"] = platform;
        item["properties"]["eo:cloud_cover"] = cloud;
        Json::Value assets( Json::objectValue );
        assets["thumbnail"]["href"] = std::string( href ) + ".jpg";
        assets["thumbnail"]["type"] = "image/jpeg";
        assets["green"]["href"] = std::string( href ) + "_B03.tif";
        assets["green"]["type"] = "image/tiff; application=geotiff";
        Json::Value bands( Json::arrayValue );
        Json::Value band( Json::objectValue );
        band["name"] = "green";
        band["common_name"] = "green";
        bands.append( band );
        assets["green"]["eo:bands"] = bands;
        item["assets"] = assets;
        Json::Value geometry( Json::objectValue );
        geometry["type"] = "Polygon";
        Json::Value ring( Json::arrayValue );
        Json::Value c1( Json::arrayValue );
        c1.append( 100.0 );
        c1.append( 30.0 );
        Json::Value c2( Json::arrayValue );
        c2.append( 101.0 );
        c2.append( 30.0 );
        Json::Value c3( Json::arrayValue );
        c3.append( 101.0 );
        c3.append( 31.0 );
        Json::Value c4( Json::arrayValue );
        c4.append( 100.0 );
        c4.append( 31.0 );
        ring.append( c1 );
        ring.append( c2 );
        ring.append( c3 );
        ring.append( c4 );
        ring.append( c1 );
        Json::Value coordinates( Json::arrayValue );
        coordinates.append( ring );
        geometry["coordinates"] = coordinates;
        item["geometry"] = geometry;
        return item;
    };

    features.append( makeItem( "item-03", "2024-03-15T10:20:30Z", "Sentinel-2A", 12.5,
                               "https://example.com/s2/item-03" ) );
    features.append( makeItem( "item-02", "2024-02-15T10:20:30Z", "Sentinel-2A", 45.0,
                               "https://example.com/s2/item-02" ) );
    features.append( makeItem( "item-01", "2024-01-15T10:20:30Z", "Sentinel-2B", 3.1,
                               "https://example.com/s2/item-01" ) );

    // item-04: no datetime — must be rejected by the parser.
    Json::Value noDate = makeItem( "item-04", "", "Sentinel-2A", 1.0,
                                   "https://example.com/s2/item-04" );
    features.append( noDate );

    // item-05: no raster asset — must be rejected by the parser.
    Json::Value noAsset( Json::objectValue );
    noAsset["id"] = "item-05";
    noAsset["type"] = "Feature";
    noAsset["properties"]["datetime"] = "2024-04-15T10:20:30Z";
    noAsset["assets"]["metadata"]["href"] = "https://example.com/s2/item-05.xml";
    noAsset["assets"]["metadata"]["type"] = "application/xml";
    features.append( noAsset );

    // Assign AFTER every append: jsoncpp assignment is a deep copy, so an
    // earlier doc["features"] = features would snapshot only the first three
    // items and the two rejected ones would silently vanish from the doc
    // (making the parse-test's features[3]/[4] out-of-range nulls).
    doc["features"] = features;

    return doc;
}

} // namespace

// ---------------------------------------------------------------------------
// DataManager temporal records
// ---------------------------------------------------------------------------

TEST_CASE( "Temporal collection records: create/get/list/update/remove", "[temporal][workspace]" )
{
    ensureApp();
    sicnu::data::DataManager dm;

    REQUIRE( dm.temporalCollections().isEmpty() );

    sicnu::data::TemporalCollectionCreateRequest request;
    request.displayName = QStringLiteral( "Landsat time series" );
    request.descriptor = QStringLiteral( R"({"version":1,"name":"Landsat time series","scenes":[{"path":"a.tif","time":"2024-01-01"}]})" );
    const auto created = dm.createTemporalCollection( request );
    REQUIRE_FALSE( created.collectionId.isNull() );
    REQUIRE_FALSE( created.reusedExisting );

    const auto fetched = dm.temporalCollection( created.collectionId );
    REQUIRE( fetched.has_value() );
    CHECK( fetched->displayName == request.displayName );
    CHECK( fetched->descriptor == request.descriptor );
    CHECK( fetched->revision == 1 );

    // Identical re-registration dedups to the same record.
    const auto again = dm.createTemporalCollection( request );
    REQUIRE( again.collectionId == created.collectionId );
    CHECK( again.reusedExisting );
    CHECK( dm.temporalCollections().size() == 1 );

    // Update bumps the revision.
    sicnu::data::TemporalCollectionCreateRequest updated = request;
    updated.descriptor += QStringLiteral( " " );
    const auto updateResult = dm.updateTemporalCollection( created.collectionId, updated );
    REQUIRE( updateResult );
    CHECK( updateResult.value().revision == 2 );
    CHECK( dm.temporalCollection( created.collectionId )->descriptor == updated.descriptor );

    // Update of an unknown record fails; empty descriptor fails.
    CHECK_FALSE( dm.updateTemporalCollection( sicnu::data::CollectionId::generate(),
                                              request ) );
    sicnu::data::TemporalCollectionCreateRequest empty;
    empty.displayName = QStringLiteral( "empty" );
    CHECK( dm.createTemporalCollection( empty ).collectionId.isNull() );

    // Remove.
    REQUIRE( dm.removeTemporalCollection( created.collectionId ) );
    CHECK_FALSE( dm.temporalCollection( created.collectionId ).has_value() );
    CHECK( dm.temporalCollections().isEmpty() );
    CHECK_FALSE( dm.removeTemporalCollection( created.collectionId ) );
}

TEST_CASE( "Restore preserves temporal record identity and revision", "[temporal][workspace]" )
{
    ensureApp();
    sicnu::data::DataManager dm;
    const auto id = sicnu::data::CollectionId::generate();
    sicnu::data::TemporalCollectionCreateRequest request;
    request.displayName = QStringLiteral( "restored" );
    request.descriptor = QStringLiteral( "{\"version\":1,\"scenes\":[]}" );
    const auto restored = dm.restoreTemporalCollection( id, 7, request );
    REQUIRE_FALSE( restored.collectionId.isNull() );
    const auto fetched = dm.temporalCollection( id );
    REQUIRE( fetched.has_value() );
    CHECK( fetched->revision == 7 );
    // Duplicate id restore is refused.
    CHECK( dm.restoreTemporalCollection( id, 8, request ).collectionId.isNull() );
}

// ---------------------------------------------------------------------------
// Binding + revision-sensitive fingerprints
// ---------------------------------------------------------------------------

TEST_CASE( "Scene asset binding stores assetId/revision; revision bump changes the fingerprint",
           "[temporal][workspace][fingerprint]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QStringList paths = makeScenes( dir );
    sicnu::data::DataManager dm;
    const QList<sicnu::data::AssetId> assetIds = registerScenes( dm, paths );

    TemporalCollection collection = TemporalCollection::fromScenePaths( paths, {}, QStringLiteral( "bound" ) );
    REQUIRE( collection.sceneCount() == 3 );
    // Before binding: path-only identity, fingerprint inputs unavailable.
    QString reason;
    QVector<sicnu::data::TaggedDerivationInput> inputs;
    CHECK( bindCollectionAssets( collection, &dm ) == 3 );
    for ( const auto &scene : collection.scenes() )
    {
        CHECK_FALSE( scene.assetId.isEmpty() );
        CHECK_FALSE( scene.assetRevision.isEmpty() );
    }

    // Persist into the workspace, then compute the fingerprint inputs.
    const auto recordId = saveCollectionToWorkspace( dm, QStringLiteral( "bound" ), collection );
    REQUIRE_FALSE( recordId.isNull() );
    inputs.clear();
    REQUIRE( collectionFingerprintInputs( dm, recordId, collection, &inputs ) );
    // 1 collection-level input + 3 scene inputs.
    CHECK( inputs.size() == 4 );
    CHECK( inputs.front().toPort == QStringLiteral( "collection" ) );

    const sicnu::data::ExecutionFingerprint fp1 = sicnu::data::makeExecutionFingerprintV2(
        QStringLiteral( "rs:temporal_summary" ), QStringLiteral( "v1" ), QJsonObject(), inputs );

    // Scene revision bump (re-register with notifyUpdateOnReuse, the
    // OutputCommitter re-commit idiom): the CURRENT revision resolved live
    // changes, so the fingerprint changes.
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = paths.first();
    sicnu::data::RegisterRequest bump;
    bump.source = source;
    bump.notifyUpdateOnReuse = true;
    const auto bumped = dm.registerSource( bump );
    REQUIRE( bumped.assetId == assetIds.first() );
    REQUIRE( bumped.reusedExisting );
    const auto afterBump = dm.asset( assetIds.first() );
    REQUIRE( afterBump->revision().value() == 2 );

    inputs.clear();
    REQUIRE( collectionFingerprintInputs( dm, recordId, collection, &inputs ) );
    const sicnu::data::ExecutionFingerprint fp2 = sicnu::data::makeExecutionFingerprintV2(
        QStringLiteral( "rs:temporal_summary" ), QStringLiteral( "v1" ), QJsonObject(), inputs );
    CHECK( fp1.isValid() );
    CHECK( fp2.isValid() );
    CHECK( fp1 != fp2 ); // scene revision change invalidates the cached step
}

TEST_CASE( "A path-only (unbound) scene makes the collection uncacheable", "[temporal][workspace][fingerprint]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QStringList paths = makeScenes( dir );
    sicnu::data::DataManager dm;
    registerScenes( dm, paths );

    TemporalCollection collection = TemporalCollection::fromScenePaths( paths, {}, QStringLiteral( "half" ) );
    bindCollectionAssets( collection, &dm );
    // Make one scene genuinely identity-less: point it at a file that is NOT
    // registered in the catalog (the live-resolution contract keys on the
    // path, so clearing the stored assetId alone would still resolve — the
    // CURRENT revision is always re-read, never the stale stored snapshot).
    collection.scenes()[1].path = dir.filePath( QStringLiteral( "unregistered_scene.tif" ) );
    collection.scenes()[1].assetId.clear();
    collection.scenes()[1].assetRevision.clear();
    const auto recordId = saveCollectionToWorkspace( dm, QStringLiteral( "half" ), collection );
    REQUIRE_FALSE( recordId.isNull() );

    QVector<sicnu::data::TaggedDerivationInput> inputs;
    CHECK_FALSE( collectionFingerprintInputs( dm, recordId, collection, &inputs ) );

    // And the operator-params policy refuses to fingerprint such a run.
    QVariantMap params;
    params.insert( QStringLiteral( "collection" ), recordId.toString() );
    QString reason;
    CHECK_FALSE( fingerprintInputsForOperatorParams( &dm, params, &inputs, &reason ) );
    CHECK_FALSE( reason.isEmpty() );
}

TEST_CASE( "fingerprintInputsForCollectionParam accepts record ids and matching descriptors",
           "[temporal][workspace][fingerprint]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QStringList paths = makeScenes( dir );
    sicnu::data::DataManager dm;
    registerScenes( dm, paths );

    TemporalCollection collection = TemporalCollection::fromScenePaths( paths, {}, QStringLiteral( "fp" ) );
    bindCollectionAssets( collection, &dm );
    const auto recordId = saveCollectionToWorkspace( dm, QStringLiteral( "fp" ), collection );
    REQUIRE_FALSE( recordId.isNull() );

    QVector<sicnu::data::TaggedDerivationInput> inputs;
    QString reason;
    REQUIRE( fingerprintInputsForCollectionParam( &dm, recordId.toString(), &inputs, &reason ) );
    CHECK( inputs.size() == 4 ); // collection + 3 bound scenes

    // A descriptor FILE whose content matches the registered record is
    // addressable too.
    const QString descriptorPath = dir.filePath( QStringLiteral( "fp.json" ) );
    REQUIRE( collection.save( descriptorPath ) );
    // Re-bind so the stored document is identical to the record's.
    TemporalCollection saved = collection;
    bindCollectionAssets( saved, &dm );
    const auto sameRecord = saveCollectionToWorkspace( dm, QStringLiteral( "fp2" ), saved );
    REQUIRE_FALSE( sameRecord.isNull() );
    // The first record's descriptor matches the file exactly.
    TemporalCollection fromFile;
    REQUIRE( TemporalCollection::load( descriptorPath, &fromFile, nullptr ) );
    (void)fromFile;

    inputs.clear();
    REQUIRE( fingerprintInputsForCollectionParam( &dm, descriptorPath, &inputs, &reason ) );

    // An unknown id fails with a reason (conservative uncacheable).
    inputs.clear();
    CHECK_FALSE( fingerprintInputsForCollectionParam( &dm, sicnu::data::CollectionId::generate().toString(),
                                                      &inputs, &reason ) );
    CHECK_FALSE( reason.isEmpty() );
}

// ---------------------------------------------------------------------------
// Workspace save/load round-trip through the record descriptor
// ---------------------------------------------------------------------------

TEST_CASE( "saveCollectionToWorkspace / loadCollectionFromWorkspace round-trip",
           "[temporal][workspace]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QStringList paths = makeScenes( dir );
    sicnu::data::DataManager dm;
    registerScenes( dm, paths );

    TemporalCollection collection = TemporalCollection::fromScenePaths( paths, {}, QStringLiteral( "rt" ) );
    const auto recordId = saveCollectionToWorkspace( dm, QStringLiteral( "rt" ), collection );
    REQUIRE_FALSE( recordId.isNull() );

    TemporalCollection loaded;
    QString error;
    REQUIRE( loadCollectionFromWorkspace( dm, recordId, &loaded, &error ) );
    CHECK( loaded.sceneCount() == 3 );
    CHECK( loaded.scenes().first().time.valid );
    // Chronological order survived the round-trip.
    CHECK( loaded.scenes().first().time.iso.startsWith( QStringLiteral( "2024-01" ) ) );
    CHECK( loaded.scenes().last().time.iso.startsWith( QStringLiteral( "2024-03" ) ) );
    // Scene asset bindings survived (path → assetId + revision).
    CHECK_FALSE( loaded.scenes().first().assetId.isEmpty() );
}

// ---------------------------------------------------------------------------
// STAC ingestion
// ---------------------------------------------------------------------------

TEST_CASE( "STAC item parsing: asset selection, cloud metadata, platform",
           "[temporal][stac]" )
{
    ensureApp();
    const Json::Value doc = stacSearchDoc();
    const Json::Value &features = doc["features"];

    StacItem item;
    QString error;
    REQUIRE( parseStacItem( features[0], &item, &error ) );
    CHECK( item.id == "item-03" );
    CHECK( item.datetime == QStringLiteral( "2024-03-15T10:20:30Z" ) );
    CHECK( item.platform == QStringLiteral( "Sentinel-2A" ) );
    CHECK( item.cloudCover == Catch::Approx( 12.5 ) );
    // The TIFF asset wins over the JPEG thumbnail (type-based pass).
    CHECK( item.rasterHref ==
           QStringLiteral( "/vsicurl/https://example.com/s2/item-03_B03.tif" ) );
    CHECK( item.rasterAssetKey == QStringLiteral( "green" ) );
    CHECK( item.rasterBands.contains( QStringLiteral( "green" ) ) );
    CHECK( item.hasGeometry );
    CHECK( item.properties.contains( QStringLiteral( "platform" ) ) );

    // No-datetime item is rejected (acquisition time is mandatory).
    StacItem bad;
    CHECK_FALSE( parseStacItem( features[3], &bad, &error ) );
    CHECK_FALSE( error.isEmpty() );
    // No-raster-asset item is rejected.
    CHECK_FALSE( parseStacItem( features[4], &bad, &error ) );
}

TEST_CASE( "STAC search ingestion: chronological order + warnings for bad items",
           "[temporal][stac]" )
{
    ensureApp();
    const Json::Value doc = stacSearchDoc();
    TemporalCollection collection;
    QString error;
    QStringList warnings;
    REQUIRE( temporalCollectionFromStacSearch( doc, QStringLiteral( "s2-series" ), &collection,
                                               &error, &warnings ) );
    CHECK( collection.sceneCount() == 3 );
    // Chronological ordering regardless of input order.
    CHECK( collection.scenes().first().time.iso.startsWith( QStringLiteral( "2024-01" ) ) );
    CHECK( collection.scenes().last().time.iso.startsWith( QStringLiteral( "2024-03" ) ) );
    // Scene metadata carried through.
    CHECK( collection.scenes().first().platform == QStringLiteral( "Sentinel-2B" ) );
    CHECK( collection.scenes().first().timeSource == QStringLiteral( "stac" ) );
    CHECK( collection.scenes().first().path ==
           QStringLiteral( "/vsicurl/https://example.com/s2/item-01_B03.tif" ) );
    // Both invalid items produced warnings.
    CHECK( warnings.size() == 2 );
}

TEST_CASE( "STAC filters: datetime range, bbox, property, limit",
           "[temporal][stac]" )
{
    ensureApp();
    const Json::Value doc = stacSearchDoc();
    QVector<StacItem> items;
    for ( const Json::Value &feature : doc["features"] )
    {
        StacItem item;
        if ( parseStacItem( feature, &item, nullptr ) )
            items.append( item );
    }
    REQUIRE( items.size() == 3 );

    // Datetime range: only February.
    QVector<StacItem> feb = filterStacItems( items, QString(), QStringLiteral( "2024-02-01/2024-02-28" ),
                                             -1, QString(), nullptr );
    REQUIRE( feb.size() == 1 );
    CHECK( feb.first().id == "item-02" );

    // Single-instant date matches that whole calendar day.
    QVector<StacItem> day = filterStacItems( items, QString(), QStringLiteral( "2024-03-15" ),
                                             -1, QString(), nullptr );
    REQUIRE( day.size() == 1 );
    CHECK( day.first().id == "item-03" );

    // Bbox: disjoint footprint drops everything.
    QVector<StacItem> none = filterStacItems( items, QStringLiteral( "0,0,1,1" ), QString(), -1,
                                              QString(), nullptr );
    CHECK( none.isEmpty() );
    // Overlapping bbox keeps everything.
    QVector<StacItem> all = filterStacItems( items, QStringLiteral( "100,30,101,31" ), QString(), -1,
                                             QString(), nullptr );
    CHECK( all.size() == 3 );

    // Property filter (numeric).
    QVector<StacItem> clear = filterStacItems( items, QString(), QString(), -1,
                                               QStringLiteral( "eo:cloud_cover=3.1" ), nullptr );
    REQUIRE( clear.size() == 1 );
    CHECK( clear.first().id == "item-01" );
    // Property filter (string).
    QVector<StacItem> bPlatform = filterStacItems( items, QString(), QString(), -1,
                                                   QStringLiteral( "platform=Sentinel-2B" ), nullptr );
    REQUIRE( bPlatform.size() == 1 );
    CHECK( bPlatform.first().id == "item-01" );

    // Limit.
    QVector<StacItem> limited = filterStacItems( items, QString(), QString(), 2, QString(), nullptr );
    CHECK( limited.size() == 2 );
    CHECK( limited.first().id == "item-01" ); // still chronological
}

TEST_CASE( "STAC datetime range treats a date-only end bound as inclusive",
           "[temporal][stac]" )
{
    ensureApp();
    StacItem onEnd;
    onEnd.id = QStringLiteral( "on-end" );
    onEnd.datetime = QStringLiteral( "2024-02-28T10:30:00Z" );
    StacItem afterEnd;
    afterEnd.id = QStringLiteral( "after-end" );
    afterEnd.datetime = QStringLiteral( "2024-03-01T00:00:01Z" );
    StacItem beforeStart;
    beforeStart.id = QStringLiteral( "before-start" );
    beforeStart.datetime = QStringLiteral( "2024-01-31T23:59:59Z" );

    const QVector<StacItem> items{ onEnd, afterEnd, beforeStart };
    const QVector<StacItem> feb =
      filterStacItems( items, QString(), QStringLiteral( "2024-02-01/2024-02-28" ), -1, QString(),
                       nullptr );
    REQUIRE( feb.size() == 1 );
    CHECK( feb.first().id == QStringLiteral( "on-end" ) );
}

TEST_CASE( "STAC datetime range rejects malformed bounds instead of treating them as open",
           "[temporal][stac]" )
{
    ensureApp();
    const Json::Value doc = stacSearchDoc();
    QVector<StacItem> items;
    for ( const Json::Value &feature : doc["features"] )
    {
        StacItem item;
        if ( parseStacItem( feature, &item, nullptr ) )
            items.append( item );
    }
    REQUIRE( items.size() == 3 );

    QStringList warnings;
    const QVector<StacItem> garbageEnd =
      filterStacItems( items, QString(), QStringLiteral( "2024-02-01/not-a-date" ), -1, QString(),
                       &warnings );
    REQUIRE_FALSE( warnings.isEmpty() );
    CHECK( warnings.join( QLatin1Char( ' ' ) ).contains( QStringLiteral( "malformed datetime" ) ) );
    CHECK( garbageEnd.size() == items.size() );

    warnings.clear();
    const QVector<StacItem> garbageStart =
      filterStacItems( items, QString(), QStringLiteral( "nope/2024-02-28" ), -1, QString(),
                       &warnings );
    REQUIRE_FALSE( warnings.isEmpty() );
    CHECK( garbageStart.size() == items.size() );
}

TEST_CASE( "STAC numeric properties round-trip at full precision", "[temporal][stac]" )
{
    ensureApp();
    Json::Value feature( Json::objectValue );
    feature["id"] = "precise";
    feature["type"] = "Feature";
    feature["properties"]["datetime"] = "2024-02-15T10:20:30Z";
    feature["properties"]["eo:cloud_cover"] = 12.345678;
    feature["assets"]["green"]["href"] = "https://example.com/s2/precise.tif";
    feature["assets"]["green"]["type"] = "image/tiff; application=geotiff";

    StacItem item;
    REQUIRE( parseStacItem( feature, &item, nullptr ) );
    bool storedOk = false;
    CHECK( item.properties[QStringLiteral( "eo:cloud_cover" )].toDouble( &storedOk ) == 12.345678 );
    CHECK( storedOk );

    const QVector<StacItem> matched =
      filterStacItems( { item }, QString(), QString(), -1, QStringLiteral( "eo:cloud_cover=12.345678" ),
                       nullptr );
    REQUIRE( matched.size() == 1 );
    CHECK( matched.first().id == QStringLiteral( "precise" ) );
}

TEST_CASE( "bindCollectionAssets binds https STAC hrefs to /vsicurl-canonical assets",
           "[temporal][stac][workspace]" )
{
    ensureApp();
    sicnu::data::DataManager dm;
    const QString href1 = QStringLiteral( "https://example.com/s2/item-01_B03.tif" );
    const QString href2 = QStringLiteral( "https://example.com/s2/item-02_B03.tif" );
    QList<sicnu::data::AssetId> ids;
    for ( const QString &href : { href1, href2 } )
    {
        sicnu::data::SourceDescriptor source;
        source.providerKey = QStringLiteral( "gdal" );
        source.canonicalSource = href;
        const auto registered = dm.registerSource( sicnu::data::RegisterRequest{ source } );
        REQUIRE_FALSE( registered.assetId.isNull() );
        ids.append( registered.assetId );
        const auto asset = dm.asset( registered.assetId );
        REQUIRE( asset.has_value() );
        CHECK( asset->source().canonicalSource == QStringLiteral( "/vsicurl/" ) + href );
    }

    TemporalCollection collection;
    collection.setName( QStringLiteral( "remote-cogs" ) );
    TemporalSceneRef a;
    a.path = href1;
    TemporalSceneRef b;
    b.path = href2;
    collection.scenes().push_back( a );
    collection.scenes().push_back( b );

    CHECK( bindCollectionAssets( collection, &dm ) == 2 );
    CHECK( collection.scenes()[0].assetId == ids[0].toString() );
    CHECK( collection.scenes()[1].assetId == ids[1].toString() );
    CHECK_FALSE( collection.scenes()[0].assetRevision.isEmpty() );

    collection.scenes()[0].path = QStringLiteral( "https://example.com/not-registered.tif" );
    CHECK( bindCollectionAssets( collection, &dm ) == 1 );
    CHECK( collection.scenes()[0].assetId.isEmpty() );
    CHECK( collection.scenes()[0].assetRevision.isEmpty() );
    CHECK( collection.scenes()[1].assetId == ids[1].toString() );
}

TEST_CASE( "STAC ingestion refuses fewer than two scenes", "[temporal][stac]" )
{
    ensureApp();
    QVector<StacItem> one;
    StacItem item;
    REQUIRE( parseStacItem( stacSearchDoc()["features"][0], &item, nullptr ) );
    one.append( item );
    TemporalCollection collection;
    QString error;
    CHECK_FALSE( temporalCollectionFromStacItems( one, QStringLiteral( "single" ), &collection, &error ) );
    CHECK_FALSE( error.isEmpty() );
}

// ---------------------------------------------------------------------------
// Execution cache regression (#720): clear() clears the path store
// ---------------------------------------------------------------------------

TEST_CASE( "ExecutionResultCache::clear clears the output-path store too (#720)",
           "[workflow][cache]" )
{
    ensureApp();
    auto &cache = sicnu::data::ExecutionResultCache::instance();
    cache.setEnabled( true );

    const sicnu::data::ExecutionFingerprint fp1 =
      sicnu::data::makeExecutionFingerprintV2( QStringLiteral( "op" ), QStringLiteral( "1" ),
                                               QJsonObject(), {} );
    const sicnu::data::ExecutionFingerprint fp2 =
      sicnu::data::makeExecutionFingerprintV2( QStringLiteral( "op2" ), QStringLiteral( "1" ),
                                               QJsonObject(), {} );
    REQUIRE( fp1.isValid() );
    REQUIRE( fp2.isValid() );
    REQUIRE( fp1 != fp2 );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path1 = dir.filePath( QStringLiteral( "out1.tif" ) );
    const QString path2 = dir.filePath( QStringLiteral( "out2.tif" ) );
    QFile f1( path1 );
    REQUIRE( f1.open( QIODevice::WriteOnly ) );
    f1.write( "x" );
    f1.close();
    QFile f2( path2 );
    REQUIRE( f2.open( QIODevice::WriteOnly ) );
    f2.write( "y" );
    f2.close();

    cache.storeOutputPath( fp1, path1 );
    cache.storeOutputPath( fp2, path2 );
    REQUIRE( cache.lookupOutputPath( fp1 ).has_value() );

    cache.clear();
    CHECK( cache.lookupOutputPath( fp1 ) == std::nullopt );
    CHECK( cache.lookupOutputPath( fp2 ) == std::nullopt );
    CHECK( cache.pathSize() == 0 );
    CHECK( cache.size() == 0 );

    cache.setEnabled( false );
}

TEST_CASE( "Remote and VSI inputs resolve or fail conservative — never omit (#726)",
           "[temporal][workspace][fingerprint][remote]" )
{
    ensureApp();
    sicnu::data::DataManager dm;

    QVector<sicnu::data::TaggedDerivationInput> inputs;
    QString reason;

    // An unregistered https URL must FAIL the whole policy — silently
    // dropping it (the old QFileInfo gate) is exactly how false hits are born.
    QVariantMap params;
    params.insert( QStringLiteral( "input" ),
                   QStringLiteral( "https://example.com/unregistered.tif" ) );
    REQUIRE_FALSE( fingerprintInputsForOperatorParams( &dm, params, &inputs, &reason ) );
    REQUIRE( reason.contains( QStringLiteral( "https://example.com/unregistered.tif" ) ) );

    // Same for /vsicurl/, /vsis3/ and OGR connection strings.
    for ( const char *remote : { "/vsicurl/https://example.com/a.tif",
                                 "/vsis3/bucket/key.tif",
                                 "PG:dbname=production host=db.example.com" } )
    {
        QVariantMap p;
        p.insert( QStringLiteral( "input" ), QString::fromLatin1( remote ) );
        INFO( "datasource: " << remote );
        REQUIRE_FALSE( fingerprintInputsForOperatorParams( &dm, p, &inputs, &reason ) );
        REQUIRE_FALSE( reason.isEmpty() );
    }

    // A colon-bearing datasource string the classifier does not know
    // (GDAL subdataset syntax) must not pass as a scientific parameter either.
    QVariantMap subdataset;
    subdataset.insert( QStringLiteral( "input" ),
                       QStringLiteral( "HDF5:\"/data/timeseries.h5\"://ndvi" ) );
    REQUIRE_FALSE( fingerprintInputsForOperatorParams( &dm, subdataset, &inputs, &reason ) );

    // Scientific VALUES that merely look stringy are not datasource
    // candidates and do not fail the policy on their own.
    QVariantMap scientific;
    scientific.insert( QStringLiteral( "index" ), QStringLiteral( "NDVI" ) );
    scientific.insert( QStringLiteral( "kernel" ), 3 );
    scientific.insert( QStringLiteral( "output" ), QStringLiteral( "/tmp/unused.tif" ) );
    inputs.clear();
    REQUIRE( fingerprintInputsForOperatorParams( &dm, scientific, &inputs, &reason ) );
    REQUIRE( inputs.isEmpty() );
}
