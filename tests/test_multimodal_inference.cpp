// tests/test_multimodal_inference.cpp — Platform 7.0 multimodal / temporal
// tiled inference: multi-file same-grid assembly, named tensor bind,
// temporal stack windowing with missing-frame policies, the valid-coverage
// gate and the OOM ladder semantics over the multi-input path.
// Synthetic rasters only (<= 64 px) via the shared raster builder.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_execution_service.h"
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
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelInputContract;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Deterministic multi-input fake: outputs = sum over feeds of the LAST
/// channel of each named tensor. Shapes follow the first feed's geometry;
/// the output is (B, 1, H, W) float32. Records the fed names to assert the
/// named bind contract.
class ChangeFakeRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m4-fake"; }
    std::string backendName() const override { return "change-fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m4"; }

    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
    bool supportsMultiInput() const override { return true; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      fedNames.clear();
      for ( const auto &nt : inputs )
        fedNames.push_back( nt.first );

      const TensorBlob &first = inputs.front().second;
      REQUIRE( first.rank() == 4 );
      const std::int64_t B = first.shape[0];
      const std::int64_t H = first.shape[2];
      const std::int64_t W = first.shape[3];

      TensorBlob out;
      out.shape = { B, 1, H, W };
      out.dtype = TensorDType::Float32;
      out.bytes.assign( static_cast<std::size_t>( B * H * W ) * sizeof( float ), 0 );
      float *outData = reinterpret_cast<float *>( out.bytes.data() );
      for ( std::size_t bi = 0; bi < inputs.size(); ++bi )
      {
        const TensorBlob &in = inputs[bi].second;
        REQUIRE( in.rank() == 4 );
        REQUIRE( in.shape[0] == B );
        REQUIRE( in.shape[2] == H );
        REQUIRE( in.shape[3] == W );
        const float *inData = in.dataFloat32();
        const std::int64_t C = in.shape[1];
        for ( std::int64_t b = 0; b < B; ++b )
          for ( std::int64_t y = 0; y < H; ++y )
            for ( std::int64_t x = 0; x < W; ++x )
            {
              // Known answer: the LAST channel of each feed sums up.
              const std::int64_t idx = ( ( b * C + ( C - 1 ) ) * H + y ) * W + x;
              outData[( b * H + y ) * W + x] += inData[idx];
            }
      }
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    std::vector<std::string> fedNames;
};

/// Reads band 1 of a float raster for pixel assertions.
std::vector<float> readBand( const QString &path, int &width, int &height )
{
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  GDALRasterBand *band = ds->GetRasterBand( 1 );
  REQUIRE( band->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height, GDT_Float32,
                           0, 0 ) == CE_None );
  GDALClose( ds );
  return data;
}

ModelInfo changeModel( int tile = 16, int halo = 0, double minCoverage = 0.0 )
{
  ModelInfo model;
  model.name = "m4-change";
  model.framework = "m4-fake";
  model.task = "change_detection";
  model.tiling.tileSize = tile;
  model.tiling.halo = halo;
  model.tiling.minValidCoverage = minCoverage;
  ModelInputContract before;
  before.name = "before";
  ModelInputContract after;
  after.name = "after";
  model.inputs = { before, after };
  model.input = before;
  model.output.classes = { "change" };
  model.output.tensorNames = { "change" };
  return model;
}

} // namespace

TEST_CASE( "multi-input same-grid tiling produces the known answer", "[models][multimodal]" )
{
  QTemporaryDir dir;
  auto before = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
                  .withConstantValue( 1, 5.0f )
                  .withConstantValue( 2, 7.0f )
                  .writeToDisk( dir.filePath( QStringLiteral( "before.tif" ) ) );
  auto after = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
                 .withConstantValue( 1, 1.0f )
                 .withConstantValue( 2, 3.0f )
                 .writeToDisk( dir.filePath( QStringLiteral( "after.tif" ) ) );
  REQUIRE_FALSE( before.isEmpty() );
  REQUIRE_FALSE( after.isEmpty() );

  auto runtime = std::make_shared<ChangeFakeRuntime>();
  TileInferenceEngine engine( changeModel( /*tile*/ 16, /*halo*/ 4 ), runtime );
  RSOperatorContext context;

  std::vector<NamedRasterFeed> feeds = {
    NamedRasterFeed{ "before", { before.toStdString() }, {} },
    NamedRasterFeed{ "after", { after.toStdString() }, {} }
  };
  const QString out = dir.filePath( QStringLiteral( "change.tif" ) );
  const TileInferenceStats stats = engine.runMultiInput( feeds, out.toStdString(), context, {} );

  CHECK( runtime->fedNames == std::vector<std::string>( { "before", "after" } ) );
  CHECK( stats.tilesProcessed == 4 );
  CHECK( stats.outBands == 1 );

  int width = 0;
  int height = 0;
  const std::vector<float> data = readBand( out, width, height );
  REQUIRE( width == 32 );
  REQUIRE( height == 32 );
  // Last channels: after=3 + before=7 → 10 everywhere (grid-preserving,
  // halo-cropped stitch, one value per pixel).
  for ( std::size_t i = 0; i < data.size(); ++i )
    CHECK( data[i] == Catch::Approx( 10.0f ).margin( 1e-4 ) );
}

TEST_CASE( "misaligned feeds are refused, never warped", "[models][multimodal]" )
{
  QTemporaryDir dir;
  auto primary = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withConstantValue( 1, 1.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "a.tif" ) ) );
  auto shifted = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withGeoTransform( 100.0, 1.0, 2000.0, -1.0 ) // different origin
                   .withConstantValue( 1, 2.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "b.tif" ) ) );
  auto smaller = sicnu::testing::RsSyntheticRasterBuilder( 16, 32, 1, GDT_Float32 )
                   .withConstantValue( 1, 2.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "c.tif" ) ) );

  auto runtime = std::make_shared<ChangeFakeRuntime>();
  TileInferenceEngine engine( changeModel(), runtime );
  RSOperatorContext context;

  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  REQUIRE_THROWS_AS( engine.runMultiInput(
                       { NamedRasterFeed{ "before", { primary.toStdString() }, {} },
                         NamedRasterFeed{ "after", { shifted.toStdString() }, {} } },
                       out.toStdString(), context, {} ),
                     RSOperatorError );
  REQUIRE_THROWS_AS( engine.runMultiInput(
                       { NamedRasterFeed{ "before", { primary.toStdString() }, {} },
                         NamedRasterFeed{ "after", { smaller.toStdString() }, {} } },
                       out.toStdString(), context, {} ),
                     RSOperatorError );
  try
  {
    engine.runMultiInput( { NamedRasterFeed{ "before", { primary.toStdString() }, {} },
                            NamedRasterFeed{ "after", { shifted.toStdString() }, {} } },
                          out.toStdString(), context, {} );
  }
  catch ( const RSOperatorError &e )
  {
    CHECK( e.code() == sicnu::operators::ErrorCode::InvalidInputData );
    CHECK_THAT( e.message(), Catch::Matchers::ContainsSubstring( "not co-registered" ) );
  }
  // No output survives a refused run (atomic publication contract).
  CHECK_FALSE( QFile::exists( out ) );
}

TEST_CASE( "temporal stacking folds frames into channels and zero-fills missing ones", "[models][multimodal]" )
{
  QTemporaryDir dir;
  // Frames 1/2/3 carry the values 1/2/3; the fake reads the LAST stacked
  // channel, so channel order (frame-major) is pinned by the expectation.
  const auto f1 = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                    .withConstantValue( 1, 1.0f )
                    .writeToDisk( dir.filePath( QStringLiteral( "t1.tif" ) ) );
  const auto f2 = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                    .withConstantValue( 1, 2.0f )
                    .writeToDisk( dir.filePath( QStringLiteral( "t2.tif" ) ) );
  const auto f3 = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                    .withConstantValue( 1, 3.0f )
                    .writeToDisk( dir.filePath( QStringLiteral( "t3.tif" ) ) );

  ModelInfo model = changeModel( /*tile*/ 16, /*halo*/ 0 );
  ModelInputContract series;
  series.name = "series";
  series.temporalLength = 3;
  series.missingTimestep = "zero";
  model.inputs = { series };
  model.input = series;

  auto runtime = std::make_shared<ChangeFakeRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;

  // Full stack: the last stacked channel is frame 3.
  const QString out = dir.filePath( QStringLiteral( "temporal.tif" ) );
  engine.runMultiInput(
    { NamedRasterFeed{ "series",
                       { f1.toStdString(), f2.toStdString(), f3.toStdString() }, {} } },
    out.toStdString(), context, {} );
  int width = 0;
  int height = 0;
  const std::vector<float> data = readBand( out, width, height );
  for ( std::size_t i = 0; i < data.size(); ++i )
    CHECK( data[i] == Catch::Approx( 3.0f ).margin( 1e-4 ) );

  // Missing third frame: the zero-fill IS the last channel → 0 everywhere
  // (a 1 or 2 would mean the frames stacked in the wrong order).
  const QString outMissing = dir.filePath( QStringLiteral( "temporal-missing.tif" ) );
  engine.runMultiInput(
    { NamedRasterFeed{ "series", { f1.toStdString(), f2.toStdString() }, {} } },
    outMissing.toStdString(), context, {} );
  const std::vector<float> missing = readBand( outMissing, width, height );
  for ( std::size_t i = 0; i < missing.size(); ++i )
    CHECK( missing[i] == 0.0f );
}

TEST_CASE( "missing frames refuse when the policy says refuse", "[models][multimodal]" )
{
  QTemporaryDir dir;
  const auto f1 = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                    .withConstantValue( 1, 1.0f )
                    .writeToDisk( dir.filePath( QStringLiteral( "t1.tif" ) ) );

  ModelInfo model = changeModel();
  ModelInputContract series;
  series.name = "series";
  series.temporalLength = 4;
  series.missingTimestep = "refuse"; // the documented default
  model.inputs = { series };
  model.input = series;

  auto runtime = std::make_shared<ChangeFakeRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;

  REQUIRE_THROWS_AS( engine.runMultiInput(
                       { NamedRasterFeed{ "series", { f1.toStdString() }, {} } },
                       dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(), context, {} ),
                     RSOperatorError );
}

TEST_CASE( "the valid-coverage gate skips low-coverage tiles as NoData", "[models][multimodal]" )
{
  QTemporaryDir dir;
  // Two 32px tiles side by side; the LEFT tile is 100% NaN.
  auto raster = sicnu::testing::RsSyntheticRasterBuilder( 64, 32, 1, GDT_Float32 )
                  .withConstantValue( 1, 2.0f )
                  .withRect( 1, 0, 0, 32, 32, std::numeric_limits<float>::quiet_NaN() )
                  .writeToDisk( dir.filePath( QStringLiteral( "half.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  ModelInfo model = changeModel( /*tile*/ 32, /*halo*/ 0, /*minCoverage*/ 0.9 );
  model.inputs = { { "before" }, { "after" } };
  model.input = model.inputs[0];

  auto runtime = std::make_shared<ChangeFakeRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;

  const QString out = dir.filePath( QStringLiteral( "gated.tif" ) );
  const TileInferenceStats stats = engine.runMultiInput(
    { NamedRasterFeed{ "before", { raster.toStdString() }, {} },
      NamedRasterFeed{ "after", { raster.toStdString() }, {} } },
    out.toStdString(), context, {} );
  // Two tiles: the all-NaN left tile skips, the right tile runs.
  CHECK( stats.tilesSkippedNoData == 1 );

  int width = 0;
  int height = 0;
  const std::vector<float> data = readBand( out, width, height );
  REQUIRE( width == 64 );
  // Left tile skipped → NoData; right tile kept → 2 + 2.
  for ( int y = 0; y < 32; ++y )
    for ( int x = 0; x < 64; ++x )
    {
      const float v = data[static_cast<std::size_t>( y ) * 64 + x];
      if ( x < 32 )
        CHECK( std::isnan( v ) );
      else
        CHECK( v == Catch::Approx( 4.0f ).margin( 1e-4 ) );
    }
}

TEST_CASE( "the single execution seam routes named feeds and refuses misuse", "[models][multimodal]" )
{
  QTemporaryDir dir;
  auto before = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                  .withConstantValue( 1, 5.0f )
                  .writeToDisk( dir.filePath( QStringLiteral( "before.tif" ) ) );
  auto after = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                 .withConstantValue( 1, 3.0f )
                 .writeToDisk( dir.filePath( QStringLiteral( "after.tif" ) ) );

  // Registration: a fake provider the service resolves through the SAME
  // registry seam; the model contract is registered programmatically and
  // carries a real (tiny) artifact so readiness verifies to Ready.
  const QString weights = dir.filePath( QStringLiteral( "weights.bin" ) );
  {
    QFile wf( weights );
    REQUIRE( wf.open( QIODevice::WriteOnly ) );
    wf.write( QByteArray( "m4-fake-weights" ) );
  }
  auto &registry = ModelRuntimeRegistry::instance();
  registry.registerProvider(
    "m4-fake",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<ChangeFakeRuntime>();
    },
    ProviderTraits{} );

  auto &catalog = ModelCatalog::instance();
  std::string manifestJson = R"({
      "name": "m4-service-model",
      "task": "change_detection",
      "framework": "m4-fake",
      "artifact": { "path": "WEIGHTS" },
      "inputs": [ { "name": "before" }, { "name": "after" } ],
      "output": { "type": "raster", "tensor_names": ["change"], "classes": ["change"] },
      "tiling": { "tile_size": 16 }
  })";
  const std::size_t placeholder = manifestJson.find( "WEIGHTS" );
  REQUIRE( placeholder != std::string::npos );
  manifestJson.replace( placeholder, 7, QDir( weights ).absolutePath().toStdString() );
  REQUIRE( catalog.registerManifestJson(
    manifestJson, QStringLiteral( "%1/model.json" ).arg( dir.path() ).toStdString() ) );

  RSOperatorContext context;
  ModelExecutionRequest request;
  request.modelReference = "m4-service-model";
  request.outputPath = dir.filePath( QStringLiteral( "service.tif" ) ).toStdString();
  request.namedInputs = { NamedRasterFeed{ "before", { before.toStdString() }, {} },
                          NamedRasterFeed{ "after", { after.toStdString() }, {} } };
  const ModelExecutionResult result = runModelInference( request, context );
  CHECK( result.rasterStats.tilesProcessed == 1 );

  int width = 0;
  int height = 0;
  const std::vector<float> data = readBand( dir.filePath( QStringLiteral( "service.tif" ) ), width, height );
  CHECK( data[0] == Catch::Approx( 8.0f ).margin( 1e-4 ) ); // 5 + 3

  // Misuse: a multi-input model run through the single-input path refuses.
  ModelExecutionRequest bad = request;
  bad.namedInputs.clear();
  bad.inputPath = before.toStdString();
  REQUIRE_THROWS_AS( runModelInference( bad, context ), RSOperatorError );
  try
  {
    runModelInference( bad, context );
  }
  catch ( const RSOperatorError &e )
  {
    CHECK_THAT( e.message(), Catch::Matchers::ContainsSubstring( "named inputs" ) );
  }
}
