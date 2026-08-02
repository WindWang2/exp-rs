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

  // Predicted center maps (32,32)->(116°,39°). Bias 3 GCPs by (+0.01°, +0.005°).
  QVector<QgsPointXY> src = { { 16, 16 }, { 32, 32 }, { 48, 48 } };
  QVector<QgsPointXY> dst = {
    { 116.0 - 0.016 + 0.01, 39.0 - 0.016 + 0.005 },
    { 116.0 + 0.01, 39.0 + 0.005 },
    { 116.0 + 0.016 + 0.01, 39.0 + 0.016 + 0.005 }
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
