// test_workspace_project_v3.cpp — Project Format v3 (Workspace Governance 3.0):
// v3 roundtrip, legacy v1 migration (in-memory, non-destructive), unknown-field
// tolerance, and atomic-save hygiene.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>

#include "app/data_project_serializer.h"
#include "app/project_context.h"
#include <gdal.h>
#include <cpl_conv.h>
#include <sqlite3.h>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/governance/governance_types.h"

using namespace sicnu::workspace;
using sicnu::data::RegisterRequest;
using sicnu::data::RegisterResult;
using sicnu::data::SourceDescriptor;

namespace {

QString syntheticSample( const QString &relative )
{
  static QTemporaryDir dir;
  static QMap<QString, QString> cache;
  auto it = cache.constFind( relative );
  if ( it != cache.constEnd() )
    return it.value();
  GDALAllRegister();
  const QString path = dir.path() + QLatin1Char( '/' ) +
                       QString::number( cache.size() ) + QStringLiteral( ".tif" );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 16, H = 16;
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection( ds,
    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
    "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  cache.insert( relative, path );
  return path;
}

SourceDescriptor rasterSource()
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = syntheticSample( QStringLiteral( "samples/ws3_asset.tif" ) );
  return source;
}

} // namespace

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "Project format v3 round-trips governed state", "[project][workspace_v3]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "v3.qgs" ) );

  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();

  // Governance store open against the project path => writes become v3.
  REQUIRE( context->openWorkspaceStore( projectPath ) );

  const RegisterResult registered =
      context->dataManager().registerSource( RegisterRequest{ rasterSource() } );
  REQUIRE( !registered.assetId.isNull() );

  DatasetId datasetId = context->workspaceService().createDataset(
      QStringLiteral( "water-2025" ), DatasetKind::Training,
      QStringList{ registered.assetId.toString() } );
  REQUIRE( !datasetId.isNull() );

  ResultRecord result;
  result.semanticType = ResultSemanticType::Classification;
  result.header.name = QStringLiteral( "rf-water" );
  ResultInput input;
  input.assetId = registered.assetId.toString();
  input.revision = 1;
  result.inputs.append( input );
  const ResultId resultId =
      context->workspaceService().registerResult( result );
  REQUIRE( !resultId.isNull() );

  REQUIRE( context->workspaceService().setAssetTags(
      registered.assetId.toString(), QStringList{ QStringLiteral( "qa" ) } ) );

  // ---- write (v3) ------------------------------------------------------------
  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      writeSucceeded = static_cast<bool>( serializer.write( document, *context ) );
                    } );
  QObject::connect( project, &QgsProject::readProject, &signalReceiver,
                    [&]( const QDomDocument &document ) {
                      static_cast<void>( serializer.read( document, *project, *context ) );
                    } );
  REQUIRE( project->write( projectPath ) );
  REQUIRE( writeSucceeded );

  QFile saved( projectPath );
  REQUIRE( saved.open( QIODevice::ReadOnly ) );
  const QByteArray savedXml = saved.readAll();
  saved.close();
  const QString xml = QString::fromUtf8( savedXml );
  REQUIRE( xml.contains( QStringLiteral( "version=\"3\"" ) ) );
  REQUIRE( xml.contains( QStringLiteral( "<workspace" ) ) );
  REQUIRE( xml.contains( QStringLiteral( "water-2025" ) ) );
  // No save temp files left behind (atomic save hygiene).
  QDir projectDir( dir.path() );
  REQUIRE( projectDir.entryList( QStringList{ QStringLiteral( ".qgis-save-*.tmp" ) } ).isEmpty() );

  // ---- clear + reopen ---------------------------------------------------------
  REQUIRE( context->clearProject( *project ) );
  context->workspaceService().store().clearAll();

  REQUIRE( project->read( projectPath ) );

  REQUIRE( context->workspaceService().datasets().size() == 1 );
  REQUIRE( context->workspaceService().datasets().first().header.name
           == QLatin1String( "water-2025" ) );
  REQUIRE( context->workspaceService().datasets().first().memberAssetIds.size() == 1 );
  REQUIRE( context->workspaceService().results().size() == 1 );
  REQUIRE( context->workspaceService().results().first().status == ResultStatus::Draft );
  // The written asset is mirrored again (same id, not re-registered).
  REQUIRE( context->workspaceService().store().assetById( registered.assetId.toString() ).has_value() );
  REQUIRE( context->workspaceService().store()
               .tagsOf( QStringLiteral( "asset" ), registered.assetId.toString() )
               .contains( QStringLiteral( "qa" ) ) );

  context->workspaceService().closeStore();
}

TEST_CASE( "Legacy v1 projects migrate in memory without touching the file",
           "[project][workspace_v3][migration]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "legacy.qgs" ) );

  // ---- produce a v1 file: a context whose governance store is NOT open -------
  {
    sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
        sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created.operator bool() );
    std::unique_ptr<sicnu::app::ProjectContext> context = created.take();

    sicnu::app::DataProjectSerializer serializer;
    QObject signalReceiver;
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      static_cast<void>( serializer.write( document, *context ) );
                    } );
    REQUIRE( project->write( projectPath ) );

    QFile saved( projectPath );
    REQUIRE( saved.open( QIODevice::ReadOnly ) );
    const QString xml = QString::fromUtf8( saved.readAll() );
    saved.close();
    REQUIRE( xml.contains( QStringLiteral( "version=\"1\"" ) ) );
    REQUIRE_FALSE( xml.contains( QStringLiteral( "<workspace" ) ) );
  }

  // ---- reopen with governance attached: v1 is accepted and migrated ----------
  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
  REQUIRE( context->openWorkspaceStore( projectPath ) );

  sicnu::app::DataProjectSerializer serializer;
  QVector<sicnu::data::Diagnostic> readDiagnostics;
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::readProject, &signalReceiver,
                    [&]( const QDomDocument &document ) {
                      const sicnu::data::Result<void> result =
                          serializer.read( document, *project, *context );
                      readDiagnostics = result.diagnostics();
                    } );
  REQUIRE( project->read( projectPath ) );

  bool migrationReported = false;
  for ( const sicnu::data::Diagnostic &d : readDiagnostics )
    migrationReported |= d.code == QLatin1String( "workspace.migrated_from_v1" );
  REQUIRE( migrationReported );

  // The file on disk is still v1 (migration is non-destructive on read).
  QFile onDisk( projectPath );
  REQUIRE( onDisk.open( QIODevice::ReadOnly ) );
  const QString xml = QString::fromUtf8( onDisk.readAll() );
  onDisk.close();
  REQUIRE( xml.contains( QStringLiteral( "version=\"1\"" ) ) );
  // The next save persists v3 (upgrade on save).
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      static_cast<void>( serializer.write( document, *context ) );
                    } );
  REQUIRE( project->write( projectPath ) );
  QFile upgraded( projectPath );
  REQUIRE( upgraded.open( QIODevice::ReadOnly ) );
  const QString upgradedXml = QString::fromUtf8( upgraded.readAll() );
  upgraded.close();
  REQUIRE( upgradedXml.contains( QStringLiteral( "version=\"3\"" ) ) );

  context->workspaceService().closeStore();
}

TEST_CASE( "Unknown workspace sections and fields are skipped, not fatal",
           "[project][workspace_v3][forward_tolerance]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "fwd.qgs" ) );

  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
  REQUIRE( context->openWorkspaceStore( projectPath ) );

  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      writeSucceeded = static_cast<bool>( serializer.write( document, *context ) );
                    } );
  REQUIRE( project->write( projectPath ) );
  REQUIRE( writeSucceeded );

  // Inject unknown elements/attributes into the extension block.
  QFile file( projectPath );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  QDomDocument doc;
  REQUIRE( doc.setContent( &file ) );
  file.close();
  QDomElement extension =
      doc.documentElement().firstChildElement( QStringLiteral( "sicnuDataManager" ) );
  REQUIRE( !extension.isNull() );
  extension.setAttribute( QStringLiteral( "futureAttribute" ), QStringLiteral( "ignored" ) );
  QDomElement bogus = doc.createElement( QStringLiteral( "futureSection" ) );
  bogus.setAttribute( QStringLiteral( "schemaVersion" ), QStringLiteral( "99" ) );
  extension.appendChild( bogus );
  REQUIRE( QFile::remove( projectPath ) );
  QFile out( projectPath );
  REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  out.write( doc.toByteArray( 2 ) );
  out.close();

  QObject::connect( project, &QgsProject::readProject, &signalReceiver,
                    [&]( const QDomDocument &document ) {
                      static_cast<void>( serializer.read( document, *project, *context ) );
                    } );
  REQUIRE( project->read( projectPath ) );

  context->workspaceService().closeStore();
}

TEST_CASE( "Corrupt governance DB never silently downgrades a v3 project",
           "[project][workspace_v3][fault][issue746]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "fault.qgs" ) );
  const QString dbPath = dir.filePath( QStringLiteral( "fault.governance.db" ) );

  // ---- author a v3 project with governed state -------------------------------
  {
    sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
        sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created.operator bool() );
    std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
    REQUIRE( context->openWorkspaceStore( projectPath ) );
    const RegisterResult registered =
        context->dataManager().registerSource( RegisterRequest{ rasterSource() } );
    REQUIRE( !registered.assetId.isNull() );
    REQUIRE( !context->workspaceService()
                  .createDataset( QStringLiteral( "fault-ds" ), DatasetKind::Training,
                                  QStringList{ registered.assetId.toString() } )
                  .isNull() );

    sicnu::app::DataProjectSerializer serializer;
    QObject signalReceiver;
    QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                      [&]( QDomDocument &document ) {
                        static_cast<void>( serializer.write( document, *context ) );
                      } );
    REQUIRE( project->write( projectPath ) );
    context->workspaceService().closeStore();
  }

  // ---- corrupt the governance DB on disk -------------------------------------
  {
    QFile db( dbPath );
    REQUIRE( db.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    db.write( "this is not a sqlite database, just garbage bytes" );
    db.close();
  }

  // ---- reopen with a failed store open: governed state must survive ----------
  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
  QString openError;
  REQUIRE_FALSE( context->workspaceService().openStore( dbPath, &openError ) );

  sicnu::app::DataProjectSerializer serializer;
  QVector<sicnu::data::Diagnostic> readDiagnostics;
  bool writeSucceeded = false;
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::readProject, &signalReceiver,
                    [&]( const QDomDocument &document ) {
                      const sicnu::data::Result<void> result =
                          serializer.read( document, *project, *context );
                      readDiagnostics = result.diagnostics();
                    } );
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      writeSucceeded = static_cast<bool>( serializer.write( document, *context ) );
                    } );
  REQUIRE( project->read( projectPath ) );

  // The unavailable store is surfaced, and the document is cached as the
  // last known governed state.
  bool unavailableReported = false;
  for ( const sicnu::data::Diagnostic &d : readDiagnostics )
    unavailableReported |= d.code == QLatin1String( "workspace.store_unavailable" );
  REQUIRE( unavailableReported );
  REQUIRE( context->workspaceService().hasCachedProjectJson() );

  // The save re-persists the cached governed document as v3 — never a v1
  // downgrade that would permanently destroy the governed state.
  REQUIRE( project->write( projectPath ) );
  REQUIRE( writeSucceeded );
  {
    QFile saved( projectPath );
    REQUIRE( saved.open( QIODevice::ReadOnly ) );
    const QString xml = QString::fromUtf8( saved.readAll() );
    saved.close();
    REQUIRE( xml.contains( QStringLiteral( "version=\"3\"" ) ) );
    REQUIRE( xml.contains( QStringLiteral( "<workspace" ) ) );
    REQUIRE( xml.contains( QStringLiteral( "fault-ds" ) ) );
  }

  context->workspaceService().closeStore();
}

TEST_CASE( "Project switch never carries cached governed state across projects",
           "[project][workspace_v3][fault][issue746]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString pathA = dir.filePath( QStringLiteral( "a.qgs" ) );
  const QString pathB = dir.filePath( QStringLiteral( "b.qgs" ) );
  const QString dbB = dir.filePath( QStringLiteral( "b.governance.db" ) );

  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();

  // ---- project A: governed v3 -------------------------------------------------
  REQUIRE( context->openWorkspaceStore( pathA ) );
  REQUIRE( !context->workspaceService()
                .createDataset( QStringLiteral( "project-a-ds" ), DatasetKind::Training )
                .isNull() );
  sicnu::app::DataProjectSerializer serializer;
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      static_cast<void>( serializer.write( document, *context ) );
                    } );
  REQUIRE( project->write( pathA ) );
  REQUIRE( context->workspaceService().hasCachedProjectJson() );

  // ---- switch to project B whose governance store is corrupt ------------------
  {
    QFile db( dbB );
    REQUIRE( db.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    db.write( "garbage" );
    db.close();
  }
  REQUIRE( context->clearProject( *project ) );
  // The transition must drop the cached document: project A's governed state
  // must never bleed into project B's file.
  REQUIRE_FALSE( context->workspaceService().hasCachedProjectJson() );
  REQUIRE_FALSE( context->workspaceService().isV3Seen() );
  REQUIRE_FALSE( context->workspaceService().openStore( dbB ) );

  {
    QFile projectBTemplate( pathB );
    REQUIRE( projectBTemplate.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    projectBTemplate.close();
  }
  REQUIRE( project->write( pathB ) );
  QFile savedB( pathB );
  REQUIRE( savedB.open( QIODevice::ReadOnly ) );
  const QString xmlB = QString::fromUtf8( savedB.readAll() );
  savedB.close();
  REQUIRE_FALSE( xmlB.contains( QStringLiteral( "project-a-ds" ) ) );
  REQUIRE_FALSE( xmlB.contains( QStringLiteral( "version=\"3\"" ) ) );

  context->workspaceService().closeStore();
}

TEST_CASE( "Newer-schema store surfaces restore failure and the document wins on save",
           "[project][workspace_v3][fault][issue752]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "readonly.qgs" ) );
  const QString dbPath = dir.filePath( QStringLiteral( "readonly.governance.db" ) );

  {
    sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
        sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created.operator bool() );
    std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
    REQUIRE( context->openWorkspaceStore( projectPath ) );
    REQUIRE( !context->workspaceService()
                  .createDataset( QStringLiteral( "ro-ds" ), DatasetKind::Training )
                  .isNull() );
    sicnu::app::DataProjectSerializer serializer;
    QObject signalReceiver;
    QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                      [&]( QDomDocument &document ) {
                        static_cast<void>( serializer.write( document, *context ) );
                      } );
    REQUIRE( project->write( projectPath ) );
    context->workspaceService().closeStore();
  }

  // Simulate a future schema: the store opens read-only (forward tolerance).
  {
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw,
                              SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
    char *err = nullptr;
    REQUIRE( sqlite3_exec( raw, "UPDATE gov_meta SET value='2' WHERE key='schema_version'",
                           nullptr, nullptr, &err ) == SQLITE_OK );
    if ( err )
      sqlite3_free( err );
    sqlite3_close( raw );
  }

  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
  REQUIRE( context->openWorkspaceStore( projectPath ) );
  REQUIRE( context->workspaceService().store().isReadOnly() );

  sicnu::app::DataProjectSerializer serializer;
  QVector<sicnu::data::Diagnostic> readDiagnostics;
  bool writeSucceeded = false;
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::readProject, &signalReceiver,
                    [&]( const QDomDocument &document ) {
                      const sicnu::data::Result<void> result =
                          serializer.read( document, *project, *context );
                      readDiagnostics = result.diagnostics();
                    } );
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      writeSucceeded = static_cast<bool>( serializer.write( document, *context ) );
                    } );
  REQUIRE( project->read( projectPath ) );

  // The read-only condition and the failed restore are explicit diagnostics,
  // never a silent no-op.
  bool readOnlyReported = false;
  bool restoreFailedReported = false;
  for ( const sicnu::data::Diagnostic &d : readDiagnostics )
  {
    readOnlyReported |= d.code == QLatin1String( "workspace.store_read_only" );
    restoreFailedReported |= d.code == QLatin1String( "workspace.restore_failed" );
  }
  REQUIRE( readOnlyReported );
  REQUIRE( restoreFailedReported );

  // Save: the document state (not the stale store rows) wins on the next save.
  REQUIRE( project->write( projectPath ) );
  REQUIRE( writeSucceeded );
  QFile saved( projectPath );
  REQUIRE( saved.open( QIODevice::ReadOnly ) );
  const QString xml = QString::fromUtf8( saved.readAll() );
  saved.close();
  REQUIRE( xml.contains( QStringLiteral( "version=\"3\"" ) ) );
  REQUIRE( xml.contains( QStringLiteral( "ro-ds" ) ) );

  context->workspaceService().closeStore();
}

TEST_CASE( "Save with an unusable store falls back to the cached document loudly (#746)",
           "[project][workspace_v3][fault][issue746][stale_warning]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "corruptopen.qgs" ) );
  const QString dbPath = dir.filePath( QStringLiteral( "corruptopen.governance.db" ) );

  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
  REQUIRE( context->openWorkspaceStore( projectPath ) );
  REQUIRE( !context->workspaceService()
                .createDataset( QStringLiteral( "corruptopen-ds" ), DatasetKind::Training )
                .isNull() );

  sicnu::app::DataProjectSerializer serializer;
  sicnu::data::Result<void> writeResult = sicnu::data::Result<void>::success();
  QObject signalReceiver;
  QObject::connect( project, &QgsProject::writeProject, &signalReceiver,
                    [&]( QDomDocument &document ) {
                      writeResult = serializer.write( document, *context );
                    } );

  // First save establishes the v3 document cache.
  REQUIRE( project->write( projectPath ) );

  // The store becomes unusable mid-session: closing it simulates the state
  // after a fatal store failure (the corrupt-while-open variant is
  // best-effort — SQLite's page cache serves small DBs from cache, so its
  // probe cannot fail deterministically). The save must fall back to the
  // cached document AND say so — never a silent success over a dead store.
  context->workspaceService().closeStore();

  REQUIRE( project->write( projectPath ) );
  REQUIRE( writeResult.operator bool() );
  bool staleWarning = false;
  for ( const sicnu::data::Diagnostic &d : writeResult.diagnostics() )
    staleWarning |= d.code == QLatin1String( "workspace.stale_document_repersisted" );
  REQUIRE( staleWarning );

  QFile saved( projectPath );
  REQUIRE( saved.open( QIODevice::ReadOnly ) );
  const QString xml = QString::fromUtf8( saved.readAll() );
  saved.close();
  REQUIRE( xml.contains( QStringLiteral( "version=\"3\"" ) ) );
  REQUIRE( xml.contains( QStringLiteral( "corruptopen-ds" ) ) );

  context->workspaceService().closeStore();
}
