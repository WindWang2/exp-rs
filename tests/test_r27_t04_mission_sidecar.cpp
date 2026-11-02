// tests/test_r27_t04_mission_sidecar.cpp — R27-T04 (#1795)
//
// Contract under test: when the mission sidecar write fails while a prior
// .mission.json exists, the stale sidecar must NOT be destroyed — the
// original state stays recoverable (renamed to a dated .failed-*.bak) and
// the restore path still resolves the (fresh) XML channel instead of a
// shadowing stale sidecar. On HEAD the stale file was QFile::remove'd
// before the XML channel was persisted: a later project-file failure left
// BOTH channels without the mission state.
//
// Failure injection: a >32 MiB mission payload is refused by the sidecar
// write authority (maxBytes gate fires before the target is touched), so
// the prior sidecar stays on disk exactly as a crashed/corrupt write would
// leave it.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context_store.h"

#include <QApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_mission_sidecar";
  char *fake_argv[] = { fake_argv0, nullptr };

  QCoreApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
      return new QCoreApplication( fake_argc, fake_argv );
    return QCoreApplication::instance();
  }

  QDomDocument emptyProjectDoc()
  {
    QDomDocument doc;
    doc.appendChild( doc.createElement( QStringLiteral( "qgis" ) ) );
    return doc;
  }
} // namespace

TEST_CASE( "Failed sidecar write preserves the original as a backup (#1795)",
           "[mission][sidecar][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString projectPath = tmp.filePath( QStringLiteral( "mission.qgs" ) );
  {
    QFile p( projectPath );
    REQUIRE( p.open( QIODevice::WriteOnly ) );
    p.write( "placeholder" );
  }
  const QString sidecar = sicnu::app::missionSidecarPathForProject( projectPath );

  // A healthy prior sidecar exists (mission "alpha").
  sicnu::app::MissionContext prior;
  prior.missionName = QStringLiteral( "alpha" );
  prior.projectRef = projectPath;
  QString priorErr;
  REQUIRE( sicnu::app::saveMissionContextToSidecar( projectPath, prior, &priorErr ) );
  REQUIRE( QFileInfo::exists( sidecar ) );
  QFile sideFile( sidecar );
  REQUIRE( sideFile.open( QIODevice::ReadOnly ) );
  const QByteArray priorBytes = sideFile.readAll();
  sideFile.close();

  // The doomed save: the sidecar write will be refused (oversize payload)
  // while the XML channel still succeeds.
  sicnu::app::MissionContext doomed;
  doomed.missionName = QStringLiteral( "beta" );
  doomed.projectRef = projectPath;
  doomed.metadata.insert(
    QStringLiteral( "blob" ),
    QString( 33u * 1024u * 1024u, QChar( u'x' ) ) ); // > kDefaultMaxSidecarBytes

  QDomDocument doc = emptyProjectDoc();
  QString err;
  const bool persisted =
    sicnu::app::persistMissionContextWithProject( projectPath, doc, doomed, &err );
  // XML channel succeeded, so persistence reports success with problems.
  REQUIRE( persisted );
  REQUIRE( !err.isEmpty() );

  // The stale sidecar no longer shadows the XML channel…
  CHECK_FALSE( QFileInfo::exists( sidecar ) );

  // …but its bytes are preserved in a dated backup (#1795; RED on HEAD:
  // the original was deleted with no recoverable copy).
  const QDir dir( tmp.path() );
  const QStringList backups =
    dir.entryList( QStringList() << QFileInfo( sidecar ).fileName() + QStringLiteral( ".failed-*.bak" ) );
  REQUIRE( backups.size() == 1 );
  QFile backup( dir.filePath( backups.first() ) );
  REQUIRE( backup.open( QIODevice::ReadOnly ) );
  CHECK( backup.readAll() == priorBytes );
  backup.close();

  // Restore prefers the fresh XML channel (mission "beta"), never a backup.
  sicnu::app::MissionContext restored;
  QString restoreErr;
  REQUIRE( sicnu::app::readMissionContextFromProjectXml( doc, restored, &restoreErr ) );
  CHECK( restored.missionName == QLatin1String( "beta" ) );
}
