// test_r29_t01_multi_feather.cpp — R29-T01 WP07: feather blending for the
// MULTI-INPUT engine (probability stack), reusing the single-input M5
// semantics. Oracles are independent: a step raster + a fake runtime that
// predicts window-centered constants makes both the hard-edge stitch and
// the cosine-weighted average exactly computable by hand (mirrors the
// single-input M5 test in test_model_runtime_9.cpp). Also pins: the A4
// NoData-wins contract (deferred all-nodata cores are never inpainted by a
// neighbor's halo), the blended uncertainty band, and the typed refusals
// for unsupported combinations (pad, strided heads via geometry, halo).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelInputContract;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Multi-input fake mirroring the single-input StepConstantRuntime: predicts
/// TWO class planes whose constant equals the value the LAST feed carries at
/// the fed window's center. Two tiles over a step raster predict different
/// constants, so seam behavior (hard step vs cosine blend) is exactly
/// observable per band.
class StepConstantMultiRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r29-fake"; }
    std::string backendName() const override { return "r29step"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r29step"; }

    bool supportsMultiInput() const override { return true; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      const TensorBlob &last = inputs.back().second;
      const std::int64_t B = last.shape[0];
      const std::int64_t C = last.shape[1];
      const std::int64_t H = last.shape[2];
      const std::int64_t W = last.shape[3];
      // Window-center value of the LAST channel (batch 0 — uniform windows
      // make every batch item's center identical for the step raster).
      const float v = last.dataFloat32()[( ( 0 * C + C - 1 ) * H + H / 2 ) * W + W / 2];
      TensorBlob out;
      out.shape = { B, 2, H, W };
      out.dtype = TensorDType::Float32;
      out.bytes.assign( static_cast<std::size_t>( B * 2 * H * W ) * sizeof( float ), 0 );
      float *outData = reinterpret_cast<float *>( out.bytes.data() );
      for ( std::int64_t b = 0; b < B; ++b )
        for ( std::int64_t p = 0; p < 2; ++p )
          for ( std::int64_t i = 0; i < H * W; ++i )
            outData[( static_cast<std::size_t>( b * 2 + p ) * H ) * W + i] = v;
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    cv::Mat infer( const cv::Mat & ) override
    {
      throw std::logic_error( "multi-input fake driven through the single-input path" );
    }
};

std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds =
    static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
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

ModelInfo twoInputStepModel( int tile, int halo, const std::string &uncertainty = {} )
{
  ModelInfo model;
  model.name = "r29-dual-step";
  model.framework = "r29-fake";
  model.task = "change_detection";
  model.tiling.tileSize = tile;
  model.tiling.halo = halo;
  ModelInputContract before;
  before.name = "before";
  ModelInputContract after;
  after.name = "after";
  model.inputs = { before, after };
  model.input = before;
  // The fake emits two class planes — the declared classes must match
  // (one channel per class is the raster head contract).
  model.output.classes = { "stable", "change" };
  if ( !uncertainty.empty() )
    model.output.uncertainty = uncertainty;
  return model;
}

} // namespace

TEST_CASE( "R29-T01: multi-input feather blends the halo overlap with cosine weights",
           "[models][r29t01][multi-feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // Step raster: 10 left half, 20 right half (the M5 fixture). Both feeds
  // carry the same grid — co-registered by construction; the fake reads the
  // LAST feed ("after").
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 16, 1, GDT_Float32 );
  builder.withRect( 1, 0, 0, 15, 15, 10.0f );
  builder.withRect( 1, 16, 0, 31, 15, 20.0f );
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "step.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );
  // A second, constant feed: proves BOTH feeds flow through the same window
  // geometry (the fake's output tracks only the last feed, but the read path
  // covers every feed).
  sicnu::testing::RsSyntheticRasterBuilder flat( 32, 16, 1, GDT_Float32 );
  flat.withConstantValue( 1, 7.0f );
  const QString before = flat.writeToDisk( dir.filePath( QStringLiteral( "flat.tif" ) ) );
  REQUIRE_FALSE( before.isEmpty() );

  auto runtime = std::make_shared<StepConstantMultiRuntime>();

  TileInferenceRunOptions none;
  none.blend = TileBlend::None;
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;

  const QString plainPath = dir.filePath( QStringLiteral( "plain.tif" ) );
  const QString blendedPath = dir.filePath( QStringLiteral( "blended.tif" ) );
  {
    TileInferenceEngine engine( twoInputStepModel( 16, 4 ), runtime );
    RSOperatorContext context;
    std::vector<NamedRasterFeed> feeds{
      { "before", { before.toStdString() }, {}, {}, {}, {} },
      { "after", { raster.toStdString() }, {}, {}, {}, {} },
    };
    engine.runMultiInput( feeds, plainPath.toStdString(), context, none );
  }
  TileInferenceStats stats;
  {
    TileInferenceEngine engine( twoInputStepModel( 16, 4 ), runtime );
    RSOperatorContext context;
    std::vector<NamedRasterFeed> feeds{
      { "before", { before.toStdString() }, {}, {}, {}, {} },
      { "after", { raster.toStdString() }, {}, {}, {}, {} },
    };
    stats = engine.runMultiInput( feeds, blendedPath.toStdString(), context, feather );
  }

  // Grid + band contract: probability stack of the 2-plane head on the input
  // grid — both stitch modes.
  CHECK( stats.outWidth == 32 );
  CHECK( stats.outHeight == 16 );
  CHECK( stats.outBands == 2 );
  CHECK( stats.headChannels == std::vector<int>{ 2 } );
  CHECK( stats.tilesProcessed > 0 );

  int w = 0;
  int h = 0;
  const std::vector<float> plain = readBand( plainPath, 1, w, h );
  const std::vector<float> blended = readBand( blendedPath, 1, w, h );
  const std::vector<float> blendedBand2 = readBand( blendedPath, 2, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 16 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  // Hard-edge stitch (the historical multi-input behavior is unchanged):
  // the seam at x=16 steps 10 → 20.
  CHECK( at( plain, 8, 8 ) == 10.0f );
  CHECK( at( plain, 15, 8 ) == 10.0f );
  CHECK( at( plain, 24, 8 ) == 20.0f );

  // Feather: pure cores keep their constants…
  CHECK( at( blended, 8, 8 ) == 10.0f );
  CHECK( at( blended, 24, 8 ) == 20.0f );
  // …the overlap averages with the cosine ramp. x=14, row 8: tile A core
  // weight 1; tile B halo distance 2/4 → wB = 0.5·(1+cos(π/2)) = 0.5:
  //   (10·1 + 20·0.5) / 1.5 = 40/3
  CHECK( at( blended, 14, 8 ) == Catch::Approx( 40.0f / 3.0f ).margin( 1e-3 ) );
  // x=18: tile A halo distance 3/4 → wA ≈ 0.146447; tile B core weight 1.
  const double wA = 0.5 * ( 1.0 + std::cos( 3.0 * 3.14159265358979323846 / 4.0 ) );
  CHECK( at( blended, 18, 8 ) == Catch::Approx( ( 10.0 * wA + 20.0 ) / ( wA + 1.0 ) ).margin( 1e-3 ) );
  // Band 2 carries the same constant per window → the same blend (per-slot
  // ledgers normalize identically for identical values, #1706).
  CHECK( at( blendedBand2, 14, 8 ) == Catch::Approx( 40.0f / 3.0f ).margin( 1e-3 ) );
}

TEST_CASE( "R29-T01: multi-input feather keeps NoData cores NoData (A4) and blends "
           "the uncertainty band",
           "[models][r29t01][multi-feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // Left half valid 10s, right half the NoData sentinel (default fill 0):
  // the two right-hand tiles defer as all-nodata; the seam column x=16
  // mixes a valid tile with a deferred neighbor whose halo WOULD have
  // predicted 10 over NoData ground.
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 16, 1, GDT_Float32 );
  builder.withNoData( 0.0 );
  builder.withRect( 1, 0, 0, 15, 15, 10.0f );
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "half.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  auto runtime = std::make_shared<StepConstantMultiRuntime>();
  TileInferenceEngine engine( twoInputStepModel( 16, 4, "margin" ), runtime );
  RSOperatorContext context;
  std::vector<NamedRasterFeed> feeds{
    { "before", { raster.toStdString() }, {}, {}, {}, {} },
    { "after", { raster.toStdString() }, {}, {}, {}, {} },
  };
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  const TileInferenceStats stats =
    engine.runMultiInput( feeds, out.toStdString(), context, feather );
  // 2 class planes + 1 margin band.
  CHECK( stats.outBands == 3 );

  int w = 0;
  int h = 0;
  const std::vector<float> band1 = readBand( out, 1, w, h );
  const std::vector<float> margin = readBand( out, 3, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 16 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  // Valid left half: constant 10 predictions (window centers sit on 10).
  CHECK( at( band1, 4, 8 ) == 10.0f );
  // The margin of two identical planes is 0 (softmax 0.5/0.5) — the
  // uncertainty band blended a constant 0 and must publish it, not NaN.
  CHECK( at( margin, 4, 8 ) == Catch::Approx( 0.0f ).margin( 1e-6 ) );
  // NoData ground stays NoData on EVERY band — including pixels a valid
  // neighbor's halo nominally covered (A4: cores are disjoint; a marked
  // pixel is NoData even where a neighbor predicted a finite value).
  for ( int x = 18; x < 32; x += 3 )
  {
    CHECK( std::isnan( at( band1, x, 8 ) ) );
    CHECK( std::isnan( at( margin, x, 8 ) ) );
  }
  // Deferred tiles are visible in the honest skip counter.
  CHECK( stats.tilesSkippedNoData > 0 );
}

TEST_CASE( "R29-T01: multi-input feather typed-refuses unsupported combinations",
           "[models][r29t01][multi-feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 16, 1, GDT_Float32 );
  builder.withConstantValue( 1, 5.0f );
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "c.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  auto runtime = std::make_shared<StepConstantMultiRuntime>();
  std::vector<NamedRasterFeed> feeds{
    { "before", { raster.toStdString() }, {}, {}, {}, {} },
    { "after", { raster.toStdString() }, {}, {}, {}, {} },
  };
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;

  SECTION( "no halo" )
  {
    TileInferenceEngine engine( twoInputStepModel( 16, 0 ), runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_AS(
      engine.runMultiInput( feeds, dir.filePath( QStringLiteral( "o.tif" ) ).toStdString(),
                            context, feather ),
      RSOperatorError );
  }
  SECTION( "preprocess.pad on the global contract" )
  {
    ModelInfo model = twoInputStepModel( 16, 4 );
    model.preprocess.pad = 2;
    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_AS(
      engine.runMultiInput( feeds, dir.filePath( QStringLiteral( "o.tif" ) ).toStdString(),
                            context, feather ),
      RSOperatorError );
  }
  SECTION( "preprocess.pad on a per-input override" )
  {
    ModelInfo model = twoInputStepModel( 16, 4 );
    model.inputs[1].preprocessDeclared = true;
    model.inputs[1].preprocess.pad = 1;
    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_AS(
      engine.runMultiInput( feeds, dir.filePath( QStringLiteral( "o.tif" ) ).toStdString(),
                            context, feather ),
      RSOperatorError );
  }
  SECTION( "derived output formats stay refused (probability stack only)" )
  {
    ModelInfo model = twoInputStepModel( 16, 4 );
    model.output.format = "labels";
    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_AS(
      engine.runMultiInput( feeds, dir.filePath( QStringLiteral( "o.tif" ) ).toStdString(),
                            context, feather ),
      RSOperatorError );
  }
}
