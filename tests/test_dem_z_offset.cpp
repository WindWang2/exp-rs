// SICNU GEO RS — Task 11.5.4 tests for QgsRpcGcpTransformer::setRpcOptions().
//
// Verifies that changing the DEM Z-offset propagates into the GDAL
// RPC transformer (via the `RPC_HEIGHT` papszOptions key) and produces
// a measurable shift in the forward transform output.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <cmath>

#include "qgsrpcgcptransformer.h"

#include "warper_test_helpers.h"

using Catch::Approx;

TEST_CASE( "RPC Z-offset: changing zOffset shifts forward transform",
           "[georef][rpc][zoffset]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rpcPath = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( rpcPath.isEmpty() );

  QgsRpcGcpTransformer t0( rpcPath );
  t0.setRpcOptions( QString(), 0.0, false );
  REQUIRE( t0.updateParametersFromGcps( {}, {}, false ) );
  double x0 = 32.0;
  double y0 = 32.0;
  REQUIRE( t0.transform( x0, y0, false ) );

  QgsRpcGcpTransformer t100( rpcPath );
  t100.setRpcOptions( QString(), 100.0, false );
  REQUIRE( t100.updateParametersFromGcps( {}, {}, false ) );
  double x1 = 32.0;
  double y1 = 32.0;
  REQUIRE( t100.transform( x1, y1, false ) );

  const double dx = std::abs( x1 - x0 );
  const double dy = std::abs( y1 - y0 );
  REQUIRE( ( dx > 1e-6 || dy > 1e-6 ) );
}
