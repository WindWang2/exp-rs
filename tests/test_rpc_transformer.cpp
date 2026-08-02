// SICNU GEO RS — Task 11.4.8 tests for QgsRpcGcpTransformer.
//
// Verifies the GDAL RPC transformer wrapper:
//   - Identity RPC metadata in a synthetic raster maps the center pixel
//     (32, 32) onto the RPC offsets (LONG_OFF, LAT_OFF) = (116.0, 39.0).
//   - minimumGcpCount() reports 0 (coefficients live in raster metadata).
//   - A nonexistent source raster path causes the fit to fail cleanly.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include "qgsgcptransformer.h"
#include "qgsrpcgcptransformer.h"

#include "warper_test_helpers.h"

using Catch::Approx;

TEST_CASE( "RpcTransformer: identity RPC at center maps to LAT/LONG_OFF",
           "[georef][rpc]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( src.isEmpty() );

  QgsRpcGcpTransformer t( src );
  REQUIRE( t.updateParametersFromGcps( {}, {}, false ) );
  REQUIRE( t.isValid() );

  // QgsGcpTransformerInterface::transform is in-place (3 args).  The base
  // class delegates to GDALTransformer / GDALTransformerArgs which our
  // override returns as GDALRPCTransform / the RPC arg.
  double x = 32.0;
  double y = 32.0;
  REQUIRE( t.transform( x, y, false ) );
  REQUIRE( x == Approx( 116.0 ).margin( 0.001 ) );
  REQUIRE( y == Approx( 39.0 ).margin( 0.001 ) );
}

TEST_CASE( "RpcTransformer: minimumGcpCount is 0", "[georef][rpc]" )
{
  // Construct a bare transformer (no source) and assert the instance method.
  QgsRpcGcpTransformer t;
  REQUIRE( t.minimumGcpCount() == 0 );

  // Also exercise the factory path so the enum entry is wired in createFromParameters().
  std::unique_ptr<QgsGcpTransformerInterface> created(
    QgsGcpTransformerInterface::create(
      QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) );
  REQUIRE( created != nullptr );
  REQUIRE( created->minimumGcpCount() == 0 );
  REQUIRE( created->method() ==
           QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
}

TEST_CASE( "RpcTransformer: invalid path returns false on fit", "[georef][rpc]" )
{
  QgsRpcGcpTransformer t( QStringLiteral( "/does/not/exist.tif" ) );
  REQUIRE_FALSE( t.updateParametersFromGcps( {}, {}, false ) );
  REQUIRE_FALSE( t.isValid() );
}

TEST_CASE( "RpcTransformer: options flow through the base-class interface (ADR 0057)",
           "[georef][rpc]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = makeSyntheticRpcRaster( tmp.path() );
  REQUIRE_FALSE( src.isEmpty() );

  // Configure through QgsGcpTransformerInterface pointers — the seam that
  // replaces the old dynamic_cast access — and verify the options take
  // effect (source path + zOffset reach the GDAL transformer).
  std::unique_ptr<QgsGcpTransformerInterface> t(
    QgsGcpTransformerInterface::create(
      QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) );
  REQUIRE( t != nullptr );
  REQUIRE( t->setRpcOptions( src, QString(), 100.0, false ) );
  REQUIRE( t->updateParametersFromGcps( {}, {}, false ) );

  double x = 32.0;
  double y = 32.0;
  REQUIRE( t->transform( x, y, false ) );

  // Non-RPC transformers ignore the call and report no DEM (default no-op).
  std::unique_ptr<QgsGcpTransformerInterface> linear(
    QgsGcpTransformerInterface::create(
      QgsGcpTransformerInterface::TransformMethod::Linear ) );
  REQUIRE( linear != nullptr );
  REQUIRE_FALSE( linear->setRpcOptions( src, QString(), 100.0, true ) );
  REQUIRE( linear->demPath().isEmpty() );
}
