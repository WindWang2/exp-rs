// tests/test_r15_C_runtime_contracts.cpp — Track 15 WP-C07: the documented
// per-tile coverage gate (tiling.min_valid_coverage) executes on the
// SINGLE-INPUT engine too.
//
// The multi-input engine has always applied the gate; the single-input
// engine computed the per-tile valid count but consulted it only at BATCH
// granularity (all-NoData), so a documented manifest gate was silently
// ignored on this path — a low-coverage tile ran a full forward pass and
// wrote predictions where the contract promises NoData.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <vector>

namespace {

using namespace sicnu::operators;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::TileInferenceEngine;
using sicnu::operators::runtime::TileInferenceStats;

/// One-plane fake: the output carries a constant plane so processed tiles are
/// distinguishable from NoData (NaN) pixels.
class PlaneRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r15c-plane"; }
    std::string backendName() const override { return "r15c-plane"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r15c-plane"; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int B = blob.size[0];
      const int H = blob.size[2];
      const int W = blob.size[3];
      const int dims[4] = { B, 1, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.5f ) );
      return out;
    }
};

std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  REQUIRE( ds->GetRasterBand( band )
             ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                         GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return data;
}

} // namespace

TEST_CASE( "R15-C C07: the single-input engine applies the min_valid_coverage gate",
           "[models][tile][r15-c]" )
{
  QTemporaryDir dir;
  // 64x64, tile 32 → 4 tiles. The first tile's left half is NoData (declared
  // sentinel on the band): coverage 50% < the 0.75 gate.
  const QString input = dir.filePath( QStringLiteral( "cov.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 1, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f );
  builder.withNoData( -9999.0 );
  builder.withRect( 1, 0, 0, 16, 32, -9999.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );

  ModelInfo model;
  model.name = "r15c-cov";
  model.framework = "r15c-plane";
  model.task = "segmentation";
  model.tiling.tileSize = 32;
  model.tiling.minValidCoverage = 0.75;
  model.output.classes = { "change" };
  model.output.type = "raster";

  auto runtime = std::make_shared<PlaneRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "cov_out.tif" ) );

  const TileInferenceStats stats =
    engine.run( input.toStdString(), {}, out.toStdString(), context, {} );

  // THE ORACLE: exactly one tile skipped for low coverage; on master the gate
  // was ignored (tilesSkippedNoData == 0) and every tile was inferred.
  CHECK( stats.tilesSkippedNoData == 1 );
  CHECK( stats.tilesProcessed == 4 );

  int width = 0;
  int height = 0;
  const std::vector<float> band = readBand( out, 1, width, height );
  REQUIRE( width == 64 );
  REQUIRE( height == 64 );
  const auto at = [ & ]( int x, int y ) {
    return band[static_cast<std::size_t>( y ) * width + x];
  };
  // The whole first tile is NoData — including its VALID half (the gate skips
  // the tile, it does not partially infer it).
  CHECK( std::isnan( at( 4, 4 ) ) );    // NoData half
  CHECK( std::isnan( at( 20, 4 ) ) );   // valid half of the skipped tile
  // The remaining tiles inferred normally.
  CHECK( at( 40, 4 ) == 0.5f );
  CHECK( at( 4, 40 ) == 0.5f );
  CHECK( at( 40, 40 ) == 0.5f );
}

TEST_CASE( "R15-C C07: the default gate (0) keeps the historical behavior bit-identically",
           "[models][tile][r15-c]" )
{
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "cov0.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 1, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f );
  builder.withNoData( -9999.0 );
  builder.withRect( 1, 0, 0, 16, 32, -9999.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );

  ModelInfo model;
  model.name = "r15c-cov0";
  model.framework = "r15c-plane";
  model.task = "segmentation";
  model.tiling.tileSize = 32;
  model.output.classes = { "change" };
  model.output.type = "raster";

  auto runtime = std::make_shared<PlaneRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "cov0_out.tif" ) );

  const TileInferenceStats stats =
    engine.run( input.toStdString(), {}, out.toStdString(), context, {} );

  CHECK( stats.tilesSkippedNoData == 0 );
  int width = 0;
  int height = 0;
  const std::vector<float> band = readBand( out, 1, width, height );
  const auto at = [ & ]( int x, int y ) {
    return band[static_cast<std::size_t>( y ) * width + x];
  };
  // NoData pixels are restored; valid pixels carry the prediction.
  CHECK( std::isnan( at( 4, 4 ) ) );
  CHECK( at( 20, 4 ) == 0.5f );
  CHECK( at( 40, 40 ) == 0.5f );
}
