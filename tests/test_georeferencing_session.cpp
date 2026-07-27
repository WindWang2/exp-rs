// test_georeferencing_session.cpp — #32 Georeferencing Session + warp snapshot
#include <catch2/catch_test_macros.hpp>

#include "app/georeferencer/rs_georeferencing_session.h"
#include "app/georeferencer/qgsgeoreftransform.h"
#include "processing/framework/task_center.h"

#include <chrono>
#include <thread>

namespace
{

sicnu::AlgorithmTaskInfo waitForTerminalTask( long taskId )
{
  sicnu::AlgorithmTaskInfo info;
  for ( int attempt = 0; attempt < 2000; ++attempt )
  {
    info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
      return info;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  return info;
}

QVector<RsGeorefGcpPair> linearGcps()
{
  // Four corners: identity-ish map (source pixel → dest map).
  QVector<RsGeorefGcpPair> gcps;
  gcps.append( { QgsPointXY( 0, 0 ), QgsPointXY( 100, 200 ), true } );
  gcps.append( { QgsPointXY( 10, 0 ), QgsPointXY( 110, 200 ), true } );
  gcps.append( { QgsPointXY( 0, 10 ), QgsPointXY( 100, 210 ), true } );
  gcps.append( { QgsPointXY( 10, 10 ), QgsPointXY( 110, 210 ), true } );
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
    QVector<RsGeorefGcpPair> one;
    one.append( { QgsPointXY( 0, 0 ), QgsPointXY( 1, 1 ), true } );
    session.setGcps( one );
    const auto fit = session.refit();
    REQUIRE_FALSE( fit.ready );
    REQUIRE( fit.enabledGcpCount == 1 );
    REQUIRE( fit.minimumGcpCount >= 2 );
    REQUIRE_FALSE( fit.errorMessage.isEmpty() );
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
  const QgsPointXY firstSrc = snap.gcps.at( 0 ).source;

  // Later session edits must not mutate the frozen snapshot.
  session.clearGcps();
  session.setSourceRasterPath( QStringLiteral( "/tmp/other.tif" ) );
  REQUIRE( session.gcps().isEmpty() );
  REQUIRE_FALSE( session.isFitReady() );

  REQUIRE( snap.gcps.size() == 4 );
  REQUIRE( snap.gcps.at( 0 ).source == firstSrc );
  REQUIRE( snap.sourcePath == QStringLiteral( "/tmp/source.tif" ) );
  REQUIRE( snap.outputPath == QStringLiteral( "/tmp/out.tif" ) );

  // Snapshot without fit readiness is refused.
  REQUIRE_FALSE( session.createWarpSnapshot(
                   QStringLiteral( "/tmp/out2.tif" ),
                   QgsImageWarper::ResamplingMethod::NearestNeighbour,
                   QgsCoordinateReferenceSystem(), 0.0 )
                   .has_value() );
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
