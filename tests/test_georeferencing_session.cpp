// test_georeferencing_session.cpp — #32 Georeferencing Session + warp snapshot
// ADR 0020 (S1): re-based onto the injected RsGeorefWarpExecutor seam — no real
// TaskCenter. Covers source-pixel RMS, per-point residuals, min-GCP gating,
// snapshot immutability, warp submit/cancel via a fake executor, and the RPC
// refinement + DEM-injection branch on a synthetic RPC fixture.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "app/georeferencer/rs_georeferencing_session.h"
#include "app/georeferencer/qgsgeoreftransform.h"
#include "operators/framework/rs_operator_context.h"

#include "warper_test_helpers.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>

using Catch::Approx;

// QGIS thread-local QgsProjContext crashes during glibc atexit cleanup (same
// issue as the georef window tests). Bypass with std::_Exit after Catch reports.
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        std::_Exit( stats.aborting || stats.totals.testCases.failed > 0 ? 1 : 0 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{

/// Records warp submissions/cancels and completes tasks on demand by emitting
/// taskUpdated — the same seam the production TaskCenter adapter uses.
class FakeWarpExecutor : public QObject
{
  Q_OBJECT
  public:
  signals:
    void taskUpdated( const sicnu::AlgorithmTaskInfo &info );
  public:
    struct Submission
    {
      long taskId = -1;
      sicnu::jobs::JobRequest request;
      sicnu::TaskCenter::JobExecutor executor;
      sicnu::TaskCenter::CancelHook onCancel;
    };

    QVector<Submission> submissions;
    QVector<long> cancelCalls;
    long nextTaskId = 100;

    long submitWarp( const sicnu::jobs::JobRequest &request,
                     const sicnu::TaskCenter::JobExecutor &executor,
                     const sicnu::TaskCenter::CancelHook &onCancel )
    {
      Submission s;
      s.taskId = nextTaskId++;
      s.request = request;
      s.executor = executor;
      s.onCancel = onCancel;
      submissions.append( s );
      return s.taskId;
    }

    bool cancelWarp( long taskId )
    {
      cancelCalls.append( taskId );
      for ( const Submission &s : submissions )
      {
        if ( s.taskId == taskId && s.onCancel )
          s.onCancel();
      }
      completeTask( taskId, sicnu::TaskStatus::Canceled, QStringLiteral( "Canceled" ) );
      return true;
    }

    /// Run the recorded job closure synchronously and emit the terminal update,
    /// mirroring what TaskCenter does on a worker thread.
    void runToCompletion( long taskId )
    {
      for ( const Submission &s : submissions )
      {
        if ( s.taskId != taskId )
          continue;
        bool ok = true;
        QString error;
        try
        {
          sicnu::operators::RSOperatorContext ctx;
          s.executor( s.request, ctx );
        }
        catch ( const std::exception &e )
        {
          ok = false;
          error = QString::fromUtf8( e.what() );
        }
        catch ( ... )
        {
          ok = false;
          error = QStringLiteral( "unknown job error" );
        }
        completeTask( taskId,
                      ok ? sicnu::TaskStatus::Completed : sicnu::TaskStatus::Failed,
                      error );
        return;
      }
    }

    void completeTask( long taskId, sicnu::TaskStatus status,
                       const QString &error = QString() )
    {
      sicnu::AlgorithmTaskInfo info;
      info.taskId = taskId;
      info.status = status;
      info.errorMessage = error;
      emit taskUpdated( info );
    }
};

struct WarpFinishedRecord
{
  int count = 0;
  long taskCenterId = -1;
  bool success = false;
  QString errorMessage;
  QString outputPath;
};

void recordWarpFinished( RsGeoreferencingSession &session, WarpFinishedRecord &rec )
{
  QObject::connect( &session, &RsGeoreferencingSession::warpFinished,
                    &session, [&rec]( long id, bool ok, const QString &err, const QString &out ) {
    ++rec.count;
    rec.taskCenterId = id;
    rec.success = ok;
    rec.errorMessage = err;
    rec.outputPath = out;
  } );
}

QCoreApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    static int argc = 1;
    static char name[] = "test_georeferencing_session";
    static char *argv[] = { name, nullptr };
    static QCoreApplication app( argc, argv );
    return &app;
  }
  return QCoreApplication::instance();
}

/// Session GCP helper: value with default empty destination CRS.
QgsGcpPoint mkGcp( const QgsPointXY &src, const QgsPointXY &dst, bool enabled = true )
{
  return QgsGcpPoint( src, dst, QgsCoordinateReferenceSystem(), enabled );
}

QVector<QgsGcpPoint> linearGcps()
{
  // Four corners: identity-ish map (source pixel → dest map).
  QVector<QgsGcpPoint> gcps;
  gcps.append( mkGcp( QgsPointXY( 0, 0 ), QgsPointXY( 100, 200 ) ) );
  gcps.append( mkGcp( QgsPointXY( 10, 0 ), QgsPointXY( 110, 200 ) ) );
  gcps.append( mkGcp( QgsPointXY( 0, 10 ), QgsPointXY( 100, 210 ) ) );
  gcps.append( mkGcp( QgsPointXY( 10, 10 ), QgsPointXY( 110, 210 ) ) );
  return gcps;
}

QVector<QgsGcpPoint> gcpSet( const QVector<QgsPointXY> &src,
                             const QVector<QgsPointXY> &dst )
{
  QVector<QgsGcpPoint> gcps;
  for ( int i = 0; i < src.size(); ++i )
    gcps.append( mkGcp( src.at( i ), dst.at( i ) ) );
  return gcps;
}

} // namespace

TEST_CASE( "GeoreferencingSession: fit readiness from GCP pairing",
           "[georef][session][fit]" )
{
  RsGeoreferencingSession session;
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );

  SECTION( "too few GCPs is not ready" )
  {
    QVector<QgsGcpPoint> one;
    one.append( mkGcp( QgsPointXY( 0, 0 ), QgsPointXY( 1, 1 ) ) );
    session.setGcps( one );
    const auto fit = session.refit();
    REQUIRE_FALSE( fit.ready );
    REQUIRE( fit.enabledGcpCount == 1 );
    REQUIRE( fit.minimumGcpCount >= 2 );
    REQUIRE_FALSE( fit.errorMessage.isEmpty() );
    REQUIRE( fit.residuals.size() == 1 );
    REQUIRE_FALSE( rsGeorefResidualIsValid( fit.residuals.at( 0 ) ) );
  }

  SECTION( "enough GCPs for Linear becomes ready with RMS" )
  {
    session.setGcps( linearGcps() );
    const auto fit = session.refit();
    REQUIRE( fit.ready );
    REQUIRE( fit.enabledGcpCount == 4 );
    REQUIRE( fit.rms >= 0.0 );
    REQUIRE( session.isFitReady() );
  }
}

TEST_CASE( "GeoreferencingSession: RMS in source pixels matches hand-computed fit",
           "[georef][session][fit][rms]" )
{
  // Linear least squares (per-axis origin+scale, invertY bookends cancel):
  //   src (0,0),(4,0),(0,3),(4,3) → dst (10,20),(18,20),(10,26),(18,27)
  // x maps exactly (origin 10, scale 2). y fits scale 13/6, origin 20, giving
  // back-transformed pixel residuals (0,0),(0,0),(0,-3/13),(0,+3/13).
  // RMS = sqrt( (2*(3/13)^2) / 4 ) = 3 / (13*sqrt(2)).
  RsGeoreferencingSession session;
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  session.setGcps( gcpSet(
    { { 0, 0 }, { 4, 0 }, { 0, 3 }, { 4, 3 } },
    { { 10, 20 }, { 18, 20 }, { 10, 26 }, { 18, 27 } } ) );

  const auto fit = session.refit();
  REQUIRE( fit.ready );

  const double expectedRms = 3.0 / ( 13.0 * std::sqrt( 2.0 ) );
  REQUIRE( fit.rms == Approx( expectedRms ).epsilon( 1e-9 ) );

  REQUIRE( fit.residuals.size() == 4 );
  REQUIRE( fit.residuals.at( 0 ).x() == Approx( 0.0 ).margin( 1e-9 ) );
  REQUIRE( fit.residuals.at( 0 ).y() == Approx( 0.0 ).margin( 1e-9 ) );
  REQUIRE( fit.residuals.at( 1 ).y() == Approx( 0.0 ).margin( 1e-9 ) );
  REQUIRE( fit.residuals.at( 2 ).x() == Approx( 0.0 ).margin( 1e-9 ) );
  REQUIRE( fit.residuals.at( 2 ).y() == Approx( -3.0 / 13.0 ).epsilon( 1e-9 ) );
  REQUIRE( fit.residuals.at( 3 ).y() == Approx( 3.0 / 13.0 ).epsilon( 1e-9 ) );
}

TEST_CASE( "GeoreferencingSession: per-point residuals, disabled GCP sentinel",
           "[georef][session][fit][residuals]" )
{
  RsGeoreferencingSession session;
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );

  // Exact affine fit for the first three points; fourth is disabled.
  auto gcps = gcpSet( { { 0, 0 }, { 4, 0 }, { 0, 3 }, { 4, 3 } },
                      { { 10, 20 }, { 18, 20 }, { 10, 26 }, { 18, 26 } } );
  gcps[3].setEnabled( false );
  session.setGcps( gcps );

  const auto fit = session.refit();
  REQUIRE( fit.ready );
  REQUIRE( fit.enabledGcpCount == 3 );
  REQUIRE( fit.rms == Approx( 0.0 ).margin( 1e-9 ) );

  // residuals aligns with gcps() ordering; disabled point keeps the sentinel.
  REQUIRE( fit.residuals.size() == 4 );
  REQUIRE( rsGeorefResidualIsValid( fit.residuals.at( 0 ) ) );
  REQUIRE( rsGeorefResidualIsValid( fit.residuals.at( 1 ) ) );
  REQUIRE( rsGeorefResidualIsValid( fit.residuals.at( 2 ) ) );
  REQUIRE_FALSE( rsGeorefResidualIsValid( fit.residuals.at( 3 ) ) );
  REQUIRE( fit.residuals.at( 0 ).x() == Approx( 0.0 ).margin( 1e-9 ) );
  REQUIRE( fit.residuals.at( 0 ).y() == Approx( 0.0 ).margin( 1e-9 ) );

  // Re-enable the point and refit: residual becomes valid again.
  gcps[3].setEnabled( true );
  session.setGcps( gcps );
  const auto fit2 = session.refit();
  REQUIRE( fit2.ready );
  REQUIRE( rsGeorefResidualIsValid( fit2.residuals.at( 3 ) ) );
}

TEST_CASE( "GeoreferencingSession: minimum GCP count per method",
           "[georef][session][fit][min]" )
{
  using TM = QgsGcpTransformerInterface::TransformMethod;

  auto checkMinimum = []( TM method, int expectedMin ) {
    RsGeoreferencingSession session;
    session.setTransformMethod( method );
    // One short of the minimum → not ready.
    QVector<QgsGcpPoint> gcps;
    for ( int i = 0; i < expectedMin - 1; ++i )
      gcps.append( mkGcp( QgsPointXY( i, i ), QgsPointXY( 10 + i, 20 + i ) ) );
    session.setGcps( gcps );
    const auto fit = session.refit();
    REQUIRE( fit.minimumGcpCount == expectedMin );
    REQUIRE( fit.enabledGcpCount == expectedMin - 1 );
    REQUIRE_FALSE( fit.ready );
  };

  checkMinimum( TM::Linear, 2 );
  checkMinimum( TM::Helmert, 2 );
  checkMinimum( TM::PolynomialOrder1, 3 );
  checkMinimum( TM::PolynomialOrder2, 6 );
  checkMinimum( TM::Projective, 4 );
}

TEST_CASE( "GeoreferencingSession: engine seam QgsGeorefTransform::fit (ADR 0057)",
           "[georef][session][fit][engine]" )
{
  using TM = QgsGcpTransformerInterface::TransformMethod;

  SECTION( "Linear: gating and RMS through the engine directly" )
  {
    const auto tooFew = QgsGeorefTransform::fit(
      gcpSet( { { 0, 0 } }, { { 10, 20 } } ), TM::Linear, QString(), QString(), 0.0 );
    REQUIRE_FALSE( tooFew.ready );
    REQUIRE( tooFew.minimumGcpCount == 2 );
    REQUIRE( tooFew.enabledGcpCount == 1 );
    REQUIRE_FALSE( tooFew.errorMessage.isEmpty() );
    REQUIRE( tooFew.residuals.size() == 1 );
    REQUIRE_FALSE( rsGeorefResidualIsValid( tooFew.residuals.at( 0 ) ) );

    const auto ok = QgsGeorefTransform::fit(
      gcpSet( { { 0, 0 }, { 4, 4 } }, { { 10, 20 }, { 14, 24 } } ),
      TM::Linear, QString(), QString(), 0.0 );
    REQUIRE( ok.ready );
    REQUIRE( ok.rms == Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( ok.residuals.size() == 2 );
  }

  SECTION( "RpcPhysical: double-fit diagnostic via the engine" )
  {
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
    REQUIRE_FALSE( rpcPath.isEmpty() );

    // Synthetic RPC maps (16,16)->(115.9995, 38.9995), (32,32)->(116,39),
    // (48,48)->(116.0005, 39.0005); bias all GCPs by (+0.01, +0.005).
    const auto fit = QgsGeorefTransform::fit(
      gcpSet( { { 16, 16 }, { 32, 32 }, { 48, 48 } },
              { { 116.0 - 0.0005 + 0.01, 39.0 - 0.0005 + 0.005 },
                { 116.0 + 0.01, 39.0 + 0.005 },
                { 116.0 + 0.0005 + 0.01, 39.0 + 0.0005 + 0.005 } } ),
      TM::RpcPhysical, rpcPath, QString(), 0.0 );
    REQUIRE( fit.ready );
    REQUIRE( fit.minimumGcpCount == 0 );
    REQUIRE( fit.refinementRmsBefore >= 0.0 );
    REQUIRE( fit.rms >= 0.0 );
    REQUIRE( fit.rms < fit.refinementRmsBefore );
    REQUIRE( fit.rms < 0.1 );
    REQUIRE( std::abs( fit.residuals[1].y() ) < 0.1 );
  }
}

TEST_CASE( "GeoreferencingSession: snapshot gating mirrors applyTransform",
           "[georef][session][snapshot]" )
{
  RsGeoreferencingSession session;
  session.setSourceRasterPath( QStringLiteral( "/tmp/source.tif" ) );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );

  const auto makeSnap = [&session]( const QString &out ) {
    return session.createWarpSnapshot(
      out, QgsImageWarper::ResamplingMethod::NearestNeighbour,
      QgsCoordinateReferenceSystem(), 0.0 );
  };

  // No fit yet → refused.
  REQUIRE_FALSE( makeSnap( QStringLiteral( "/tmp/out.tif" ) ).has_value() );

  // Fit with the Linear minimum (2 enabled GCPs) → accepted.
  session.setGcps( gcpSet( { { 0, 0 }, { 4, 4 } }, { { 10, 20 }, { 14, 24 } } ) );
  REQUIRE( session.refit().ready );
  REQUIRE( makeSnap( QStringLiteral( "/tmp/out.tif" ) ).has_value() );

  // Empty output path → refused even with a ready fit.
  REQUIRE_FALSE( makeSnap( QString() ).has_value() );

  // Drop below the method minimum WITHOUT refitting (stale ready fit): the
  // snapshot gate must look at the live GCP list, like applyTransform does.
  session.setGcps( gcpSet( { { 0, 0 } }, { { 10, 20 } } ) );
  REQUIRE( session.isFitReady() ); // stale fit is still marked ready
  REQUIRE_FALSE( makeSnap( QStringLiteral( "/tmp/out2.tif" ) ).has_value() );

  // Empty source path → refused.
  session.setGcps( gcpSet( { { 0, 0 }, { 4, 4 } }, { { 10, 20 }, { 14, 24 } } ) );
  session.setSourceRasterPath( QString() );
  REQUIRE( session.refit().ready );
  REQUIRE_FALSE( makeSnap( QStringLiteral( "/tmp/out3.tif" ) ).has_value() );
}

TEST_CASE( "GeoreferencingSession: warp snapshot is immutable after session edits",
           "[georef][session][snapshot]" )
{
  RsGeoreferencingSession session;
  session.setSourceRasterPath( QStringLiteral( "/tmp/source.tif" ) );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  session.setGcps( linearGcps() );
  REQUIRE( session.refit().ready );

  const auto snapOpt = session.createWarpSnapshot(
    QStringLiteral( "/tmp/out.tif" ),
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    QgsCoordinateReferenceSystem(),
    0.0 );
  REQUIRE( snapOpt.has_value() );
  const RsGeorefWarpSnapshot snap = *snapOpt;
  REQUIRE( snap.gcps.size() == 4 );
  REQUIRE( snap.sourcePath == QStringLiteral( "/tmp/source.tif" ) );
  REQUIRE( snap.outputPath == QStringLiteral( "/tmp/out.tif" ) );
  const QgsPointXY firstSrc = snap.gcps.at( 0 ).sourcePoint();

  // Later session edits must not mutate the frozen snapshot.
  session.clearGcps();
  session.setSourceRasterPath( QStringLiteral( "/tmp/other.tif" ) );
  REQUIRE( session.gcps().isEmpty() );
  REQUIRE_FALSE( session.isFitReady() );

  REQUIRE( snap.gcps.size() == 4 );
  REQUIRE( snap.gcps.at( 0 ).sourcePoint() == firstSrc );
  REQUIRE( snap.sourcePath == QStringLiteral( "/tmp/source.tif" ) );
  REQUIRE( snap.outputPath == QStringLiteral( "/tmp/out.tif" ) );

  // Snapshot without fit readiness is refused.
  REQUIRE_FALSE( session.createWarpSnapshot(
                   QStringLiteral( "/tmp/out2.tif" ),
                   QgsImageWarper::ResamplingMethod::NearestNeighbour,
                   QgsCoordinateReferenceSystem(), 0.0 )
                   .has_value() );
}

TEST_CASE( "GeoreferencingSession: appendGcps bulk-appends and refits",
           "[georef][session][match]" )
{
  RsGeoreferencingSession session;
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  REQUIRE( session.gcps().isEmpty() );

  QVector<QgsGcpPoint> pairs;
  pairs.append( mkGcp( QgsPointXY( 1, 2 ), QgsPointXY( 10, 20 ) ) );
  pairs.append( mkGcp( QgsPointXY( 3, 4 ), QgsPointXY( 30, 40 ) ) );
  session.appendGcps( pairs );

  REQUIRE( session.gcps().size() == 2 );
  REQUIRE( session.gcps().at( 0 ).sourcePoint() == QgsPointXY( 1, 2 ) );
  REQUIRE( session.gcps().at( 1 ).destinationPoint() == QgsPointXY( 30, 40 ) );

  // Second accept appends, does not replace.
  session.appendGcps( pairs );
  REQUIRE( session.gcps().size() == 4 );
}

TEST_CASE( "GeoreferencingSession: granular GCP mutations emit and refit",
           "[georef][session][mutation]" )
{
  RsGeoreferencingSession session;
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );

  int gcpsChangedCount = 0;
  int fitChangedCount = 0;
  QObject::connect( &session, &RsGeoreferencingSession::gcpsChanged,
                    &session, [&gcpsChangedCount]() { ++gcpsChangedCount; } );
  QObject::connect( &session, &RsGeoreferencingSession::fitChanged,
                    &session, [&fitChangedCount]( const RsGeorefFitResult & ) { ++fitChangedCount; } );

  SECTION( "addGcp / removeGcpAt mutate and refit" )
  {
    session.addGcp( mkGcp( QgsPointXY( 0, 0 ), QgsPointXY( 100, 200 ) ) );
    session.addGcp( mkGcp( QgsPointXY( 10, 0 ), QgsPointXY( 110, 200 ) ) );
    session.addGcp( mkGcp( QgsPointXY( 0, 10 ), QgsPointXY( 100, 210 ) ) );
    REQUIRE( session.gcps().size() == 3 );
    REQUIRE( gcpsChangedCount == 3 );
    REQUIRE( fitChangedCount == 3 );
    // Each add refits; with 3 points Linear is ready.
    REQUIRE( session.isFitReady() );

    session.removeGcpAt( 2 );
    REQUIRE( session.gcps().size() == 2 );
    REQUIRE( gcpsChangedCount == 4 );
    REQUIRE( fitChangedCount == 4 );

    // Out-of-range removals are ignored without signals.
    session.removeGcpAt( 99 );
    REQUIRE( gcpsChangedCount == 4 );
  }

  SECTION( "setGcpEnabled toggles the enabled count and refits" )
  {
    session.setGcps( linearGcps() );
    REQUIRE( session.refit().ready );
    gcpsChangedCount = 0;
    fitChangedCount = 0;

    session.setGcpEnabled( 3, false );
    REQUIRE( gcpsChangedCount == 1 );
    REQUIRE( fitChangedCount == 1 );
    REQUIRE( session.gcps().at( 3 ).isEnabled() == false );
    REQUIRE( session.lastFit().enabledGcpCount == 3 );
    REQUIRE_FALSE( rsGeorefResidualIsValid( session.lastFit().residuals.at( 3 ) ) );

    session.setGcpEnabled( 3, true );
    REQUIRE( session.lastFit().enabledGcpCount == 4 );
    REQUIRE( rsGeorefResidualIsValid( session.lastFit().residuals.at( 3 ) ) );

    // No-op toggle emits nothing.
    const int before = gcpsChangedCount;
    session.setGcpEnabled( 3, true );
    REQUIRE( gcpsChangedCount == before );
  }

  SECTION( "setGcpSource / setGcpDestination / setGcpPointType update rows" )
  {
    session.setGcps( linearGcps() );
    REQUIRE( session.refit().ready );
    gcpsChangedCount = 0;
    fitChangedCount = 0;

    session.setGcpSource( 0, QgsPointXY( 2, 2 ) );
    session.setGcpDestination( 0, QgsPointXY( 120, 220 ) );
    session.setGcpPointType( 0, QStringLiteral( "road" ) );
    REQUIRE( gcpsChangedCount == 3 );
    REQUIRE( fitChangedCount == 3 );
    REQUIRE( session.gcps().at( 0 ).sourcePoint() == QgsPointXY( 2, 2 ) );
    REQUIRE( session.gcps().at( 0 ).destinationPoint() == QgsPointXY( 120, 220 ) );
    REQUIRE( session.gcps().at( 0 ).pointType() == QStringLiteral( "road" ) );
    // The fit follows the edited coordinates (no longer exact → non-zero RMS).
    REQUIRE( session.lastFit().ready );
    REQUIRE( session.lastFit().rms > 0.0 );
  }
}

TEST_CASE( "GeoreferencingSession: transformFromSnapshot uses frozen GCPs",
           "[georef][session][snapshot]" )
{
  RsGeoreferencingSession session;
  session.setSourceRasterPath( QStringLiteral( "/tmp/source.tif" ) );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  session.setGcps( linearGcps() );
  REQUIRE( session.refit().ready );

  const auto snap = session.createWarpSnapshot(
    QStringLiteral( "/tmp/out.tif" ),
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    QgsCoordinateReferenceSystem(), 0.0 );
  REQUIRE( snap.has_value() );

  auto xf = RsGeoreferencingSession::transformFromSnapshot( *snap );
  REQUIRE( xf != nullptr );
  REQUIRE( xf->parametersInitialized() );
}

TEST_CASE( "GeoreferencingSession: warp submit/cancel through injected executor",
           "[georef][session][warp]" )
{
  ensureApp();
  auto executor = std::make_shared<FakeWarpExecutor>();
  CustomWarpExecutor customExec;
  customExec.submit = [executor]( const sicnu::jobs::JobRequest &req,
                                  const sicnu::TaskCenter::JobExecutor &exec,
                                  const sicnu::TaskCenter::CancelHook &cancel ) {
    return executor->submitWarp( req, exec, cancel );
  };
  customExec.cancel = [executor]( long taskId ) {
    return executor->cancelWarp( taskId );
  };
  RsGeoreferencingSession session( customExec );
  QObject::connect( executor.get(), &FakeWarpExecutor::taskUpdated,
                    &session, &RsGeoreferencingSession::onTaskUpdated );
  session.setSourceRasterPath( QStringLiteral( "/tmp/source.tif" ) );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  session.setGcps( linearGcps() );
  REQUIRE( session.refit().ready );

  const auto snap = session.createWarpSnapshot(
    QStringLiteral( "/tmp/out.tif" ),
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    QgsCoordinateReferenceSystem(), 0.0 );
  REQUIRE( snap.has_value() );

  WarpFinishedRecord rec;
  recordWarpFinished( session, rec );

  SECTION( "submit records the job; terminal update emits warpFinished" )
  {
    const long id = session.startWarpTask( *snap );
    REQUIRE( id >= 0 );
    REQUIRE( session.pendingWarpTaskId() == id );
    REQUIRE( executor->submissions.size() == 1 );
    REQUIRE( executor->submissions.at( 0 ).request.algorithmId == "module:georef:warp" );

    // A second submit while one is pending is refused.
    REQUIRE( session.startWarpTask( *snap ) == -1 );
    REQUIRE( executor->submissions.size() == 1 );

    // Fake a failed terminal update (job never ran → warp task has no result).
    executor->completeTask( id, sicnu::TaskStatus::Failed,
                            QStringLiteral( "boom" ) );
    REQUIRE( rec.count == 1 );
    REQUIRE( rec.taskCenterId == id );
    REQUIRE_FALSE( rec.success );
    REQUIRE( rec.outputPath == QStringLiteral( "/tmp/out.tif" ) );
    REQUIRE( session.pendingWarpTaskId() == -1 );
  }

  SECTION( "cancel routes through the executor and emits warpFinished" )
  {
    const long id = session.startWarpTask( *snap );
    REQUIRE( id >= 0 );

    REQUIRE( session.cancelWarpTask( id ) );
    REQUIRE( executor->cancelCalls == QVector<long> { id } );
    REQUIRE( rec.count == 1 );
    REQUIRE( rec.taskCenterId == id );
    REQUIRE_FALSE( rec.success );
    REQUIRE( rec.errorMessage == QStringLiteral( "Cancelled" ) );

    // Cancelling an unknown / stale id is refused without hitting the executor.
    REQUIRE_FALSE( session.cancelWarpTask( 9999 ) );
    REQUIRE( executor->cancelCalls.size() == 1 );
  }
}

TEST_CASE( "GeoreferencingSession: warp job runs through fake executor (success path)",
           "[georef][session][warp]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString srcPath = makeSynthetic64Raster( tmp.path() );
  REQUIRE_FALSE( srcPath.isEmpty() );
  const QString outPath = tmp.path() + QStringLiteral( "/warped.tif" );

  auto executor = std::make_shared<FakeWarpExecutor>();
  CustomWarpExecutor customExec;
  customExec.submit = [executor]( const sicnu::jobs::JobRequest &req,
                                  const sicnu::TaskCenter::JobExecutor &exec,
                                  const sicnu::TaskCenter::CancelHook &cancel ) {
    return executor->submitWarp( req, exec, cancel );
  };
  customExec.cancel = [executor]( long taskId ) {
    return executor->cancelWarp( taskId );
  };
  RsGeoreferencingSession session( customExec );
  QObject::connect( executor.get(), &FakeWarpExecutor::taskUpdated,
                    &session, &RsGeoreferencingSession::onTaskUpdated );
  session.setSourceRasterPath( srcPath );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
  session.setGcps( linearGcps() );
  REQUIRE( session.refit().ready );

  const auto snap = session.createWarpSnapshot(
    outPath, QgsImageWarper::ResamplingMethod::NearestNeighbour,
    QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ), 0.0 );
  REQUIRE( snap.has_value() );

  WarpFinishedRecord rec;
  recordWarpFinished( session, rec );

  const long id = session.startWarpTask( *snap );
  REQUIRE( id >= 0 );

  // Run the recorded job closure — a real 64x64 warp — then emit Completed.
  executor->runToCompletion( id );
  REQUIRE( rec.count == 1 );
  REQUIRE( rec.taskCenterId == id );
  REQUIRE( rec.success );
  REQUIRE( rec.errorMessage.isEmpty() );
  REQUIRE( rec.outputPath == outPath );
}

TEST_CASE( "GeoreferencingSession: RPC refinement + DEM injection branch",
           "[georef][session][fit][rpc]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );
  const QString demPath = makeSyntheticDem( tmp.path() );
  REQUIRE_FALSE( demPath.isEmpty() );

  RsGeoreferencingSession session;
  session.setSourceRasterPath( rpcPath );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  session.setDemPath( demPath );
  session.setDemZOffset( 0.0 );

  // Same biased-GCP fixture as test_rpc_gcp_refine: center (32,32)→(116,39),
  // all GCPs biased by (+0.01°, +0.005°) so refinement has something to fix.
  session.setGcps( gcpSet(
    { { 16, 16 }, { 32, 32 }, { 48, 48 } },
    { { 116.0 - 0.0005 + 0.01, 39.0 - 0.0005 + 0.005 },
      { 116.0 + 0.01, 39.0 + 0.005 },
      { 116.0 + 0.0005 + 0.01, 39.0 + 0.0005 + 0.005 } } ) );

  const auto fit = session.refit();
  REQUIRE( fit.ready );
  REQUIRE( fit.minimumGcpCount == 0 ); // RpcPhysical has no hard minimum

  // Two-phase RPC fit: the unrefined diagnostic RMS is recorded and the
  // refined fit improves on it (constant GCP bias is corrected).
  REQUIRE( fit.refinementRmsBefore >= 0.0 );
  REQUIRE( fit.rms >= 0.0 );
  REQUIRE( fit.rms < fit.refinementRmsBefore );

  REQUIRE( fit.residuals.size() == 3 );
  for ( const QPointF &r : fit.residuals )
    REQUIRE( rsGeorefResidualIsValid( r ) );
}

TEST_CASE( "GeoreferencingSession: RPC below 3 GCPs skips refinement",
           "[georef][session][fit][rpc]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );

  RsGeoreferencingSession session;
  session.setSourceRasterPath( rpcPath );
  session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  session.setDemPath( QString() );

  SECTION( "two GCPs: plain RPC fit, no refinement diagnostic" )
  {
    session.setGcps( gcpSet( { { 16, 16 }, { 32, 32 } },
                             { { 116.0 - 0.016, 39.0 - 0.016 }, { 116.0, 39.0 } } ) );
    const auto fit = session.refit();
    REQUIRE( fit.ready );
    REQUIRE( fit.refinementRmsBefore == -1.0 );
  }

  SECTION( "no GCPs at all: RPC parameters come from raster metadata" )
  {
    session.setGcps( {} );
    const auto fit = session.refit();
    REQUIRE( fit.ready );
    REQUIRE( fit.enabledGcpCount == 0 );
    // No points → RMS is undefined, residuals empty.
    REQUIRE( fit.residuals.isEmpty() );
  }
}

TEST_CASE( "GeoreferencingSession: DEM setters mark dirty (ADR 0027)",
           "[georef][session][dirty]" )
{
  RsGeoreferencingSession session;
  session.clearDirty();
  REQUIRE_FALSE( session.isDirty() );

  session.setDemPath( QStringLiteral( "/tmp/dem.tif" ) );
  REQUIRE( session.isDirty() );
  REQUIRE( session.demPath() == QStringLiteral( "/tmp/dem.tif" ) );

  session.clearDirty();
  REQUIRE_FALSE( session.isDirty() );

  session.setDemZOffset( 15.5 );
  REQUIRE( session.isDirty() );
  REQUIRE( session.demZOffset() == 15.5 );
}

TEST_CASE( "GeoreferencingSession: WorkflowRuntime mirror integration (ADR 0028)",
           "[georef][session][workflow]" )
{
  RsGeoreferencingSession session;
  REQUIRE_FALSE( session.isWorkflowMirrorActive() );

  REQUIRE( session.enableWorkflowMirror( "lab.georef.image_to_map" ) );
  REQUIRE( session.isWorkflowMirrorActive() );
  const std::string sid = session.workflowSessionId();
  REQUIRE_FALSE( sid.empty() );

  session.setSourceRasterPath( QStringLiteral( "/tmp/source.tif" ) );
  session.setGcps( linearGcps() );

  auto state = session.workflowRuntime().state( sid );
  REQUIRE( state.currentStepId == "gcp" );
  REQUIRE( state.artifacts.count( "source_raster" ) == 1 );
  REQUIRE( state.artifacts["source_raster"] == "/tmp/source.tif" );
  REQUIRE( state.artifacts.count( "gcp_count" ) == 1 );
  REQUIRE( state.artifacts["gcp_count"] == "4" );

  session.setWorkflowStep( "gcp" );
  session.markWorkflowStepComplete( "gcp" );
  state = session.workflowRuntime().state( sid );
  const auto &completed = state.completedStepIds;
  REQUIRE( std::find( completed.begin(), completed.end(), "gcp" ) != completed.end() );
}

TEST_CASE( "Georeferencer/Session: Destructor cancels running warp and cleans up RsWarpTask", "[georef][session]" )
{
  FakeWarpExecutor fake;
  CustomWarpExecutor custom{
    [&fake]( const sicnu::jobs::JobRequest &req,
             const sicnu::TaskCenter::JobExecutor &exec,
             const sicnu::TaskCenter::CancelHook &cancel ) {
      return fake.submitWarp( req, exec, cancel );
    },
    [&fake]( long taskId ) {
      return fake.cancelWarp( taskId );
    }
  };

  {
    RsGeoreferencingSession session( custom );
    session.setSourceRasterPath( QStringLiteral( "/tmp/dummy_src.tif" ) );
    session.setTransformMethod( QgsGcpTransformerInterface::TransformMethod::Linear );
    session.setGcps( linearGcps() );
    REQUIRE( session.refit().ready );

    const auto snap = session.createWarpSnapshot(
      QStringLiteral( "/tmp/dummy_dst.tif" ),
      QgsImageWarper::ResamplingMethod::NearestNeighbour,
      QgsCoordinateReferenceSystem(), 0.0 );
    REQUIRE( snap.has_value() );

    long taskId = session.startWarpTask( *snap );
    REQUIRE( taskId >= 0 );
    REQUIRE( session.pendingWarpTaskId() == taskId );
    REQUIRE( fake.submissions.size() == 1 );
    REQUIRE( fake.cancelCalls.isEmpty() );
  }

  REQUIRE( fake.cancelCalls.size() == 1 );
}

#include "test_georeferencing_session.moc"
