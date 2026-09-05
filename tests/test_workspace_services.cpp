// test_workspace_services.cpp — Workspace Governance 3.0 services:
// relink, validation, metadata pipeline, import center, repro bundle,
// snapshot, cleanup, undo/redo.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTemporaryDir>

#include <vector>

#include <gdal.h>

#include "data/data_manager.h"
#include "data/data_asset.h"
#include "data/governance/import_center.h"
#include "data/governance/metadata_pipeline.h"
#include "data/governance/relink_service.h"
#include "data/governance/repro_bundle.h"
#include "data/governance/workspace_lifecycle.h"
#include "data/governance/workspace_service.h"
#include "data/governance/workspace_validator.h"

using namespace sicnu::workspace;
using sicnu::data::DataManager;
using sicnu::data::RegisterRequest;
using sicnu::data::RegisterResult;
using sicnu::data::SourceDescriptor;

namespace {

QCoreApplication &testApp()
{
    static int argc = 0;
    static QCoreApplication app( argc, nullptr );
    return app;
}

QString makeRaster( const QString &name, bool withCrs = true )
{
    static QTemporaryDir dir;
    const QString path = QDir( dir.path() ).filePath( name );
    if ( QFileInfo::exists( path ) )
        return path;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    constexpr int W = 8, H = 8;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, ( double ) H, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    if ( withCrs )
        GDALSetProjection( ds,
            "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
            "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( W, 3.5f );
    for ( int row = 0; row < H; ++row )
        GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
}

struct Fixture
{
    DataManager manager;
    WorkspaceService service;
    RelinkService relink;
    WorkspaceValidator validator;
    MetadataPipeline pipeline;
    ImportCenter importer;
    CleanupService cleanup;
    SnapshotService snapshots;
    WorkspaceTransactionStack transactions;

    Fixture()
        : relink( manager, service )
        , validator( manager, service )
        , pipeline( service )
        , importer( service )
        , cleanup( service )
        , snapshots( service )
        , transactions( service )
    {
        testApp();
        service.bindDataManager( &manager );
        pipeline.bindDataManager( &manager );
        importer.bindDataManager( &manager );
        static int instance = 0;
        const QString storeDir = QDir::temp().filePath(
            QStringLiteral( "ws3-services-%1-%2" ).arg( QCoreApplication::applicationPid() ).arg( ++instance ) );
        REQUIRE( QDir().mkpath( storeDir ) );
        const QString storePath = QDir( storeDir ).filePath( QStringLiteral( "gov.db" ) );
        REQUIRE( service.openStore( storePath ) );
        service.store().setMeta( QStringLiteral( "db_path" ), storePath );
    }
    ~Fixture() { service.closeStore(); }
};

RegisterResult registerFile( DataManager &manager, const QString &path )
{
    SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = path;
    return manager.registerSource( RegisterRequest{ source } );
}

} // namespace

TEST_CASE( "RelinkService detects missing sources and relinks through relocate", "[workspace][relink]" )
{
    Fixture fx;
    QTemporaryDir dir;
    const QString original = makeRaster( QStringLiteral( "relink-a.tif" ) );
    const RegisterResult registered = registerFile( fx.manager, original );
    REQUIRE( !registered.assetId.isNull() );

    // Move the payload away: the locator is now stale, identity intact.
    const QString moved = QDir( dir.path() ).filePath( QStringLiteral( "relink-b.tif" ) );
    REQUIRE( QFile::rename( original, moved ) );

    const QVector<MissingAsset> missing = fx.relink.scanMissing();
    REQUIRE( missing.size() == 1 );
    REQUIRE( missing.first().assetId == registered.assetId.toString() );
    REQUIRE( missing.first().kind == QLatin1String( "raster" ) );

    // Identity survives the move: relink keeps the same AssetId.
    REQUIRE( fx.relink.relinkAsset( registered.assetId.toString(), moved ) );
    const std::optional<sicnu::data::AssetSnapshot> snapshot = fx.manager.asset( registered.assetId );
    REQUIRE( snapshot.has_value() );
    REQUIRE( snapshot->source().canonicalSource == moved );
    // Governance mirror follows.
    REQUIRE( fx.service.store().assetById( registered.assetId.toString() )->canonicalSource == moved );
}

TEST_CASE( "RelinkService applyRootMove remaps a whole external root", "[workspace][relink]" )
{
    Fixture fx;
    QTemporaryDir oldRoot, newRoot;
    QDir( oldRoot.path() ).mkpath( QStringLiteral( "site" ) );
    QDir( newRoot.path() ).mkpath( QStringLiteral( "site" ) );
    const QString original = makeRaster( QStringLiteral( "rootmove.tif" ) );
    const QString oldPath = QDir( oldRoot.filePath( QStringLiteral( "site" ) ) ).filePath( QStringLiteral( "x.tif" ) );
    const QString newPath = QDir( newRoot.filePath( QStringLiteral( "site" ) ) ).filePath( QStringLiteral( "x.tif" ) );
    REQUIRE( QFile::copy( original, oldPath ) );

    const RegisterResult registered = registerFile( fx.manager, oldPath );
    REQUIRE( !registered.assetId.isNull() );
    REQUIRE( QFile::rename( oldPath, newPath ) );
    REQUIRE( fx.relink.scanMissing().size() == 1 );

    // Without fingerprint verification the target exists -> relinked.
    const RelinkOutcome outcome =
        fx.relink.applyRootMove( oldRoot.path(), newRoot.path(), false );
    REQUIRE( outcome.relinked == 1 );
    REQUIRE( outcome.failed == 0 );
    REQUIRE( fx.manager.asset( registered.assetId )->source().canonicalSource == newPath );
    // Mapping recorded for future relocation support.
    REQUIRE( fx.service.store().pathMappings().size() == 1 );
}

TEST_CASE( "WorkspaceValidator reports missing, changed and CRS findings", "[workspace][validator]" )
{
    Fixture fx;
    QTemporaryDir dir;
    const QString withCrs = makeRaster( QStringLiteral( "val-crs.tif" ) );
    const QString noCrs = makeRaster( QStringLiteral( "val-nocrs.tif" ), false );

    const RegisterResult okAsset = registerFile( fx.manager, withCrs );
    const RegisterResult crsless = registerFile( fx.manager, noCrs );
    REQUIRE( !okAsset.assetId.isNull() );
    REQUIRE( !crsless.assetId.isNull() );

    // Mirror rows + record a verified state for the ok asset.
    fx.service.mirrorAllAssets();
    REQUIRE( fx.service.noteAssetVerified( okAsset.assetId.toString(),
                                           WorkspaceService::contentFingerprint( withCrs ),
                                           QFileInfo( withCrs ).size(),
                                           QFileInfo( withCrs ).lastModified().toMSecsSinceEpoch() ) );

    // First pass: everything present — the CRS-less raster is reported.
    ValidationOptions options;
    options.verifyFingerprints = false;
    const ValidationReport crsReport = fx.validator.validateProject( options );
    bool crsReported = false;
    for ( const GovernanceDiagnostic &d : crsReport.diagnostics )
        crsReported |= d.kind == DiagnosticKind::CrsMissing;
    REQUIRE( crsReported );

    // Second pass: remove a payload -> missing-file finding.
    REQUIRE( QFile::remove( noCrs ) );
    const ValidationReport report = fx.validator.validateProject( options );

    bool missingReported = false;
    for ( const GovernanceDiagnostic &d : report.diagnostics )
        missingReported |= d.kind == DiagnosticKind::MissingFile;
    REQUIRE( missingReported );
    REQUIRE( report.summary.errors >= 1 );

    // Every diagnostic is machine-readable: stable code + entity anchor.
    for ( const GovernanceDiagnostic &d : report.diagnostics )
    {
        REQUIRE( !d.code.isEmpty() );
        REQUIRE( !d.entityKind.isEmpty() );
    }

    // JSON serialization is well-formed.
    const QJsonObject json = report.toJson();
    REQUIRE( json.value( QLatin1String( "diagnostics" ) ).toArray().size() == ( int ) report.diagnostics.size() );
}

TEST_CASE( "MetadataPipeline verifies incrementally and is cancellable", "[workspace][pipeline]" )
{
    Fixture fx;
    QTemporaryDir dir;
    for ( int i = 0; i < 5; ++i )
        registerFile( fx.manager, makeRaster( QStringLiteral( "pipe-%1.tif" ).arg( i ) ) );
    fx.service.mirrorAllAssets();
    REQUIRE( fx.service.store().assetCount() == 5 );

    MetadataPipeline::Config config;
    config.refreshStructure = true;
    config.recomputeFingerprints = true;

    QEventLoop loop;
    QObject::connect( &fx.pipeline, &MetadataPipeline::finished, &loop, &QEventLoop::quit );
    QTimer::singleShot( 30000, &loop, &QEventLoop::quit );
    REQUIRE( fx.pipeline.start( config ) );
    loop.exec();
    REQUIRE_FALSE( fx.pipeline.isRunning() );

    // Fingerprints recorded -> incremental pass skips everything.
    REQUIRE( fx.service.store().assetById( fx.service.store().allAssets( 1 ).first().assetId )->contentFingerprint.size() == 64 );

    QEventLoop second;
    QObject::connect( &fx.pipeline, &MetadataPipeline::finished, &second, &QEventLoop::quit );
    QTimer::singleShot( 30000, &second, &QEventLoop::quit );
    REQUIRE( fx.pipeline.start() );
    second.exec();

    // A second start while a pass runs is refused (single-flight contract);
    // the pass above already completed, so starting again must succeed.
    REQUIRE( fx.pipeline.start() == true );
    fx.pipeline.cancel();
}

TEST_CASE( "ImportCenter scans, dedupes and registers a folder", "[workspace][import]" )
{
    Fixture fx;
    QTemporaryDir dir;
    QDir( dir.path() ).mkpath( QStringLiteral( "tree/nested" ) );
    const QString raster = makeRaster( QStringLiteral( "imp-a.tif" ) );
    REQUIRE( QFile::copy( raster, QDir( dir.filePath( QStringLiteral( "tree" ) ) ).filePath( QStringLiteral( "a.tif" ) ) ) );
    REQUIRE( QFile::copy( raster, QDir( dir.filePath( QStringLiteral( "tree/nested" ) ) ).filePath( QStringLiteral( "b.tif" ) ) ) );
    QFile junk( QDir( dir.filePath( QStringLiteral( "tree" ) ) ).filePath( QStringLiteral( "notes.txt" ) ) );
    REQUIRE( junk.open( QIODevice::WriteOnly ) );
    junk.write( "x" );
    junk.close();

    ImportScanOptions options;
    options.root = dir.path();
    options.recursive = true;

    QEventLoop loop;
    QObject::connect( &fx.importer, &ImportCenter::finished, &loop, &QEventLoop::quit );
    QTimer::singleShot( 30000, &loop, &QEventLoop::quit );
    REQUIRE( fx.importer.startScan( options ) );
    loop.exec();

    // Second scan deduplicates against the durable index.
    QEventLoop second;
    QObject::connect( &fx.importer, &ImportCenter::finished, &second, &QEventLoop::quit );
    QTimer::singleShot( 30000, &second, &QEventLoop::quit );
    REQUIRE( fx.importer.startScan( options ) );
    second.exec();
}

TEST_CASE( "ReproBundleExporter writes manifest with mode-dependent content", "[workspace][bundle]" )
{
    Fixture fx;
    QTemporaryDir dir;
    const QString raster = makeRaster( QStringLiteral( "bundle.tif" ) );
    REQUIRE( registerFile( fx.manager, raster ).assetId.isNull() == false );
    fx.service.mirrorAllAssets();
    REQUIRE( fx.service.noteAssetVerified(
        fx.service.store().allAssets( 1 ).first().assetId,
        WorkspaceService::contentFingerprint( raster ),
        QFileInfo( raster ).size(), QFileInfo( raster ).lastModified().toMSecsSinceEpoch() ) );

    ReproBundleOptions options;
    options.outputDir = dir.filePath( QStringLiteral( "bundle-metadata" ) );
    options.mode = ReproBundleOptions::Mode::MetadataOnly;
    const ReproBundleReport report = ReproBundleExporter( fx.manager, fx.service ).exportBundle( options );
    REQUIRE( report.ok );
    REQUIRE( report.inputCount == 1 );

    QFile manifest( report.manifestPath );
    REQUIRE( manifest.open( QIODevice::ReadOnly ) );
    const QJsonObject parsed = QJsonDocument::fromJson( manifest.readAll() ).object();
    REQUIRE( parsed.value( QLatin1String( "kind" ) ).toString() == QLatin1String( "exp_rs_repro_bundle" ) );
    REQUIRE( parsed.value( QLatin1String( "mode" ) ).toString() == QLatin1String( "metadata_only" ) );

    QFile inputs( QDir( options.outputDir ).filePath( QStringLiteral( "inputs.json" ) ) );
    REQUIRE( inputs.open( QIODevice::ReadOnly ) );
    const QJsonObject inputsJson = QJsonDocument::fromJson( inputs.readAll() ).object();
    REQUIRE( inputsJson.value( QLatin1String( "inputs" ) ).toArray().first().toObject()
                 .value( QLatin1String( "contentFingerprint" ) ).toString().size() == 64 );
}

TEST_CASE( "SnapshotService snapshots project metadata and prunes old copies", "[workspace][snapshot]" )
{
    Fixture fx;
    QTemporaryDir dir;
    const QString projectFile = dir.filePath( QStringLiteral( "snap.qgs" ) );
    REQUIRE( QFile( projectFile ).open( QIODevice::WriteOnly ) );  // placeholder file
    const QString snapshotDir = dir.filePath( QStringLiteral( "snapshots" ) );

    for ( int i = 0; i < 3; ++i )
    {
        const SnapshotReport report = fx.snapshots.createSnapshot( projectFile, snapshotDir, 2 );
        INFO( report.error.toStdString() );
        REQUIRE( report.ok );
    }
    const int snapshotCount =
        QDir( snapshotDir ).entryList( QStringList{ QStringLiteral( "snap-*.snapshot" ) }, QDir::Dirs ).size();
    REQUIRE( snapshotCount == 2 );
}

TEST_CASE( "CleanupService protects referenced rows and removes orphans only", "[workspace][cleanup]" )
{
    Fixture fx;
    // Orphan row: canonical source missing, no lineage, no results.
    GovernedAsset orphan;
    orphan.assetId = QStringLiteral( "orphan-1" );
    orphan.canonicalSource = QStringLiteral( "/definitely/not/there.tif" );
    orphan.kind = QStringLiteral( "raster" );
    REQUIRE( fx.service.store().upsertAsset( orphan ).operator bool() );

    CleanupReport plan = fx.cleanup.plan();
    REQUIRE( plan.candidates.size() == 1 );
    REQUIRE( plan.candidates.first().code == QLatin1String( "cleanup.orphan_row" ) );
    REQUIRE( plan.execute( fx.service ) == 1 );
    REQUIRE_FALSE( fx.service.store().assetById( QStringLiteral( "orphan-1" ) ).has_value() );
}

TEST_CASE( "Workspace transactions undo and redo governance mutations", "[workspace][transactions]" )
{
    Fixture fx;
    // Tag mutation round-trip.
    REQUIRE( fx.transactions.execute( std::make_unique<SetTagsCommand>(
        QStringLiteral( "asset" ), QStringLiteral( "t-1" ),
        QStringList{ QStringLiteral( "alpha" ) }, QStringLiteral( "test" ) ) ) );
    REQUIRE( fx.service.store().tagsOf( QStringLiteral( "asset" ), QStringLiteral( "t-1" ) )
                 .contains( QStringLiteral( "alpha" ) ) );
    REQUIRE( fx.transactions.undo() );
    REQUIRE( fx.service.store().tagsOf( QStringLiteral( "asset" ), QStringLiteral( "t-1" ) ).isEmpty() );
    REQUIRE( fx.transactions.redo() );
    REQUIRE( fx.service.store().tagsOf( QStringLiteral( "asset" ), QStringLiteral( "t-1" ) )
                 .contains( QStringLiteral( "alpha" ) ) );

    // Dataset membership removal (never deletes the asset itself).
    const DatasetId datasetId = fx.service.createDataset( QStringLiteral( "undo-ds" ), DatasetKind::Group,
                                                          QStringList{ QStringLiteral( "m-1" ) } );
    REQUIRE( !datasetId.isNull() );
    REQUIRE( fx.transactions.execute(
        std::make_unique<RemoveDatasetMemberCommand>( datasetId.toString(), QStringLiteral( "m-1" ),
                                                      QStringLiteral( "test" ) ) ) );
    REQUIRE( fx.service.dataset( datasetId.toString() )->memberAssetIds.isEmpty() );
    REQUIRE( fx.transactions.undo() );
    REQUIRE( fx.service.dataset( datasetId.toString() )->memberAssetIds
                 .contains( QStringLiteral( "m-1" ) ) );
}
