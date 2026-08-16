// SICNU GEO RS — Task 11.5.5 tests for QgsRpcGcpTransformer GCP refinement.
//
// Verifies that enabling the GCP refinement flag with ≥ 3 biased GCPs
// reduces the forward-transform residual at a known sample point, and
// that fewer than 3 GCPs degrades gracefully (refinement skipped).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>
#include <QVector>

#include <cmath>

#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransform.h"
#include "qgspointxy.h"
#include "qgsrpcgcptransformer.h"

#include "warper_test_helpers.h"

using Catch::Approx;

TEST_CASE( "RPC refinement: 3 biased GCPs reduce mean residual",
           "[georef][rpc][refine]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );

  // Synthetic RPC: lon = 116 + ((line-32)/32)*0.001, lat likewise. Predictions:
  // (16,16)->(115.9995, 38.9995), (32,32)->(116,39), (48,48)->(116.0005, 39.0005).
  // Bias all three by (+0.01°, +0.005°).
  QVector<QgsPointXY> src = { { 16, 16 }, { 32, 32 }, { 48, 48 } };
  QVector<QgsPointXY> dst = {
    { 116.0 - 0.0005 + 0.01, 39.0 - 0.0005 + 0.005 },
    { 116.0 + 0.01, 39.0 + 0.005 },
    { 116.0 + 0.0005 + 0.01, 39.0 + 0.0005 + 0.005 }
  };

  // Without refinement
  QgsRpcGcpTransformer noref( rpcPath );
  noref.setRpcOptions( rpcPath, QString(), 0.0, /*useRefine=*/false );
  REQUIRE( noref.updateParametersFromGcps( src, dst, false ) );
  double x = 32.0;
  double y = 32.0;
  REQUIRE( noref.transform( x, y, false ) );
  const double residNoRef = std::hypot( x - dst[1].x(), y - dst[1].y() );

  // With refinement
  QgsRpcGcpTransformer wref( rpcPath );
  wref.setRpcOptions( rpcPath, QString(), 0.0, /*useRefine=*/true );
  REQUIRE( wref.updateParametersFromGcps( src, dst, false ) );
  double x2 = 32.0;
  double y2 = 32.0;
  REQUIRE( wref.transform( x2, y2, false ) );
  const double residWithRef = std::hypot( x2 - dst[1].x(), y2 - dst[1].y() );

  REQUIRE( residWithRef < residNoRef );
}

TEST_CASE( "RPC refinement: <3 GCPs skips refinement gracefully",
           "[georef][rpc][refine]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );

  QVector<QgsPointXY> src = { { 32, 32 } };
  QVector<QgsPointXY> dst = { { 116.5, 39.5 } };

  QgsRpcGcpTransformer t( rpcPath );
  t.setRpcOptions( rpcPath, QString(), 0.0, true );
  REQUIRE( t.updateParametersFromGcps( src, dst, false ) );
  REQUIRE( t.isValid() );
}

TEST_CASE( "RPC refinement: projected destination CRS does not corrupt the model",
           "[georef][rpc][refine]" )
{
  // #286 - destinations arrive in the panel CRS. Before the fix, meter-scale
  // biases (~500000) were added to the degree-unit LONG_OFF/LAT_OFF, moving
  // the model to ~500000 degrees. With setDestinationCrs the bias is computed
  // in WGS84 lon/lat and the model lands on the GCPs.
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );

  const QgsCoordinateReferenceSystem utm( QStringLiteral( "EPSG:32650" ) );
  REQUIRE( utm.isValid() );
  const QgsCoordinateTransform toUtm( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ),
                                      utm, QgsCoordinateTransformContext() );

  QVector<QgsPointXY> src = { { 16, 16 }, { 32, 32 }, { 48, 48 } };
  // Same +0.01/+0.005 degree bias as the base test, expressed in UTM meters.
  QVector<QgsPointXY> dstDeg = {
    { 116.0 - 0.0005 + 0.01, 39.0 - 0.0005 + 0.005 },
    { 116.0 + 0.01, 39.0 + 0.005 },
    { 116.0 + 0.0005 + 0.01, 39.0 + 0.0005 + 0.005 }
  };
  QVector<QgsPointXY> dst;
  for ( const QgsPointXY &p : dstDeg )
    dst.append( toUtm.transform( p ) );

  QgsRpcGcpTransformer wref( rpcPath );
  wref.setRpcOptions( rpcPath, QString(), 0.0, /*useRefine=*/true );
  wref.setDestinationCrs( utm );
  REQUIRE( wref.updateParametersFromGcps( src, dst, false ) );

  // The RPC transformer always outputs WGS84 lon/lat (degrees); the refined
  // model must map the center pixel onto the center GCP in degrees.
  double x = 32.0;
  double y = 32.0;
  REQUIRE( wref.transform( x, y, false ) );
  REQUIRE( std::hypot( x - dstDeg[1].x(), y - dstDeg[1].y() ) < 0.001 );
}

TEST_CASE( "RPC refinement: a single outlier GCP does not drag the model (median)",
           "[georef][rpc][refine]" )
{
  // #286 - the previous mean bias let one misclicked GCP shift the whole
  // model by outlier/N; the median leaves the three good GCPs exact.
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );

  QVector<QgsPointXY> src = { { 16, 16 }, { 32, 32 }, { 48, 48 }, { 8, 8 } };
  QVector<QgsPointXY> dst = {
    { 116.0 - 0.0005, 39.0 - 0.0005 }, // 3 accurate GCPs (true model)
    { 116.0, 39.0 },
    { 116.0 + 0.0005, 39.0 + 0.0005 },
    { 116.2, 39.2 }                    // 1 outlier, ~0.2 deg (~22 km) off
  };

  QgsRpcGcpTransformer wref( rpcPath );
  wref.setRpcOptions( rpcPath, QString(), 0.0, /*useRefine=*/true );
  REQUIRE( wref.updateParametersFromGcps( src, dst, false ) );

  // Center pixel must stay within ~0.02 deg of the good GCP (mean would
  // shift it by 0.05 deg).
  double x = 32.0;
  double y = 32.0;
  REQUIRE( wref.transform( x, y, false ) );
  REQUIRE( std::hypot( x - 116.0, y - 39.0 ) < 0.02 );
}
