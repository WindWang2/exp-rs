// tests/test_r27_t04_project_downgrade.cpp — R27-T04 (#1747)
//
// Contract under test: when the workspace downgrade guard would refuse to
// serialize the SICNU extension (v3 seen, governance store unavailable, no
// cached document), a save must be REFUSED BEFORE QgsProject::write() —
// the on-disk project file (with its v3 extension block) stays
// byte-identical. On HEAD the refusal fired inside QgsProject::writeProject,
// which cannot abort the write: the file was still overwritten with NO
// extension block and the governed state was destroyed.
//
// Part 1 pins the preflight API; part 2 drives the real shell save paths
// (saveProject / saveProjectAsTo) offscreen against a corrupted-v3 project.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "support/qt_lifecycle.h"

#include "main_window.h"
#include "app/data_project_serializer.h"
#include "app/project_context.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <qgsmapcanvas.h>
#include <qgsproject.h>

#include <gdal.h>

#include <vector>

#include "data/data_manager.h"
#include "data/governance/governance_types.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_project_downgrade";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      qputenv( "QT_QPA_PLATFORM", "offscreen" );
      QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
      QCoreApplication::setApplicationName( QStringLiteral( "r27-t04-downgrade" ) );
      QSettings::setDefaultFormat( QSettings::IniFormat );
      QApplication *app = new QApplication( fake_argc, fake_argv );
      QSettings().clear();
      return app;
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  void armModalAnswer( QMessageBox::StandardButton button, int budget = 6 )
  {
    if ( budget <= 0 )
      return;
    QTimer::singleShot( 0, ensureApp(), [button, budget] {
      if ( QWidget *modal = QApplication::activeModalWidget() )
      {
        if ( QMessageBox *box = qobject_cast<QMessageBox *>( modal ) )
        {
          if ( QAbstractButton *target = box->button( button ) )
            target->click();
          else
            box->close();
        }
        else
          modal->close();
      }
      armModalAnswer( button, budget - 1 );
    } );
  }

  QString syntheticRaster( const QString &path )
  {
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    GDALSetGeoTransform( ds, gt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( 8, 1.f );
    for ( int row = 0; row < 8; ++row )
      GDALRasterIO( band, GF_Write, 0, row, 8, 1, line.data(), 8, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return {};
  }

  /// Writes a real v3 project (governance store open, one registered asset).
  bool writeV3Project( const QString &projectPath, const QString &rasterPath )
  {
    sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
      sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created.operator bool() );
    std::unique_ptr<sicnu::app::ProjectContext> context = created.take();
    REQUIRE( context->openWorkspaceStore( projectPath ) );

    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = rasterPath;
    const sicnu::data::RegisterResult registered =
      context->dataManager().registerSource( { source } );
    REQUIRE( !registered.assetId.isNull() );

    sicnu::app::DataProjectSerializer serializer;
    bool writeSucceeded = false;
    QObject receiver;
    QObject::connect( QgsProject::instance(), &QgsProject::writeProject, &receiver,
                      [&]( QDomDocument &document ) {
                        writeSucceeded = static_cast<bool>( serializer.write( document, *context ) );
                      } );
    const bool ok = QgsProject::instance()->write( projectPath ) && writeSucceeded;
    QObject::disconnect( QgsProject::instance(), &QgsProject::writeProject, &receiver, nullptr );
    context->workspaceService().closeStore();
    QgsProject::instance()->clear();
    return ok;
  }

  /// Break the governed document inside the v3 extension block so a later
  /// read marks the session v3-seen but cannot cache the governed state
  /// (the exact refusal precondition), and remove the sidecar store.
  void corruptV3Block( const QString &projectPath )
  {
    QFile file( projectPath );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    QString xml = QString::fromUtf8( file.readAll() );
    file.close();
    const qsizetype docBegin = xml.indexOf( QStringLiteral( "<document>" ) );
    const qsizetype docEnd = xml.indexOf( QStringLiteral( "</document>" ), docBegin );
    REQUIRE( docBegin > 0 );
    REQUIRE( docEnd > docBegin );
    xml.replace( docBegin + 10, docEnd - docBegin - 10, QStringLiteral( "{not-json" ) );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( xml.toUtf8() );
    file.close();
    // Governance store gone too: nothing can re-serialize the governed state
    // (same sidecar naming as WorkspaceService::defaultStorePathFor).
    const QString sidecar =
      QFileInfo( projectPath ).dir().filePath(
        QFileInfo( projectPath ).completeBaseName() + QStringLiteral( ".governance.db" ) );
    if ( QFileInfo::exists( sidecar ) )
      QFile::remove( sidecar );
  }

  QByteArray readFileBytes( const QString &path )
  {
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
  }
} // namespace

TEST_CASE( "DataProjectSerializer::canWrite pins the downgrade refusal condition (#1747)",
           "[project][downgrade][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString projectPath = tmp.filePath( QStringLiteral( "unit.qgs" ) );
  const QString rasterPath = tmp.filePath( QStringLiteral( "unit.tif" ) );
  REQUIRE( syntheticRaster( rasterPath ).isEmpty() );
  REQUIRE( writeV3Project( projectPath, rasterPath ) );
  // The governed state made it into the file.
  REQUIRE( QString::fromUtf8( readFileBytes( projectPath ) )
             .contains( QStringLiteral( "version=\"3\"" ) ) );

  sicnu::data::Result<std::unique_ptr<sicnu::app::ProjectContext>> created =
    sicnu::app::ProjectContext::createHeadless();
  REQUIRE( created.operator bool() );
  std::unique_ptr<sicnu::app::ProjectContext> context = created.take();

  // Fresh context: nothing v3 seen yet — a plain v1 write is fine.
  CHECK( sicnu::app::DataProjectSerializer::canWrite( *context ).operator bool() );

  // The downgrade state: v3 seen, store unavailable, no cached document.
  context->workspaceService().markV3Seen();
  const sicnu::data::Result<void> refused =
    sicnu::app::DataProjectSerializer::canWrite( *context );
  REQUIRE( !refused );
  REQUIRE( refused.diagnostics().size() >= 1 );
  CHECK( refused.diagnostics().constFirst().code ==
         QStringLiteral( "workspace.downgrade_refused" ) );

  // A live store re-enables the save.
  REQUIRE( context->openWorkspaceStore( projectPath ) );
  CHECK( sicnu::app::DataProjectSerializer::canWrite( *context ).operator bool() );
  context->workspaceService().closeStore();
}

TEST_CASE( "Downgrade refusal leaves the on-disk v3 project byte-identical (#1747)",
           "[project][downgrade][shell][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString projectPath = tmp.filePath( QStringLiteral( "guarded.qgs" ) );
  const QString rasterPath = tmp.filePath( QStringLiteral( "guarded.tif" ) );
  REQUIRE( syntheticRaster( rasterPath ).isEmpty() );
  REQUIRE( writeV3Project( projectPath, rasterPath ) );
  corruptV3Block( projectPath );
  const QByteArray bytesBefore = readFileBytes( projectPath );

  QgisDesktopWindow window;
  armModalAnswer( QMessageBox::Ok );
  // Open: the v3 marker pins the session; the unparseable block leaves no
  // cache; the store sidecar is gone — the save-refusal precondition.
  REQUIRE( window.openProjectFrom( projectPath ) );
  QApplication::processEvents();

  // --- saveProject must refuse without touching the file ---------------
  armModalAnswer( QMessageBox::Ok );
  window.saveProject();
  QApplication::processEvents( QEventLoop::AllEvents, 200 );

  CHECK( readFileBytes( projectPath ) == bytesBefore );
  CHECK( window.statusBar()->currentMessage().contains( QStringLiteral( "refused" ), Qt::CaseInsensitive ) );

  // --- saveProjectAsTo refuses too, writes no new file ------------------
  const QString asPath = tmp.filePath( QStringLiteral( "never.qgs" ) );
  armModalAnswer( QMessageBox::Ok );
  CHECK_FALSE( window.saveProjectAsTo( asPath ) );
  QApplication::processEvents( QEventLoop::AllEvents, 200 );
  CHECK_FALSE( QFileInfo::exists( asPath ) );
  CHECK( readFileBytes( projectPath ) == bytesBefore );
}
