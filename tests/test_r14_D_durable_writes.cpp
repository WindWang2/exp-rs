// tests/test_r14_D_durable_writes.cpp — R14-D WP-B: the shared crash-or-valid
// file publish authority (src/app/durable_file_write.h) and the call sites
// routed through it.
//
// Coverage map (spec letters):
//   (a) fresh-file write: content + 0644-style perms + no litter
//   (b) destination permissions preserved across overwrite (T-d)
//   (c) symlink target followed: referent updated, link stays a link (T-e)
//   (d) symlink loop: honest failure, nothing written
//   (e) read-only parent dir: honest failure, target absent
//   (f) target is a directory: honest failure
//   (g) failed overwrite leaves the prior content intact
//   (h) no temp-file litter after success AND after failure
// Plus integration through public entry points:
//   - rsSaveGcpPointsFile / rsLoadGcpPointsFile (georeferencer .points codec)
//   - PipelineEditorDock::writePipelineDefinitionFile (static; re-asserts the
//     #1501 guarantees from tests/test_workflow_pipeline_ui.cpp:698-863 —
//     which stay in place — and adds the T-d/T-e assertions)
//
// Residual (not cheaply headless-testable, verified by code reading):
//   - PythonScriptEditor::saveScript (src/app/widgets/python_script_editor.cpp):
//     compiled only under SICNU_EMBED_PYTHON (OFF by default) and its public
//     entry needs the save dialog when m_currentFile is unset. Code reading:
//     saveScript builds script().toUtf8() (+ CRLF on Windows) and publishes
//     via sicnu::writeBytesDurably; on failure it warns and does NOT touch
//     m_currentFile nor emit statusMessage.
//   - RoiStatisticsWidget export lambda + RsAccuracyPanel::exportCsv: modal
//     QFileDialog::getSaveFileName stands between the public entry and the
//     write. Code reading: both serialize into a QByteArray and publish via
//     sicnu::writeBytesDurably; success feedback only on ok.
//   - A mid-write ENOSPC injection is not simulated; the short-write branch is
//     covered by construction (every write is size-checked in the helper) and
//     by the injectable failure paths below.
#include <catch2/catch_test_macros.hpp>

#include "durable_file_write.h"

#include "app/workflow/pipeline_editor_dock.h"
#include "qgsgcplist.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsgcppoint.h"
#include "qgspointxy.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>

#if defined( Q_OS_UNIX )
#include <unistd.h>
#endif

using sicnu::writeBytesDurably;
using namespace sicnu::workflow::gui;

namespace {

// Root can write into 0500 directories, which would flip the read-only-dir
// expectations; degrade gracefully instead of failing honestly-configured CI.
bool readOnlyDirEnforced()
{
#if defined( Q_OS_UNIX )
  return ::geteuid() != 0;
#else
  return true;
#endif
}

QByteArray readFileBytes( const QString &path )
{
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return QByteArray();
  return f.readAll();
}

bool writeFileBytes( const QString &path, const QByteArray &bytes )
{
  QFile f( path );
  if ( !f.open( QIODevice::WriteOnly ) )
    return false;
  return f.write( bytes ) == bytes.size() && f.flush();
}

// Qt never names a publish temporary "*.tmp": QSaveFile writes through
// QTemporaryFileEngine with the TARGET path as the name template, so a named
// temporary left behind is "<target>.<6 random chars>" — QTemporaryFileName
// appends Qt's ".XXXXXX" mask (Linux first tries an unnamed O_TMPFILE), and no
// ".tmp" literal exists anywhere in libQt6Core (verified against the installed
// Qt 6.12). A "*.tmp" wildcard can therefore never match a leak, which would
// make the (h) assertion vacuous. Assert the guarantee pattern-agnostically
// instead: the only files left in dirPath are @p expectedFiles, so any leftover
// name — temp or otherwise — is caught.
QStringList leakedTmpFiles( const QString &dirPath, const QStringList &expectedFiles = QStringList() )
{
  const QStringList files = QDir( dirPath ).entryList( QDir::Files | QDir::Hidden );
  QStringList leaked;
  for ( const QString &file : files )
  {
    if ( !expectedFiles.contains( file ) )
      leaked.append( file );
  }
  return leaked;
}

int fake_argc = 1;
char fake_argv0[] = "test_r14_D_durable_writes";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  if ( !qApp )
    new QApplication( fake_argc, fake_argv );
  return qApp;
}

} // namespace

// ---------------------------------------------------------------------------
// Helper unit tests
// ---------------------------------------------------------------------------

TEST_CASE( "durable write: fresh file gets content and 0644-style perms (a)",
           "[r14D][durable][wpb]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString target = tempDir.filePath( QStringLiteral( "fresh.dat" ) );

  const QByteArray payload = QByteArrayLiteral( "line one\nline two\n" );
  const sicnu::DurableWriteResult result = writeBytesDurably( target, payload );

  REQUIRE( result.ok );
  CHECK( result.error.isEmpty() );
  CHECK( readFileBytes( target ) == payload );

  // 0644-equivalent: owner read/write, group/other read (documented default —
  // deliberately NOT umask-relative, so this is assertable everywhere).
  const QFileDevice::Permissions perms = QFileInfo( target ).permissions();
  CHECK( perms.testFlag( QFileDevice::ReadOwner ) );
  CHECK( perms.testFlag( QFileDevice::WriteOwner ) );
  CHECK( perms.testFlag( QFileDevice::ReadGroup ) );
  CHECK( perms.testFlag( QFileDevice::ReadOther ) );

  // (h) no temp litter after success — exact listing, pattern-agnostic.
  const QStringList files = QDir( tempDir.path() ).entryList( QDir::Files );
  REQUIRE( files.size() == 1 );
  CHECK( files.first() == QStringLiteral( "fresh.dat" ) );
}

TEST_CASE( "durable write: destination permissions are preserved across overwrite (b)",
           "[r14D][durable][wpb][issue1487]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString target = tempDir.filePath( QStringLiteral( "keep_perms.dat" ) );

  // Seed via the default (0644-style) lane, then tighten to 0600.
  REQUIRE( writeBytesDurably( target, QByteArrayLiteral( "v1" ) ).ok );
  REQUIRE( QFile::setPermissions( target, QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );

  const sicnu::DurableWriteResult result =
    writeBytesDurably( target, QByteArrayLiteral( "version-two-payload" ) );
  REQUIRE( result.ok );
  CHECK( readFileBytes( target ) == QByteArrayLiteral( "version-two-payload" ) );

  // T-d regression: the publish must not replace the tightened mode. (The old
  // pipeline lane went the other way — 0644 destination clobbered to 0600 —
  // either direction of drift is a violation; here 0600 must SURVIVE.)
  const QFileDevice::Permissions perms = QFileInfo( target ).permissions();
  CHECK( perms.testFlag( QFileDevice::ReadOwner ) );
  CHECK( perms.testFlag( QFileDevice::WriteOwner ) );
  CHECK_FALSE( perms.testFlag( QFileDevice::ReadGroup ) );
  CHECK_FALSE( perms.testFlag( QFileDevice::ReadOther ) );
}

#if defined( Q_OS_UNIX )
TEST_CASE( "durable write: symlink target referent is updated, link stays a link (c)",
           "[r14D][durable][wpb][issue1487]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString real = tempDir.filePath( QStringLiteral( "real.points" ) );
  const QString link = tempDir.filePath( QStringLiteral( "alias.points" ) );
  REQUIRE( writeFileBytes( real, QByteArrayLiteral( "OLD" ) ) );
  REQUIRE( QFile::link( real, link ) ); // alias.points -> real.points

  const sicnu::DurableWriteResult result =
    writeBytesDurably( link, QByteArrayLiteral( "NEW-BYTES" ) );
  REQUIRE( result.ok );

  // T-e: the referent carries the new bytes; the link is still a link.
  CHECK( readFileBytes( real ) == QByteArrayLiteral( "NEW-BYTES" ) );
  CHECK( QFileInfo( link ).isSymbolicLink() );
  CHECK( readFileBytes( link ) == QByteArrayLiteral( "NEW-BYTES" ) );

  // A DANGLING symlink publishes to its referent (write-follows-link).
  const QString dangling = tempDir.filePath( QStringLiteral( "dangling.points" ) );
  const QString referent = tempDir.filePath( QStringLiteral( "created.points" ) );
  REQUIRE( QFile::link( referent, dangling ) );
  REQUIRE( writeBytesDurably( dangling, QByteArrayLiteral( "created" ) ).ok );
  CHECK( QFileInfo( referent ).isFile() );
  CHECK( readFileBytes( referent ) == QByteArrayLiteral( "created" ) );
  CHECK( QFileInfo( dangling ).isSymbolicLink() );

  // (h) no litter: exactly the four expected names.
  const QStringList files = QDir( tempDir.path() ).entryList( QDir::Files );
  CHECK( files.size() == 4 );
}

TEST_CASE( "durable write: symlink loop fails honestly with nothing written (d)",
           "[r14D][durable][wpb]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString a = tempDir.filePath( QStringLiteral( "loop_a" ) );
  const QString b = tempDir.filePath( QStringLiteral( "loop_b" ) );
  REQUIRE( QFile::link( b, a ) ); // a -> b
  REQUIRE( QFile::link( a, b ) ); // b -> a

  const sicnu::DurableWriteResult result =
    writeBytesDurably( a, QByteArrayLiteral( "never lands" ) );
  CHECK_FALSE( result.ok );
  CHECK_FALSE( result.error.isEmpty() );
  CHECK( result.error.contains( QStringLiteral( "loop_a" ) ) );

  // Nothing was written anywhere: the loop never resolves to a regular file,
  // and no stray temp files remain.
  CHECK_FALSE( QFileInfo( a ).isFile() );
  CHECK_FALSE( QFileInfo( b ).isFile() );
  // Dangling symlinks are not QDir::Files on POSIX; count every entry so the
  // check is really "the two links and no stray temp files".
  const QStringList files =
    QDir( tempDir.path() ).entryList( QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System );
  CHECK( files.size() == 2 );
}
#endif // Q_OS_UNIX

TEST_CASE( "durable write: read-only parent dir fails honestly, target absent (e)",
           "[r14D][durable][wpb]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString roDir = tempDir.filePath( QStringLiteral( "ro_dir" ) );
  REQUIRE( QDir().mkdir( roDir ) );
  QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner );

  const QString target = QDir( roDir ).filePath( QStringLiteral( "blocked.dat" ) );
  const sicnu::DurableWriteResult result =
    writeBytesDurably( target, QByteArrayLiteral( "nope" ) );

  if ( readOnlyDirEnforced() )
  {
    CHECK_FALSE( result.ok );
    CHECK_FALSE( result.error.isEmpty() );
    CHECK( result.error.contains( QStringLiteral( "Cannot create temporary file" ) ) );
    CHECK_FALSE( QFileInfo::exists( target ) );
    // (h) no litter in the writable ancestor either.
    CHECK( leakedTmpFiles( tempDir.path() ).isEmpty() );
  }
  else
  {
    // Running as root: the write legitimately succeeds; the oracle must hold.
    CHECK( result.ok );
    CHECK( readFileBytes( target ) == QByteArrayLiteral( "nope" ) );
  }

  QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner );
}

TEST_CASE( "durable write: directory target fails honestly (f)",
           "[r14D][durable][wpb]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString dirTarget = tempDir.filePath( QStringLiteral( "target_is_dir" ) );
  REQUIRE( QDir().mkdir( dirTarget ) );
  // Non-empty directory, like the pipeline error-path test.
  REQUIRE( writeFileBytes( QDir( dirTarget ).filePath( QStringLiteral( "dummy.txt" ) ),
                           QByteArrayLiteral( "hello" ) ) );

  const sicnu::DurableWriteResult result =
    writeBytesDurably( dirTarget, QByteArrayLiteral( "data" ) );
  CHECK_FALSE( result.ok );
  CHECK( result.error.contains( QStringLiteral( "Target is a directory" ) ) );
  // The directory and its content are untouched; no temp litter next to it.
  CHECK( QFileInfo( dirTarget ).isDir() );
  CHECK( readFileBytes( QDir( dirTarget ).filePath( QStringLiteral( "dummy.txt" ) ) )
         == QByteArrayLiteral( "hello" ) );
  CHECK( leakedTmpFiles( tempDir.path() ).isEmpty() );
}

TEST_CASE( "durable write: failed overwrite leaves prior content intact (g)",
           "[r14D][durable][wpb]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString roDir = tempDir.filePath( QStringLiteral( "ro_keep" ) );
  REQUIRE( QDir().mkdir( roDir ) );
  const QString target = QDir( roDir ).filePath( QStringLiteral( "precious.dat" ) );
  REQUIRE( writeFileBytes( target, QByteArrayLiteral( "PRECIOUS-OLD-CONTENT" ) ) );
  QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner );

  const sicnu::DurableWriteResult result =
    writeBytesDurably( target, QByteArrayLiteral( "partial-new-content" ) );

  if ( readOnlyDirEnforced() )
  {
    CHECK_FALSE( result.ok );
    CHECK_FALSE( result.error.isEmpty() );
    // Old-or-new oracle: the previous complete content survives verbatim.
    CHECK( readFileBytes( target ) == QByteArrayLiteral( "PRECIOUS-OLD-CONTENT" ) );
  }
  else
  {
    CHECK( result.ok );
    CHECK( readFileBytes( target ) == QByteArrayLiteral( "partial-new-content" ) );
  }

  QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner );
}

TEST_CASE( "durable write: empty path and missing parent dir fail honestly",
           "[r14D][durable][wpb]" )
{
  const sicnu::DurableWriteResult empty =
    writeBytesDurably( QString(), QByteArrayLiteral( "x" ) );
  CHECK_FALSE( empty.ok );
  CHECK_FALSE( empty.error.isEmpty() );

  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const sicnu::DurableWriteResult missing =
    writeBytesDurably( tempDir.filePath( QStringLiteral( "no_such_sub/f.dat" ) ),
                       QByteArrayLiteral( "x" ) );
  CHECK_FALSE( missing.ok );
  CHECK( missing.error.contains( QStringLiteral( "Directory does not exist" ) ) );
}

// ---------------------------------------------------------------------------
// Integration: GCP .points codec (APP-4)
// ---------------------------------------------------------------------------

TEST_CASE( "rsSaveGcpPointsFile publishes durably and reports failure (APP-4)",
           "[r14D][durable][georef][points]" )
{
  QgsCoordinateReferenceSystem crs( QStringLiteral( "EPSG:32650" ) );
  QVector<QgsGcpPoint> points;
  points.append( QgsGcpPoint( QgsPointXY( 100, 200 ), QgsPointXY( 400000, 4280000 ), crs, true ) );
  points.append( QgsGcpPoint( QgsPointXY( 300, 400 ), QgsPointXY( 401000, 4281000 ), crs, false ) );

  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  SECTION( "round-trip survives durable overwrite" )
  {
    const QString path = tempDir.filePath( QStringLiteral( "gcps.points" ) );
    REQUIRE( rsSaveGcpPointsFile( path, points ) );

    QVector<QgsGcpPoint> loaded;
    REQUIRE( rsLoadGcpPointsFile( path, crs, loaded ) );
    REQUIRE( loaded.size() == 2 );
    CHECK( loaded.at( 0 ).destinationPoint().x() == 400000.0 );
    CHECK( loaded.at( 0 ).destinationPoint().y() == 4280000.0 );
    CHECK( loaded.at( 0 ).isEnabled() );
    CHECK_FALSE( loaded.at( 1 ).isEnabled() );
    CHECK( loaded.at( 0 ).destinationPointCrs().authid() == QString( "EPSG:32650" ) );

    // Overwrite through the durable lane keeps the codec bytes loadable.
    points.append( QgsGcpPoint( QgsPointXY( 5, 6 ), QgsPointXY( 7, 8 ), crs, true ) );
    REQUIRE( rsSaveGcpPointsFile( path, points ) );
    QVector<QgsGcpPoint> reloaded;
    REQUIRE( rsLoadGcpPointsFile( path, crs, reloaded ) );
    CHECK( reloaded.size() == 3 );

    // No temp litter beside the .points file.
    const QStringList expectedFiles = QStringList() << QStringLiteral( "gcps.points" );
    CHECK( leakedTmpFiles( tempDir.path(), expectedFiles ).isEmpty() );
  }

  SECTION( "destination permissions survive a .points save (T-d at the call site)" )
  {
    const QString path = tempDir.filePath( QStringLiteral( "perms.points" ) );
    REQUIRE( rsSaveGcpPointsFile( path, points ) );
    REQUIRE( QFile::setPermissions( path, QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );
    REQUIRE( rsSaveGcpPointsFile( path, points ) );

    const QFileDevice::Permissions perms = QFileInfo( path ).permissions();
    CHECK( perms.testFlag( QFileDevice::ReadOwner ) );
    CHECK( perms.testFlag( QFileDevice::WriteOwner ) );
    CHECK_FALSE( perms.testFlag( QFileDevice::ReadGroup ) );
    CHECK_FALSE( perms.testFlag( QFileDevice::ReadOther ) );
  }

#if defined( Q_OS_UNIX )
  SECTION( "symlinked .points updates the referent (T-e at the call site)" )
  {
    const QString real = tempDir.filePath( QStringLiteral( "real.points" ) );
    const QString link = tempDir.filePath( QStringLiteral( "alias.points" ) );
    REQUIRE( rsSaveGcpPointsFile( real, points ) );
    REQUIRE( QFile::link( real, link ) );

    REQUIRE( rsSaveGcpPointsFile( link, points ) );
    CHECK( QFileInfo( link ).isSymbolicLink() );

    QVector<QgsGcpPoint> viaLink;
    REQUIRE( rsLoadGcpPointsFile( link, crs, viaLink ) );
    CHECK( viaLink.size() == 2 );
  }
#endif

  SECTION( "unwritable targets return false (call-site honesty)" )
  {
    // Target is a directory.
    const QString dirTarget = tempDir.filePath( QStringLiteral( "dir_target" ) );
    REQUIRE( QDir().mkdir( dirTarget ) );
    CHECK_FALSE( rsSaveGcpPointsFile( dirTarget, points ) );

    // Missing parent directory.
    CHECK_FALSE( rsSaveGcpPointsFile(
      tempDir.filePath( QStringLiteral( "missing_sub/x.points" ) ), points ) );

    // Read-only parent directory (restored afterwards).
    if ( readOnlyDirEnforced() )
    {
      const QString roDir = tempDir.filePath( QStringLiteral( "ro_gcp" ) );
      REQUIRE( QDir().mkdir( roDir ) );
      QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner );
      CHECK_FALSE( rsSaveGcpPointsFile( QDir( roDir ).filePath(
        QStringLiteral( "blocked.points" ) ), points ) );
      QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner );
    }
  }
}

// ---------------------------------------------------------------------------
// Integration: pipeline editor definition write (#1487/#1501 lane, now on the
// shared authority; the original suite stays in test_workflow_pipeline_ui.cpp)
// ---------------------------------------------------------------------------

TEST_CASE( "PipelineEditorDock write: permission preservation, symlink following and error-path parity (WP-D1)",
           "[r14D][durable][pipeline][save][issue1487]" )
{
  ensureApp();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  Json::Value root;
  root["id"] = "r14d_pipe";
  root["title"] = "R14-D Durable";
  root["steps"] = Json::Value( Json::arrayValue );

  SECTION( "save keeps destination permissions (T-d)" )
  {
    const QString path = tempDir.filePath( QStringLiteral( "perm_pipeline.json" ) );
    QString error;
    REQUIRE( PipelineEditorDock::writePipelineDefinitionFile( path, root, &error ) );
    CHECK( error.isEmpty() );

    REQUIRE( QFile::setPermissions( path, QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );
    root["title"] = "R14-D Durable v2";
    REQUIRE( PipelineEditorDock::writePipelineDefinitionFile( path, root, &error ) );

    const QFileDevice::Permissions perms = QFileInfo( path ).permissions();
    CHECK( perms.testFlag( QFileDevice::ReadOwner ) );
    CHECK( perms.testFlag( QFileDevice::WriteOwner ) );
    CHECK_FALSE( perms.testFlag( QFileDevice::ReadGroup ) );
    CHECK_FALSE( perms.testFlag( QFileDevice::ReadOther ) );

    Json::Value reloaded;
    REQUIRE( PipelineEditorDock::readPipelineDefinitionFile( path, reloaded, &error ) );
    CHECK( reloaded["title"].asString() == "R14-D Durable v2" );
  }

#if defined( Q_OS_UNIX )
  SECTION( "save through a symlink updates the referent (T-e)" )
  {
    const QString real = tempDir.filePath( QStringLiteral( "real_pipeline.json" ) );
    const QString link = tempDir.filePath( QStringLiteral( "alias_pipeline.json" ) );
    QString error;
    REQUIRE( PipelineEditorDock::writePipelineDefinitionFile( real, root, &error ) );
    REQUIRE( QFile::link( real, link ) );

    root["title"] = "Via Symlink";
    REQUIRE( PipelineEditorDock::writePipelineDefinitionFile( link, root, &error ) );

    CHECK( QFileInfo( link ).isSymbolicLink() );
    Json::Value reloaded;
    REQUIRE( PipelineEditorDock::readPipelineDefinitionFile( real, reloaded, &error ) );
    CHECK( reloaded["title"].asString() == "Via Symlink" );
  }
#endif

  SECTION( "error-path message parity with the #1501 suite" )
  {
    QString error;

    // Missing parent directory.
    CHECK_FALSE( PipelineEditorDock::writePipelineDefinitionFile(
      tempDir.filePath( QStringLiteral( "nosuchdir/pipe.json" ) ), root, &error ) );
    CHECK( error.contains( QStringLiteral( "Directory does not exist" ) ) );

    // Target is a directory.
    const QString dirTarget = tempDir.filePath( QStringLiteral( "pipeline_dir" ) );
    REQUIRE( QDir().mkdir( dirTarget ) );
    CHECK_FALSE( PipelineEditorDock::writePipelineDefinitionFile( dirTarget, root, &error ) );
    CHECK( error.contains( QStringLiteral( "Target is a directory" ) ) );

    // Read-only parent directory (temp creation fails).
    if ( readOnlyDirEnforced() )
    {
      const QString roDir = tempDir.filePath( QStringLiteral( "ro_pipeline" ) );
      REQUIRE( QDir().mkdir( roDir ) );
      QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner );
      CHECK_FALSE( PipelineEditorDock::writePipelineDefinitionFile(
        QDir( roDir ).filePath( QStringLiteral( "blocked.json" ) ), root, &error ) );
      CHECK( error.contains( QStringLiteral( "Cannot create temporary file" ) ) );
      QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner );
    }

    // Empty path.
    CHECK_FALSE( PipelineEditorDock::writePipelineDefinitionFile( QString(), root, &error ) );
    CHECK_FALSE( error.isEmpty() );

    // No temp litter anywhere in the fixture tree (mirrors the leak check).
    CHECK( leakedTmpFiles( tempDir.path() ).isEmpty() );
  }
}
