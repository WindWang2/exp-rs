// tests/test_r24_b_detection_nodata.cpp — R24-B (WP-B6, P1): declared NoData
// sentinels must never reach the detection model raw. The raster/scene lanes
// convert declared sentinels to NaN before the nodata_policy "zero" sweep
// (tile_inference_engine.cpp "Declared sentinels → NaN"); the detection lane
// swept only non-finite values, so a finite sentinel (NoDataValue=-9999)
// flowed through preprocessing into the model blob as a huge outlier —
// silently, with no error anywhere (lane parity break found by R24-B
// adversarial review). This suite pins the parity: sentinel → NaN → 0.
//
// Fakes copied in trimmed form from tests/test_ensemble_detection.cpp
// (DetectionHead / DetectionHeadRuntime / capturing runtime / manifest
// helpers) so the R24-B registration stays self-contained.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"

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
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;

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

struct DetectionHead
{
  struct Candidate
  {
    float cx, cy, w, h, obj;
    float score0, score1;
  };
  std::vector<Candidate> candidates;
};

class CapturingDetectionRuntime final : public IModelRuntime
{
  public:
    CapturingDetectionRuntime( std::string artifact, std::string framework,
                               std::shared_ptr<std::atomic<double>> sum,
                               std::shared_ptr<std::atomic<int>> count )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_sum( std::move( sum ) ), m_count( std::move( count ) )
    {
    }

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "r24_b_capture"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const cv::Mat contiguous = blob.isContinuous() ? blob : blob.clone();
      const std::size_t total = contiguous.total();
      const float *data = contiguous.ptr<float>( 0 );
      double local = 0.0;
      for ( std::size_t i = 0; i < total; ++i )
        local += data[i];
      if ( total > 0 )
      {
        m_sum->fetch_add( local );
        m_count->fetch_add( static_cast<int>( total ) );
      }
      // One low-confidence candidate keeps the decode path exercised without
      // asserting on box geometry — this suite is about the fed blob.
      const int channels = 5 + 2;
      int dims[3] = { blob.size[ 0 ], channels, 1 };
      cv::Mat out( 3, dims, CV_32F );
      out.setTo( 0 );
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    std::shared_ptr<std::atomic<double>> m_sum;
    std::shared_ptr<std::atomic<int>> m_count;
};

struct CapturingDetectionProviderGuard
{
    std::shared_ptr<std::atomic<double>> sum = std::make_shared<std::atomic<double>>( 0.0 );
    std::shared_ptr<std::atomic<int>> count = std::make_shared<std::atomic<int>>( 0 );

    CapturingDetectionProviderGuard( const std::string &framework )
    {
      ModelRuntimeRegistry::instance().registerProvider(
        framework,
        [ framework, sum = sum, count = count ]( const ModelInfo &model,
                                                  const ModelHardwareCapabilities &,
                                                  std::string *error ) -> ModelRuntimePtr {
            if ( model.resolvedArtifactPath.empty() )
            {
              if ( error )
                *error = "no resolved artifact";
              return nullptr;
            }
            return std::make_shared<CapturingDetectionRuntime>( model.resolvedArtifactPath,
                                                                framework, sum, count );
        } );
    }

    double capturedMean() const
    {
      const long long seen = count->load();
      return seen > 0 ? sum->load() / static_cast<double>( seen ) : 0.0;
    }
    long long capturedCount() const { return count->load(); }
};

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

Json::Value detectionManifest( const std::string &name, const std::string &framework )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "detection";
  json["framework"] = framework;
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

// 16x16 x 3-band Float32 GTiff: columns [0,8) carry `valid`, columns [8,16)
// carry `sentinel`; when `declareNoData` every band declares
// NoDataValue=`sentinel`, otherwise no sentinel is declared at all.
bool writeNodataRaster( const QString &path, float valid, float sentinel, bool declareNoData )
{
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 16, 16, 3, GDT_Float32,
                                nullptr );
  if ( !ds )
    return false;
  std::vector<float> rowValid( 8, valid ), rowSentinel( 8, sentinel );
  for ( int b = 1; b <= 3; ++b )
  {
    GDALRasterBandH band = GDALGetRasterBand( ds, b );
    if ( declareNoData )
      GDALSetRasterNoDataValue( band, sentinel );
    for ( int y = 0; y < 16; ++y )
    {
      if ( GDALRasterIO( band, GF_Write, 0, y, 8, 1, rowValid.data(), 8, 1, GDT_Float32, 0,
                         0 ) != CE_None
           || GDALRasterIO( band, GF_Write, 8, y, 8, 1, rowSentinel.data(), 8, 1, GDT_Float32,
                            0, 0 ) != CE_None )
      {
        GDALClose( ds );
        return false;
      }
    }
  }
  GDALClose( ds );
  return true;
}

} // namespace

TEST_CASE( "detection lane feeds declared NoData sentinels as zero, not raw "
           "(raster-lane parity, R24-B)",
           "[r24_b][models][detection][nodata]" )
{
  RegistryReset reset;
  const CapturingDetectionProviderGuard guard( "r24-b-det-nd" );
  QTemporaryDir dir;

  const Json::Value manifest = detectionManifest( "r24-b-det-nd", "r24-b-det-nd" );
  registerManifest( manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString() );

  // Half the raster is the declared sentinel -9999. The raster lanes convert
  // it to NaN and the nodata_policy "zero" sweep feeds the model 0; the
  // detection lane used to feed -9999 raw (a ~-5100 outlier after the same
  // arithmetic the raster lane never performs on sentinels).
  const QString input = dir.filePath( QStringLiteral( "nodata_input.tif" ) );
  REQUIRE( writeNodataRaster( input, 200.0f, -9999.0f, true ) );

  const QString output = dir.filePath( QStringLiteral( "nodata_out.gpkg" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "r24-b-det-nd";
  request.asDetection = true;
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );

  REQUIRE( guard.capturedCount() > 0 );
  // Fixed: left half feeds 200, sentinel half feeds 0 → mean 100.
  // Broken: mean (200 + -9999)/2 = -4899.5.
  CHECK( guard.capturedMean() == Catch::Approx( 100.0 ).margin( 0.5 ) );
}

TEST_CASE( "detection lane leaves sentinel-free rasters untouched (control)",
           "[r24_b][models][detection][nodata]" )
{
  RegistryReset reset;
  const CapturingDetectionProviderGuard guard( "r24-b-det-plain" );
  QTemporaryDir dir;

  const Json::Value manifest = detectionManifest( "r24-b-det-plain", "r24-b-det-plain" );
  registerManifest( manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString() );

  // No NoData declared: no pixel may be rewritten (mean stays 200 — a
  // regression guard against over-eager sentinel guessing, e.g. treating 0
  // as an implicit sentinel).
  const QString input = dir.filePath( QStringLiteral( "plain_input.tif" ) );
  REQUIRE( writeNodataRaster( input, 200.0f, 200.0f, false ) );

  const QString output = dir.filePath( QStringLiteral( "plain_out.gpkg" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "r24-b-det-plain";
  request.asDetection = true;
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );

  REQUIRE( guard.capturedCount() > 0 );
  CHECK( guard.capturedMean() == Catch::Approx( 200.0 ).margin( 0.5 ) );
}
