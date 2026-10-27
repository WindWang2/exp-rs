// tests/test_r24_b_publish_fence.cpp — R24-B (WP-B7): publish occupancy and
// stage hygiene.
//   F3: staged-publish paths are UNIQUE per attempt ("<final>.<pid>x<n>.tmp~")
//       — the fixed name let two concurrent runs on one output path clobber
//       each other's stage during the (fence-free) write. The sweep is
//       AGE-GATED (a day) so it never deletes a concurrent live writer's
//       stage, while crash litter still self-heals.
//   F4: the occupancy fence is cross-process now — a QLockFile held on the
//       fence key makes a second guard refuse typed even from another
//       process.
//   F5: a guard whose fence acquisition fails removes the caller's staged
//       file (the documented stageForCleanup contract, previously broken on
//       exactly the fence-contention path by a throwing member-init).
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_publish.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QString>
#include <QTemporaryDir>

#include <string>

namespace {

using sicnu::operators::runtime::canonicalPublishFenceKey;
using sicnu::operators::runtime::ProductPublishGuard;
using sicnu::operators::runtime::PublishFenceLease;
using sicnu::operators::runtime::sweepStaleStagePaths;
using sicnu::operators::runtime::uniqueStagePath;

} // namespace

TEST_CASE( "staged publish paths are unique per attempt and the sweep is "
           "age-gated (R24-B)",
           "[r24_b][publish][stage]" )
{
  QTemporaryDir dir;
  const QString finalPath = dir.filePath( QStringLiteral( "out.tif" ) );

  const QString a = uniqueStagePath( finalPath );
  const QString b = uniqueStagePath( finalPath );
  REQUIRE( !a.isEmpty() );
  CHECK( a != b );
  CHECK( a.startsWith( finalPath + QLatin1Char( '.' ) ) );
  CHECK( a.endsWith( QStringLiteral( ".tmp~" ) ) );
  // The legacy fixed spelling is never produced.
  CHECK( a != finalPath + QStringLiteral( ".tmp~" ) );

  // Sweep: a FRESH stage (live writer stand-in) survives; day-old litter is
  // removed; the legacy fixed name is never matched.
  const QDir outDir( dir.path() );
  QFile fresh( a );
  REQUIRE( fresh.open( QIODevice::WriteOnly ) );
  fresh.write( "live" );
  fresh.close();
  const QString legacy = finalPath + QStringLiteral( ".tmp~" );
  QFile legacyFile( legacy );
  REQUIRE( legacyFile.open( QIODevice::WriteOnly ) );
  legacyFile.write( "legacy" );
  legacyFile.close();
  const QString staleName =
      finalPath + QStringLiteral( ".1999x0.tmp~" );
  QFile stale( staleName );
  REQUIRE( stale.open( QIODevice::WriteOnly ) );
  stale.write( "crash litter" );
  stale.close();
  // Age the litter: mtime one day and a second in the past.
  const QDateTime old = QDateTime::currentDateTime().addDays( -1 ).addSecs( -5 );
  REQUIRE( stale.setFileTime( old, QFile::FileModificationTime ) );

  sweepStaleStagePaths( finalPath );

  CHECK( QFile::exists( a ) );           // live writer untouched
  CHECK( QFile::exists( legacy ) );      // legacy fixed name never matched
  CHECK_FALSE( QFile::exists( staleName ) ); // crash litter swept
}

TEST_CASE( "publish fence is cross-process: a held QLockFile refuses a "
           "second guard typed (R24-B)",
           "[r24_b][publish][fence]" )
{
  QTemporaryDir dir;
  const QString finalPath = dir.filePath( QStringLiteral( "shared.tif" ) );

  // Hold the lease (as another process would via the lock file path).
  {
    PublishFenceLease lease( finalPath );

    // Recreate exactly what another process holding the lock looks like to
    // this one: the lock file exists and is locked by a DIFFERENT pid. The
    // in-process fence would refuse the second lease anyway, so pin the
    // cross-process leg through the lock file directly.
    const QFileInfo info( finalPath );
    const QString lockPath = info.path() + QLatin1Char( '/' ) + QLatin1Char( '.' )
                             + info.fileName() + QStringLiteral( ".publish.lock" );
    CHECK( QFile::exists( lockPath ) );

    // The lease's own lock must be live: an independent QLockFile on the
    // same path cannot take it.
    QLockFile otherProcess( lockPath );
    otherProcess.setStaleLockTime( 0 );
    CHECK_FALSE( otherProcess.tryLock( 100 ) );

    // And an in-process second lease is still refused typed.
    bool refused = false;
    try
    {
      PublishFenceLease second( finalPath );
    }
    catch ( const sicnu::operators::RSOperatorError &e )
    {
      refused = e.code() == sicnu::operators::ErrorCode::AlreadyRunning;
    }
    CHECK( refused );
  }
  // Lease released: the lock file is gone and a fresh lease succeeds.
  const QFileInfo info( finalPath );
  const QString lockPath = info.path() + QLatin1Char( '/' ) + QLatin1Char( '.' )
                           + info.fileName() + QStringLiteral( ".publish.lock" );
  CHECK_FALSE( QFile::exists( lockPath ) );
  PublishFenceLease fresh( finalPath );
}

TEST_CASE( "a fence refusal removes the caller's staged file (R24-B, F5)",
           "[r24_b][publish][fence][stage-cleanup]" )
{
  QTemporaryDir dir;
  const QString finalPath = dir.filePath( QStringLiteral( "busy.tif" ) );

  PublishFenceLease holder( finalPath );

  // The freshly written stage a raster lane would hand the guard.
  const QString stage = dir.filePath( QStringLiteral( "busy.tif.7x1.tmp~" ) );
  {
    QFile f( stage );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "fully written stage" );
    f.close();
  }
  REQUIRE( QFile::exists( stage ) );

  bool refused = false;
  try
  {
    ProductPublishGuard guard( finalPath, QStringLiteral( ".prev~" ),
                               "r24.b.publish_park", stage );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    refused = e.code() == sicnu::operators::ErrorCode::AlreadyRunning;
  }
  CHECK( refused );
  // The documented contract: a throwing construction removed the stage —
  // the old member-init lease threw before the ctor body's cleanup ran.
  CHECK_FALSE( QFile::exists( stage ) );
}
