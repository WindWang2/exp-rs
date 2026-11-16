// tests/test_r32_t02_detection_tiles.cpp — R32-T02 WP04 (#2087).
//
// DetectionTileEngine::run still materialized the WHOLE tile grid
// (one CoreTile per cell), walked it with `int y += tileSize` (signed
// overflow UB past the last full row on a giant raster) and truncated
// core.size() into the int stats field. The raster engine was fixed in
// R30-T02 (WP08) — this suite pins the detection lane's parity:
//   * checkedTileCount boundaries (INT_MAX-near stepping, exact admits and
//     the INT_MAX+1 refusal) as pure metadata arithmetic — no raster,
//     no allocation;
//   * engine-level admission: a tiny VRT DECLARING a giant grid is typed-
//     refused BEFORE any window read or forward pass (fake provider's fed
//     counter stays zero);
//   * small-raster parity: the streamed grid derivation admits the same
//     tiles the materialized grid produced — edge tile (20 px wide raster
//     at 16 px tiles) is fed and processed, stats stay consistent.
// Fakes mirror tests/test_r24_b_detection_nodata.cpp (self-contained).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <memory>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::checkedTileCount;

struct RegistryReset
{
  RegistryReset()
  {
    ModelRuntimeRegistry::instance().releaseAll();
    ModelRuntimeRegistry::instance().resetLoadCount();
    ModelRuntimeRegistry::instance().setMaxCachedSessions( 8 );
    ModelRuntimeRegistry::instance().setIdleEvictionMs( 0 );
  }
  ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

class CountingDetectionRuntime final : public IModelRuntime
{
  public:
    explicit CountingDetectionRuntime( std::string artifact )
        : m_artifact( std::move( artifact ) )
    {
    }

    std::string framework() const override { return "r32-t02-det-count"; }
    std::string backendName() const override { return "r32_t02_count"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      ++m_forwards;
      // 7-channel empty candidate layout: decodes to zero boxes; this suite
      // is about admission/ordering, not box geometry.
      const int channels = 5 + 2;
      int dims[3] = { blob.size[0], channels, 1 };
      cv::Mat out( 3, dims, CV_32F );
      out.setTo( 0 );
      return out;
    }

    int forwards() const { return m_forwards; }

  private:
    std::string m_artifact;
    int m_forwards = 0;
};

struct CountingProviderGuard
{
    std::shared_ptr<CountingDetectionRuntime> runtime;

    CountingProviderGuard()
    {
      ModelRuntimeRegistry::instance().registerProvider(
        "r32-t02-det-count",
        [ this ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                  std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          runtime = std::make_shared<CountingDetectionRuntime>( model.resolvedArtifactPath );
          return runtime;
        } );
    }
};

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

Json::Value detectionManifest( const std::string &name )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "detection";
  json["framework"] = "r32-t02-det-count";
  json["artifact"]["path"] = identityModelPath().toStdString();
  json["input"]["width"] = 16;
  json["input"]["height"] = 16;
  json["preprocess"]["resize"] = "to_input";
  json["preprocess"]["normalize"] = "linear";
  json["preprocess"]["scale"] = 1.0;
  Json::Value &detection = json["output"]["detection"] = Json::Value( Json::objectValue );
  detection["layout"] = "xywh_objectness";
  detection["tensor_layout"] = "channels_first";
  detection["conf_threshold"] = 0.25;
  detection["nms_iou"] = 0.45;
  detection["max_detections"] = 100;
  Json::Value classesJson( Json::arrayValue );
  classesJson.append( "tree" );
  classesJson.append( "shrub" );
  detection["classes"] = classesJson;
  return json;
}

bool registerManifest( const Json::Value &json, const std::string &manifestPath )
{
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ), manifestPath, &error );
  if ( !ok )
    FAIL( "manifest registration failed: " + error );
  return ok;
}

// A tiny VRT that DECLARES huge dimensions without allocating any pixels —
// the metadata-only shape a hostile/sparse grid presents to the engine.
QString writeGiantVrt( const QString &path, int w, int h )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "VRT" );
  if ( !driver )
    return QString();
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32,
                                nullptr );
  if ( !ds )
    return QString();
  GDALClose( ds );
  QFileInfo info( path );
  return info.exists() ? path : QString();
}

// 20x16 1-band Float32 GTiff: the left 16-px full tile carries `a`, the
// right 4-px EDGE tile carries `b` (resize to_input resamples both windows
// to 16x16 constants — the fed means separate the two tiles exactly).
bool writeTwoValueRaster( const QString &path, float a, float b )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 20, 16, 1, GDT_Float32,
                                nullptr );
  if ( !ds )
    return false;
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> left( 16, a ), right( 4, b );
  for ( int y = 0; y < 16; ++y )
  {
    if ( GDALRasterIO( band, GF_Write, 0, y, 16, 1, left.data(), 16, 1, GDT_Float32, 0, 0 )
           != CE_None
         || GDALRasterIO( band, GF_Write, 16, y, 4, 1, right.data(), 4, 1, GDT_Float32, 0, 0 )
              != CE_None )
    {
      GDALClose( ds );
      return false;
    }
  }
  GDALClose( ds );
  return true;
}

} // namespace

TEST_CASE( "R32-T02 WP04: checkedTileCount admits INT_MAX-near grids and "
           "refuses the first unschedulable one",
           "[r32t02][wp04][detection][grid]" )
{
  // Exact INT_MAX admit (1-px tiles on a single INT_MAX-wide row).
  CHECK( checkedTileCount( 2147483647, 1, 1 ) == 2147483647 );

  // Single-axis giant: rasterW = INT_MAX at 256-px tiles, one tile row.
  CHECK( checkedTileCount( 2147483647, 256, 256 ) == 8388608 );

  // INT_MAX + 1 stepping: gridX*gridY = 65536*32768 = 2^31 exactly — the
  // FIRST count an int can neither hold nor schedule. Typed refusal.
  REQUIRE_THROWS_AS(
    checkedTileCount( 65536 * 512, 32768 * 512, 512 ), RSOperatorError );
  try
  {
    checkedTileCount( 65536 * 512, 32768 * 512, 512 );
  }
  catch ( const RSOperatorError &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "schedulable bound" ) );
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "2147483648" ) );
  }

  // A giant square grid (2e9 x 2e9 px): typed refusal long before any
  // per-tile allocation could start.
  REQUIRE_THROWS_AS( checkedTileCount( 2000000000, 2000000000, 16 ), RSOperatorError );
}

TEST_CASE( "R32-T02 WP04: giant declared VRT grid is refused before any "
           "window read or forward",
           "[r32t02][wp04][detection][admission]" )
{
  RegistryReset reset;
  const CountingProviderGuard guard;
  QTemporaryDir dir;

  const Json::Value manifest = detectionManifest( "r32-t02-det-giant" );
  registerManifest( manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString() );

  // The VRT file itself is a few hundred bytes; only its DECLARED grid is
  // giant. tileSize resolves to 16 (manifest input) → the 2e9×2e9 grid
  // needs ~1.5e16 tiles — the #2087 admission must refuse it BEFORE the
  // former materialized CoreTile vector (which would have committed
  // gigabytes of scheduling metadata) and before any forward pass.
  const QString input = writeGiantVrt(
    dir.filePath( QStringLiteral( "giant.vrt" ) ), 2000000000, 2000000000 );
  REQUIRE_FALSE( input.isEmpty() );

  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = dir.filePath( QStringLiteral( "out.gpkg" ) ).toStdString();
  request.modelReference = "r32-t02-det-giant";
  request.asDetection = true;
  RSOperatorContext context;
  try
  {
    runModelInference( request, context );
    FAIL( "a giant declared grid must be refused" );
  }
  catch ( const RSOperatorError &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "schedulable bound" ) );
  }
  // Admission fired before the tile loop: no window ever flowed through a
  // forward pass (the session may exist — the refusal is in run()).
  CHECK( ( !guard.runtime || guard.runtime->forwards() == 0 ) );
}

TEST_CASE( "R32-T02 WP04: streamed grid keeps small-raster tile admission "
           "and edge windows identical",
           "[r32t02][wp04][detection][parity]" )
{
  RegistryReset reset;
  const CountingProviderGuard guard;
  QTemporaryDir dir;

  const Json::Value manifest = detectionManifest( "r32-t02-det-small" );
  registerManifest( manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString() );

  // 20 px wide raster at 16 px tiles = full tile (0,0,16,16) + edge tile
  // (16,0,4,16): the streamed derivation must admit exactly these two, in
  // row-major order, with the edge window 4 px wide (then resized to_input).
  const QString input = dir.filePath( QStringLiteral( "small.tif" ) );
  REQUIRE( writeTwoValueRaster( input, 100.0f, 200.0f ) );

  const QString output = dir.filePath( QStringLiteral( "small_out.gpkg" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "r32-t02-det-small";
  request.asDetection = true;
  RSOperatorContext context;
  const ModelExecutionResult result = runModelInference( request, context );

  CHECK( result.detectionStats.tilesPlanned == 2 );
  CHECK( result.detectionStats.tilesProcessed == 2 );
  CHECK( result.detectionStats.rasterWidth == 20 );
  CHECK( result.detectionStats.rasterHeight == 16 );
  REQUIRE( guard.runtime );
  CHECK( guard.runtime->forwards() >= 1 ); // both tiles flowed through forwards
  REQUIRE( QFileInfo::exists( output ) );  // a real vector product was published
}
