// test_r30_t02_working_set.cpp — R30-T02 WP07/WP08: the multi-input batch
// budget accounts EVERY coexisting feed, and the tile grid is checked before
// any scheduling metadata is allocated.
//   * batch sizing: a two-feed (3+2 channel) run sizes its batch exactly
//     like a single-feed 5-channel run on the same pinned VRAM budget — the
//     historical max-feed basis produced a LARGER batch (2x for equal feeds);
//   * the plan's working-set estimate follows the same Σ-feeds/window-side
//     arithmetic (bigger window ⇒ bigger estimate, more feeds ⇒ bigger
//     estimate);
//   * tile-count bound: a sparse VRT declaring a giant grid is refused
//     typed, allocation-free, by BOTH the engine and the plan;
//   * window bound: a tile+halo window above kMaxWindowPx refuses in both.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QTemporaryDir>
#include <QFile>

#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace sicnu::operators::runtime;
using sicnu::operators::ErrorCode;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelInputContract;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Pass-through multi-input fake (each window forwarded, two class planes).
class PassthroughMultiRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r30-ws"; }
    std::string backendName() const override { return "r30ws"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r30ws"; }
    bool supportsMultiInput() const override { return true; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> & ) override
    {
      const TensorBlob &in = inputs.front().second;
      TensorBlob out;
      out.shape = { in.shape[0], 2, in.shape[2], in.shape[3] };
      out.dtype = TensorDType::Float32;
      out.bytes.assign( static_cast<std::size_t>( out.shape[0] * 2 * out.shape[2] * out.shape[3] )
                          * sizeof( float ),
                        0.0f );
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    cv::Mat infer( const cv::Mat &nchw ) override
    {
      int dims[4] = { nchw.size[0], 2, nchw.size[2], nchw.size[3] };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.0f ) );
      return out;
    }
};

QString raster( const QTemporaryDir &dir, const QString &name, int bands, int w = 32,
                int h = 32 )
{
  sicnu::testing::RsSyntheticRasterBuilder builder( w, h, bands, GDT_Float32 );
  for ( int b = 1; b <= bands; ++b )
    builder.withConstantValue( b, 0.25f * b );
  const QString path = builder.writeToDisk( dir.filePath( name ) );
  REQUIRE_FALSE( path.isEmpty() );
  return path;
}

/// A sparse VRT declaring a huge grid — creation is metadata-only, so the
/// ENGINE must refuse on the tile-count/window bound without allocating.
QString giantVrt( const QTemporaryDir &dir, const QString &name, int w, int h )
{
  GDALAllRegister();
  const QString path = dir.filePath( name );
  GDALDriver *vrt = GetGDALDriverManager()->GetDriverByName( "VRT" );
  REQUIRE( vrt );
  GDALDataset *ds = vrt->Create( path.toUtf8().constData(), w, h, 1, GDT_Byte, nullptr );
  REQUIRE( ds );
  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
  return path;
}

/// Registers the fake framework so the PLAN's runtime-readiness gate sees a
/// provider (the engine runs the same fake directly).
struct WorkingSetProviderGuard
{
  WorkingSetProviderGuard()
  {
    ModelRuntimeRegistry::instance().registerProvider(
      "r30-ws",
      []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) {
        struct NullRuntime final : IModelRuntime
        {
          std::string framework() const override { return "r30-ws"; }
          std::string backendName() const override { return "ws"; }
          std::string deviceName() const override { return "cpu"; }
          std::string artifactPath() const override { return "fake://ws"; }
          cv::Mat infer( const cv::Mat &nchw ) override { return nchw.clone(); }
        };
        return ModelRuntimePtr( std::make_shared<NullRuntime>() );
      },
      ProviderTraits{} );
  }
  ~WorkingSetProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

struct HardwarePin
{
  explicit HardwarePin( const ModelHardwareCapabilities &caps )
  {
    ModelRuntimeRegistry::instance().setHardwareForTest( caps );
  }
  ~HardwarePin() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE( "R30-T02: multi-feed batch budget accounts every coexisting feed",
           "[models][r30t02][workingset]" )
{
  WorkingSetProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  auto runtime = std::make_shared<PassthroughMultiRuntime>();

  // Pin a small VRAM budget so effectiveBatchSize is budget-derived:
  // batch = budgetBytes / (4 · fedW·fedH · channels · 4).
  ModelHardwareCapabilities hw;
  hw.vramBudgetMb = 1; // 1 MiB
  HardwarePin pin( hw );

  const int tileSize = 32;

  // Two feeds: 3 bands + 2 bands (Σ = 5 stacked channels).
  ModelInfo multi;
  multi.name = "r30-ws-multi";
  multi.framework = "r30-ws";
  multi.task = "segmentation";
  multi.tiling.tileSize = tileSize;
  multi.output.classes = { "a", "b" };
  ModelInputContract fa;
  fa.name = "A";
  ModelInputContract fb;
  fb.name = "B";
  multi.inputs = { fa, fb };
  multi.input = fa;

  const QString rasterA = raster( dir, QStringLiteral( "a3.tif" ), 3 );
  const QString rasterB = raster( dir, QStringLiteral( "b2.tif" ), 2 );

  TileInferenceEngine multiEngine( multi, runtime );
  RSOperatorContext context;
  std::vector<NamedRasterFeed> feeds{
    { "A", { rasterA.toStdString() }, {}, {}, {}, {} },
    { "B", { rasterB.toStdString() }, {}, {}, {}, {} },
  };
  const TileInferenceStats multiStats = multiEngine.runMultiInput(
    feeds, dir.filePath( QStringLiteral( "multi.tif" ) ).toStdString(), context,
    TileInferenceRunOptions{} );

  // Single feed with the Σ channel count (5 bands) for the same budget and
  // tile: the batch verdicts must AGREE (the historical max-feed basis made
  // the multi run claim a batch ~5/3× larger than its real working set).
  ModelInfo single;
  single.name = "r30-ws-single";
  single.framework = "r30-ws";
  single.task = "segmentation";
  single.tiling.tileSize = tileSize;
  single.output.classes = { "a", "b" };

  const QString raster5 = raster( dir, QStringLiteral( "s5.tif" ), 5 );
  TileInferenceEngine singleEngine( single, runtime );
  RSOperatorContext singleContext;
  const TileInferenceStats singleStats =
    singleEngine.run( raster5.toStdString(), {},
                      dir.filePath( QStringLiteral( "single.tif" ) ).toStdString(),
                      singleContext );

  CHECK( multiStats.batchSize == singleStats.batchSize );
  CHECK( multiStats.batchSize >= 1 );

  // And the PLAN agrees with the engine on the same Σ basis: the multi plan's
  // batch matches the 5-band single plan's batch.
  ModelExecutionRequest multiPlanRequest;
  multiPlanRequest.modelReference = multi.name;
  multiPlanRequest.outputPath = dir.filePath( QStringLiteral( "p1.tif" ) ).toStdString();
  multiPlanRequest.namedInputs = feeds;
  const InferencePlan multiPlan = planModelInference( multiPlanRequest, multi );

  ModelExecutionRequest singlePlanRequest;
  singlePlanRequest.modelReference = single.name;
  singlePlanRequest.inputPath = raster5.toStdString();
  singlePlanRequest.outputPath = dir.filePath( QStringLiteral( "p2.tif" ) ).toStdString();
  const InferencePlan singlePlan = planModelInference( singlePlanRequest, single );

  REQUIRE( multiPlan.verdict == InferencePlan::Verdict::Ready );
  REQUIRE( singlePlan.verdict == InferencePlan::Verdict::Ready );
  CHECK( multiPlan.batchSize == singlePlan.batchSize );
  CHECK( multiPlan.batchSize == multiStats.batchSize );
}

TEST_CASE( "R30-T02: the plan's working-set estimate scales with window and feeds",
           "[models][r30t02][workingset]" )
{
  WorkingSetProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const auto planFor = [ & ]( int halo, int bands ) {
    ModelInfo model;
    model.name = "r30-ws-est";
    model.framework = "r30-ws";
    model.task = "segmentation";
    model.tiling.tileSize = 32;
    model.tiling.halo = halo;
    model.output.classes = { "a", "b" };
    const QString in = raster( dir, QStringLiteral( "in.tif" ), bands );
    ModelExecutionRequest request;
    request.modelReference = model.name;
    request.inputPath = in.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "out.tif" ) ).toStdString();
    return planModelInference( request, model );
  };

  // Halo grows the fed WINDOW (tile + 2·halo per side): a 16-px halo at a
  // 32-px tile grows the estimate by (64/32)² = 4×.
  const double plain = planFor( 0, 1 ).batchWorkingSetMiB;
  const double halod = planFor( 16, 1 ).batchWorkingSetMiB;
  REQUIRE( plain > 0.0 );
  CHECK( halod == Catch::Approx( plain * 4.0 ).margin( plain * 0.01 ) );

  // More fed bands grow the estimate linearly.
  const double threeBands = planFor( 0, 3 ).batchWorkingSetMiB;
  CHECK( threeBands == Catch::Approx( plain * 3.0 ).margin( plain * 0.01 ) );
}

TEST_CASE( "R30-T02: a giant tile grid refuses typed before any allocation",
           "[models][r30t02][workingset]" )
{
  WorkingSetProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  auto runtime = std::make_shared<PassthroughMultiRuntime>();

  // tile 16 → grid 46875² ≈ 2.2e9 tiles > INT_MAX: refused allocation-free.
  // A real GeoTIFF of this extent is impossible on this host — the VRT
  // declares it for free, which is exactly the hostile-input shape.
  const QString huge = giantVrt( dir, QStringLiteral( "huge.vrt" ), 750000, 750000 );

  ModelInfo model;
  model.name = "r30-ws-huge";
  model.framework = "r30-ws";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  model.output.classes = { "a", "b" };

  // Engine: typed refusal, no product.
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  REQUIRE_THROWS_WITH(
    engine.run( huge.toStdString(), {}, dir.filePath( QStringLiteral( "huge.tif" ) ).toStdString(),
                context ),
    Catch::Matchers::ContainsSubstring( "above the schedulable bound" ) );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "huge.tif" ) ) ) );

  // Plan: the same refusal (finding + refused verdict).
  ModelExecutionRequest request;
  request.modelReference = model.name;
  request.inputPath = huge.toStdString();
  request.outputPath = dir.filePath( QStringLiteral( "huge.tif" ) ).toStdString();
  const InferencePlan plan = planModelInference( request, model );
  REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
  REQUIRE( plan.findingCode( "plan.tile_count_overflow" ) );
}

TEST_CASE( "R30-T02: an oversized window refuses typed in plan and engine",
           "[models][r30t02][workingset]" )
{
  WorkingSetProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  auto runtime = std::make_shared<PassthroughMultiRuntime>();

  // tile 140000 > kMaxWindowPx (131072): the window side itself is unbounded.
  const QString big = giantVrt( dir, QStringLiteral( "big.vrt" ), 140000, 8 );

  ModelInfo model;
  model.name = "r30-ws-window";
  model.framework = "r30-ws";
  model.task = "segmentation";
  model.tiling.tileSize = 140000;
  model.output.classes = { "a", "b" };

  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  REQUIRE_THROWS_WITH(
    engine.run( big.toStdString(), {},
                dir.filePath( QStringLiteral( "big.tif" ) ).toStdString(), context ),
    Catch::Matchers::ContainsSubstring( "exceeds the window bound" ) );

  ModelExecutionRequest request;
  request.modelReference = model.name;
  request.inputPath = big.toStdString();
  request.outputPath = dir.filePath( QStringLiteral( "big.tif" ) ).toStdString();
  const InferencePlan plan = planModelInference( request, model );
  REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
  REQUIRE( plan.findingCode( "plan.window_budget" ) );
}
