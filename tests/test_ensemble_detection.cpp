// tests/test_ensemble_detection.cpp — detection ensembles (Platform 13.0):
// canonical Weighted Boxes Fusion semantics against hand-computed answers,
// the typed refusals (class-map mismatch, non-wbf combination, non-detection
// members), empty/NaN handling, the end-to-end fused vector product and its
// provenance (fusion algorithm + thresholds). Single-model detection behavior
// is covered by the historical suites and must stay unchanged.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/detection_fusion.h"
#include "operators/runtime/detection_tile_engine.h"
#include "operators/runtime/model_ensemble.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::DetectionBox;
using sicnu::operators::runtime::DetectionFusionContract;
using sicnu::operators::runtime::DetectionFusionResult;
using sicnu::operators::runtime::DetectionMemberBoxes;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::fuseDetectionsWbf;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

DetectionBox box( float x, float y, float w, float h, int classId, float confidence )
{
  DetectionBox b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  b.classId = classId;
  b.confidence = confidence;
  return b;
}

bool sameBox( const DetectionBox &a, const DetectionBox &b, float tolerance )
{
  return a.classId == b.classId && std::fabs( a.confidence - b.confidence ) <= tolerance
         && std::fabs( a.x - b.x ) <= tolerance && std::fabs( a.y - b.y ) <= tolerance
         && std::fabs( a.w - b.w ) <= tolerance && std::fabs( a.h - b.h ) <= tolerance;
}

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

/// Detection head fake: emits a fixed candidate set per framework id, in the
/// model input frame, layout xywh_objectness / channels_first. The candidate
/// sets are the hand-computed fusion inputs.
struct DetectionHead
{
  struct Candidate
  {
    float cx, cy, w, h, obj;
    float score0, score1;
  };
  std::vector<Candidate> candidates;
};

class DetectionHeadRuntime final : public IModelRuntime
{
  public:
    DetectionHeadRuntime( std::string artifact, std::string framework,
                          const DetectionHead &head )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_head( head ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "detection_fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int batch = blob.size[0];
      const int channels = 5 + 2; // xywh + objectness + 2 classes
      const int samples = static_cast<int>( m_head.candidates.size() );
      int dims[3] = { batch, channels, std::max( 1, samples ) };
      cv::Mat out( 3, dims, CV_32F );
      out.setTo( 0 );
      for ( int b = 0; b < batch; ++b )
      {
        float *plane = out.ptr<float>( b );
        for ( int s = 0; s < samples; ++s )
        {
          const DetectionHead::Candidate &c = m_head.candidates[static_cast<std::size_t>( s )];
          plane[s] = c.cx;
          plane[static_cast<std::size_t>( dims[2] ) + s] = c.cy;
          plane[static_cast<std::size_t>( 2 * dims[2] ) + s] = c.w;
          plane[static_cast<std::size_t>( 3 * dims[2] ) + s] = c.h;
          plane[static_cast<std::size_t>( 4 * dims[2] ) + s] = c.obj;
          plane[static_cast<std::size_t>( 5 * dims[2] ) + s] = c.score0;
          plane[static_cast<std::size_t>( 6 * dims[2] ) + s] = c.score1;
        }
      }
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    DetectionHead m_head;
};

struct DetectionProviderGuard
{
  DetectionProviderGuard( const std::string &framework, const DetectionHead &head )
  {
    ModelRuntimeRegistry::instance().registerProvider(
      framework,
      [ framework, head ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                            std::string *error ) -> ModelRuntimePtr {
        if ( model.resolvedArtifactPath.empty() )
        {
          if ( error )
            *error = "no resolved artifact";
          return nullptr;
        }
        return std::make_shared<DetectionHeadRuntime>( model.resolvedArtifactPath, framework,
                                                       head );
      } );
  }
};

/// Detection member manifest (fixed 16x16 input frame, two classes).
Json::Value detectionMemberManifest( const std::string &name, const std::string &framework,
                                     const std::vector<std::string> &classes )
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
  for ( const std::string &cls : classes )
    classesJson.append( cls );
  detection["classes"] = classesJson;
  return json;
}

void registerManifest( const Json::Value &json, const std::string &manifestPath )
{
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ), manifestPath, &error );
  if ( !ok )
    FAIL( "manifest rejected: " + error );
}

/// One feature read back from a published detection vector.
struct VectorFeature
{
  std::string className;
  double confidence = 0.0;
  double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
};

std::vector<VectorFeature> readDetectionVector( const QString &path )
{
  std::vector<VectorFeature> features;
  GDALAllRegister();
  GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr,
                                nullptr );
  if ( !ds )
    return features;
  for ( int layer = 0; layer < GDALDatasetGetLayerCount( ds ); ++layer )
  {
    OGRLayerH ogrLayer = GDALDatasetGetLayer( ds, layer );
    if ( !ogrLayer )
      continue;
    OGR_L_ResetReading( ogrLayer );
    OGRFeatureH feature = nullptr;
    while ( ( feature = OGR_L_GetNextFeature( ogrLayer ) ) != nullptr )
    {
      VectorFeature entry;
      entry.className = OGR_F_GetFieldAsString( feature, OGR_F_GetFieldIndex( feature, "class" ) );
      entry.confidence = OGR_F_GetFieldAsDouble( feature,
                                                 OGR_F_GetFieldIndex( feature, "confidence" ) );
      OGRGeometryH geometry = OGR_F_GetGeometryRef( feature );
      OGREnvelope envelope;
      envelope.MinX = envelope.MinY = envelope.MaxX = envelope.MaxY = 0.0;
      if ( geometry )
        OGR_G_GetEnvelope( geometry, &envelope ); // C API: void, fills in place
      entry.minX = envelope.MinX;
      entry.minY = envelope.MinY;
      entry.maxX = envelope.MaxX;
      entry.maxY = envelope.MaxY;
      features.push_back( entry );
      OGR_F_Destroy( feature );
    }
  }
  GDALClose( ds );
  return features;
}

} // namespace

// ---------------------------------------------------------------------------
// Pure WBF oracles (hand-computed, canonical semantics — ADR 0171)
// ---------------------------------------------------------------------------

TEST_CASE( "WBF fuses an overlapping same-class pair into the weighted box",
           "[models][ensemble][detection][wbf]" )
{
  const DetectionFusionContract contract; // iou 0.55, skip 0
  // A=[10,10,30,30]@0.9, B=[12,12,32,32]@0.8: IoU = 324/476 ≈ 0.6807 > 0.55.
  std::vector<DetectionMemberBoxes> members;
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 10, 10, 20, 20, 0, 0.9f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 12, 12, 20, 20, 0, 0.8f ) } } );

  const DetectionFusionResult fused = fuseDetectionsWbf( members, contract );
  REQUIRE( fused.boxes.size() == 1 );
  // Coordinates: Σ(eff·coord)/Σ(eff) = (0.9·10 + 0.8·12)/1.7 ≈ 10.941176.
  CHECK( fused.boxes[0].x == Catch::Approx( 10.941176f ).margin( 1e-5f ) );
  CHECK( fused.boxes[0].y == Catch::Approx( 10.941176f ).margin( 1e-5f ) );
  CHECK( fused.boxes[0].w == Catch::Approx( 20.0f ).margin( 1e-5f ) );
  // Confidence: mean effective × min(2,2)/Σw = 0.85 × 1 = 0.85.
  CHECK( fused.boxes[0].confidence == Catch::Approx( 0.85f ).margin( 1e-6f ) );
  CHECK( fused.boxesPooled == 2 );
  CHECK( fused.clusters == 1 );
  CHECK( fused.memberSurviving[0] == 1 );
  CHECK( fused.memberAbsorbed[0] == 1 );
}

TEST_CASE( "WBF weights coordinates and the count-aware score by member weight",
           "[models][ensemble][detection][wbf]" )
{
  const DetectionFusionContract contract;
  std::vector<DetectionMemberBoxes> members;
  members.push_back( DetectionMemberBoxes{ 3.0, { box( 10, 10, 20, 20, 0, 0.9f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 12, 12, 20, 20, 0, 0.8f ) } } );

  const DetectionFusionResult fused = fuseDetectionsWbf( members, contract );
  REQUIRE( fused.boxes.size() == 1 );
  // Effective scores 2.7 and 0.8 → x = (2.7·10 + 0.8·12)/3.5 ≈ 10.457143.
  CHECK( fused.boxes[0].x == Catch::Approx( 10.457143f ).margin( 1e-5f ) );
  // mean effective = 1.75; × min(2,2)/Σw(4) = 0.875.
  CHECK( fused.boxes[0].confidence == Catch::Approx( 0.875f ).margin( 1e-6f ) );
}

TEST_CASE( "WBF keeps disjoint boxes and classes separate", "[models][ensemble][detection][wbf]" )
{
  const DetectionFusionContract contract;
  std::vector<DetectionMemberBoxes> members;
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 0, 0, 10, 10, 0, 0.9f ),
                                                  box( 100, 100, 10, 10, 1, 0.7f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 200, 200, 10, 10, 1, 0.6f ) } } );

  const DetectionFusionResult fused = fuseDetectionsWbf( members, contract );
  REQUIRE( fused.boxes.size() == 3 );
  CHECK( fused.clusters == 3 );
  // Deterministic order: confidence desc. Each cluster holds ONE box out of
  // TWO members, so the canonical rescale halves it: 0.45, 0.35, 0.30.
  CHECK( fused.boxes[0].confidence == Catch::Approx( 0.45f ).margin( 1e-6f ) );
  CHECK( fused.boxes[1].confidence == Catch::Approx( 0.35f ).margin( 1e-6f ) );
  CHECK( fused.boxes[2].confidence == Catch::Approx( 0.30f ).margin( 1e-6f ) );
}

TEST_CASE( "WBF never clusters across classes and gates low-confidence boxes",
           "[models][ensemble][detection][wbf]" )
{
  DetectionFusionContract contract;
  contract.skipBoxThreshold = 0.5;
  std::vector<DetectionMemberBoxes> members;
  // Identical geometry, different classes → two clusters.
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 10, 10, 20, 20, 0, 0.9f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 10, 10, 20, 20, 1, 0.9f ),
                                                  box( 10, 10, 20, 20, 0, 0.4f ) } } );

  const DetectionFusionResult fused = fuseDetectionsWbf( members, contract );
  REQUIRE( fused.boxes.size() == 2 );
  CHECK( fused.boxesPooled == 3 );
  CHECK( fused.boxesGated == 1 ); // the 0.4-confidence box never entered
  CHECK( fused.clusters == 2 );
}

TEST_CASE( "WBF drops zero-weight members and non-finite boxes",
           "[models][ensemble][detection][wbf]" )
{
  const DetectionFusionContract contract;
  std::vector<DetectionMemberBoxes> members;
  members.push_back( DetectionMemberBoxes{ 0.0, { box( 10, 10, 20, 20, 0, 0.9f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 30, 30, 20, 20, 0, 0.8f ) } } );

  const DetectionFusionResult fused = fuseDetectionsWbf( members, contract );
  REQUIRE( fused.boxes.size() == 1 );
  CHECK( fused.boxes[0].x == Catch::Approx( 30.0f ).margin( 1e-6f ) );
  CHECK( fused.memberSurviving[0] == 0 ); // weight 0 contributes no boxes

  // Non-finite geometry never reaches the product (defensive; decode clamps).
  // The survivor is a lone cluster of one out of two members: 0.9 × 1/2.
  std::vector<DetectionMemberBoxes> poisoned;
  poisoned.push_back( DetectionMemberBoxes{ 1.0, { box( 10, 10, 20, 20, 0, 0.9f ) } } );
  poisoned.push_back( DetectionMemberBoxes{ 1.0, { box( 10, 10, 20, 20, 0,
                                                        std::numeric_limits<float>::quiet_NaN() ) } } );
  const DetectionFusionResult clean = fuseDetectionsWbf( poisoned, contract );
  REQUIRE( clean.boxes.size() == 1 );
  CHECK( clean.boxes[0].confidence == Catch::Approx( 0.45f ).margin( 1e-6f ) );
  CHECK( clean.boxesGated == 1 );
}

TEST_CASE( "WBF scales a lone detection by the member count",
           "[models][ensemble][detection][wbf]" )
{
  const DetectionFusionContract contract;
  // Three members; only two detect the object → the cluster is scaled by
  // min(3,2)/3: mean effective 0.85 × 2/3 ≈ 0.566667.
  std::vector<DetectionMemberBoxes> members;
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 10, 10, 20, 20, 0, 0.9f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, { box( 11, 11, 20, 20, 0, 0.8f ) } } );
  members.push_back( DetectionMemberBoxes{ 1.0, {} } );

  const DetectionFusionResult fused = fuseDetectionsWbf( members, contract );
  REQUIRE( fused.boxes.size() == 1 );
  CHECK( fused.boxes[0].confidence == Catch::Approx( 0.566667f ).margin( 1e-5f ) );
}

TEST_CASE( "WBF is order-independent and empty-safe",
           "[models][ensemble][detection][wbf]" )
{
  const DetectionFusionContract contract;
  const std::vector<DetectionMemberBoxes> members = {
    DetectionMemberBoxes{ 2.0, { box( 10, 10, 20, 20, 0, 0.9f ), box( 90, 90, 8, 8, 1, 0.5f ) } },
    DetectionMemberBoxes{ 1.0, { box( 12, 12, 20, 20, 0, 0.8f ) } } };
  const DetectionFusionResult forward = fuseDetectionsWbf( members, contract );

  std::vector<DetectionMemberBoxes> reversed( members.rbegin(), members.rend() );
  const DetectionFusionResult backward = fuseDetectionsWbf( reversed, contract );
  REQUIRE( forward.boxes.size() == backward.boxes.size() );
  for ( std::size_t i = 0; i < forward.boxes.size(); ++i )
    CHECK( sameBox( forward.boxes[i], backward.boxes[i], 1e-6f ) );

  const DetectionFusionResult empty = fuseDetectionsWbf( {}, contract );
  CHECK( empty.boxes.empty() );
  CHECK( empty.clusters == 0 );

  const DetectionFusionResult allEmpty = fuseDetectionsWbf(
    { DetectionMemberBoxes{ 1.0, {} }, DetectionMemberBoxes{ 1.0, {} } }, contract );
  CHECK( allEmpty.boxes.empty() );
}

TEST_CASE( "detection fusion contract ranges are validated",
           "[models][ensemble][detection][manifest]" )
{
  DetectionFusionContract bad;
  bad.iouThreshold = 0.0;
  CHECK( bad.validate().find( "iou_threshold" ) != std::string::npos );
  bad = DetectionFusionContract{};
  bad.iouThreshold = 1.5;
  CHECK( bad.validate().find( "iou_threshold" ) != std::string::npos );
  bad = DetectionFusionContract{};
  bad.skipBoxThreshold = 1.0;
  CHECK( bad.validate().find( "skip_box_threshold" ) != std::string::npos );
  CHECK( DetectionFusionContract{}.validate().empty() );
}

// ---------------------------------------------------------------------------
// End-to-end detection ensemble
// ---------------------------------------------------------------------------

TEST_CASE( "detection ensemble fuses member boxes into one WBF vector product",
           "[models][ensemble][detection][run]" )
{
  RegistryReset reset;
  // One 16x16 tile at the model input size, halo 0 → scale 1 (fed px = raster
  // px). Member A: class 0 → raster (6,6,4,4)@0.9; class 1 → (1,1,2,2)@0.9.
  // Member B: class 0 → (7,6,4,4)@0.8 (IoU 0.6 with A's); class 1 → (13,13,2,2)@0.7.
  DetectionHead headA;
  headA.candidates = { { 8, 8, 4, 4, 1.0f, 0.9f, 0.1f },
                       { 2, 2, 2, 2, 1.0f, 0.1f, 0.9f } };
  DetectionHead headB;
  headB.candidates = { { 9, 8, 4, 4, 1.0f, 0.8f, 0.2f },
                       { 14, 14, 2, 2, 1.0f, 0.3f, 0.7f } };
  const DetectionProviderGuard guardA( "detfw-a", headA );
  const DetectionProviderGuard guardB( "detfw-b", headB );

  QTemporaryDir dir;
  registerManifest( detectionMemberManifest( "det-a", "detfw-a", { "tree", "shrub" } ),
                    dir.filePath( QStringLiteral( "det-a/model.json" ) ).toStdString() );
  registerManifest( detectionMemberManifest( "det-b", "detfw-b", { "tree", "shrub" } ),
                    dir.filePath( QStringLiteral( "det-b/model.json" ) ).toStdString() );

  Json::Value ensemble( Json::objectValue );
  ensemble["name"] = "det-ens";
  ensemble["task"] = "detection";
  ensemble["framework"] = "onnx";
  Json::Value members( Json::arrayValue );
  Json::Value a( Json::objectValue );
  a["model"] = "det-a";
  a["weight"] = 1.0;
  members.append( a );
  Json::Value b( Json::objectValue );
  b["model"] = "det-b";
  b["weight"] = 1.0;
  members.append( b );
  ensemble["ensemble"]["members"] = members;
  ensemble["ensemble"]["combination"] = "wbf";
  registerManifest( ensemble, dir.filePath( QStringLiteral( "det-ens/model.json" ) ).toStdString() );

  const QString input = dir.filePath( QStringLiteral( "det_input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 3, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f ).withConstantValue( 2, 20.0f ).withConstantValue( 3, 30.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );

  const QString output = dir.filePath( QStringLiteral( "fused.gpkg" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "det-ens";
  request.asDetection = true;
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );

  CHECK( result.backend == "ensemble(wbf)" );
  CHECK( result.payload["combination"].asString() == "wbf" );
  CHECK( result.payload["fusion"]["algorithm"].asString() == "wbf" );
  CHECK( result.payload["detections"].asUInt64() == 3 );

  const std::vector<VectorFeature> features = readDetectionVector( output );
  REQUIRE( features.size() == 3 );
  // Deterministic order: confidence desc. The class-0 pair fuses to 0.85
  // (mean effective × 2/2); the two lone class-1 boxes are scaled by 1/2
  // each (0.45 and 0.35) — canonical WBF agreement signal.
  CHECK( features[0].confidence == Catch::Approx( 0.85f ).margin( 1e-4f ) );
  CHECK( features[0].className == "tree" );
  CHECK( features[1].confidence == Catch::Approx( 0.45f ).margin( 1e-4f ) );
  CHECK( features[1].className == "shrub" );
  CHECK( features[2].confidence == Catch::Approx( 0.35f ).margin( 1e-4f ) );
  CHECK( features[2].className == "shrub" );

  // The fused class-0 box: x = (0.9·6 + 0.8·7)/1.7 ≈ 6.470588 raster px; the
  // builder's geotransform is [0,1,0,H,0,-1] so map x == raster x and
  // map y == H - raster y (16 - 10 = 6, 16 - 6 = 10).
  const VectorFeature &fusedClass0 = features[0]; // 0.85 = the fused class-0 pair
  CHECK( fusedClass0.minX == Catch::Approx( 6.470588 ).margin( 1e-3 ) );
  CHECK( fusedClass0.maxX == Catch::Approx( 10.470588 ).margin( 1e-3 ) );
  CHECK( fusedClass0.minY == Catch::Approx( 16.0 - 10.0 ).margin( 1e-3 ) ); // y+h = 10
  CHECK( fusedClass0.maxY == Catch::Approx( 16.0 - 6.0 ).margin( 1e-3 ) );  // y = 6

  // Provenance sidecar: the fusion block explains the product.
  const QString sidecar = output + QStringLiteral( ".prov.json" );
  REQUIRE( QFile::exists( sidecar ) );
  std::ifstream sidecarStream( sidecar.toStdString() );
  REQUIRE( sidecarStream.is_open() );
  Json::CharReaderBuilder readerBuilder;
  Json::Value provenance;
  std::string parseErrors;
  REQUIRE( Json::parseFromStream( readerBuilder, sidecarStream, &provenance, &parseErrors ) );
  CHECK( provenance["schema"].asString() == "exp-rs-prov/1" );
  REQUIRE( provenance.isMember( "fusion" ) );
  CHECK( provenance["fusion"]["algorithm"].asString() == "wbf" );
  CHECK( provenance["fusion"]["iou_threshold"].asDouble() == Catch::Approx( 0.55 ) );
  CHECK( provenance["fusion"]["skip_box_threshold"].asDouble() == Catch::Approx( 0.0 ) );
  CHECK( provenance["fusion"]["boxes_pooled"].asUInt64() == 4 );
  CHECK( provenance["fusion"]["detections"].asUInt64() == 3 );
  REQUIRE( provenance["ensemble"].isObject() );
  CHECK( provenance["ensemble"]["members"].size() == 2 );
  CHECK( provenance["ensemble"]["member_concurrency"].asInt() >= 1 );
  CHECK( provenance["ensemble"]["staging_compression"].asString() == "deflate" );
}

TEST_CASE( "detection ensemble refusals stay typed", "[models][ensemble][detection][refusal]" )
{
  RegistryReset reset;
  DetectionHead headA;
  headA.candidates = { { 8, 8, 4, 4, 1.0f, 0.9f, 0.1f } };
  const DetectionProviderGuard guardA( "detfw-a", headA );
  QTemporaryDir dir;
  registerManifest( detectionMemberManifest( "det-a", "detfw-a", { "tree", "shrub" } ),
                    dir.filePath( QStringLiteral( "det-a/model.json" ) ).toStdString() );
  // A segmentation member (no detection contract).
  Json::Value segMember( Json::objectValue );
  segMember["name"] = "seg-b";
  segMember["task"] = "segmentation";
  segMember["framework"] = "detfw-b";
  segMember["artifact"]["path"] = identityModelPath().toStdString();
  registerManifest( segMember, dir.filePath( QStringLiteral( "seg-b/model.json" ) ).toStdString() );
  // A detection member with a DIFFERENT class vocabulary.
  registerManifest( detectionMemberManifest( "det-c", "detfw-a", { "tree", "shrub", "grass" } ),
                    dir.filePath( QStringLiteral( "det-c/model.json" ) ).toStdString() );

  auto registerEnsemble = [ & ]( const std::string &name, const std::string &memberA,
                                 const std::string &memberB,
                                 const std::string &combination ) {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = name;
    ensemble["task"] = "detection";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    Json::Value a( Json::objectValue );
    a["model"] = memberA;
    a["weight"] = 1.0;
    members.append( a );
    Json::Value b( Json::objectValue );
    b["model"] = memberB;
    b["weight"] = 1.0;
    members.append( b );
    ensemble["ensemble"]["members"] = members;
    if ( !combination.empty() )
      ensemble["ensemble"]["combination"] = combination;
    registerManifest( ensemble, dir.filePath( QString::fromStdString( name ) + "/model.json" )
                                  .toStdString() );
  };

  const QString input = dir.filePath( QStringLiteral( "det_input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 3, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );

  RSOperatorContext context;

  SECTION( "a detection request on a non-wbf ensemble is refused" )
  {
    registerEnsemble( "det-ens-mean", "det-a", "det-c", std::string() );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "x.gpkg" ) ).toStdString();
    request.modelReference = "det-ens-mean";
    request.asDetection = true;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "requires combination 'wbf'" ) );
  }

  SECTION( "a raster request on a wbf ensemble is refused" )
  {
    registerManifest( detectionMemberManifest( "det-a2", "detfw-a", { "tree", "shrub" } ),
                      dir.filePath( QStringLiteral( "det-a2/model.json" ) ).toStdString() );
    registerEnsemble( "det-ens-wbf", "det-a", "det-a2", "wbf" );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "y.tif" ) ).toStdString();
    request.modelReference = "det-ens-wbf";
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "combination 'wbf'" ) );
  }

  SECTION( "a member without a detection contract is refused" )
  {
    registerEnsemble( "det-ens-seg", "det-a", "seg-b", "wbf" );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "z.gpkg" ) ).toStdString();
    request.modelReference = "det-ens-seg";
    request.asDetection = true;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "cannot run as a detection member" ) );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "z.gpkg" ) ) ) );
  }

  SECTION( "a class vocabulary mismatch across members is refused" )
  {
    registerEnsemble( "det-ens-vocab", "det-a", "det-c", "wbf" );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "v.gpkg" ) ).toStdString();
    request.modelReference = "det-ens-vocab";
    request.asDetection = true;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "shared class map" ) );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "v.gpkg" ) ) ) );
  }
}

TEST_CASE( "detection ensemble publishes an empty product when no member detects",
           "[models][ensemble][detection][empty]" )
{
  RegistryReset reset;
  DetectionHead head;
  // One candidate far below the 0.25 confidence gate.
  head.candidates = { { 8, 8, 4, 4, 1.0f, 0.05f, 0.05f } };
  const DetectionProviderGuard guard( "detfw-empty", head );
  QTemporaryDir dir;
  registerManifest( detectionMemberManifest( "det-e1", "detfw-empty", { "tree", "shrub" } ),
                    dir.filePath( QStringLiteral( "det-e1/model.json" ) ).toStdString() );
  registerManifest( detectionMemberManifest( "det-e2", "detfw-empty", { "tree", "shrub" } ),
                    dir.filePath( QStringLiteral( "det-e2/model.json" ) ).toStdString() );

  Json::Value ensemble( Json::objectValue );
  ensemble["name"] = "det-ens-empty";
  ensemble["task"] = "detection";
  ensemble["framework"] = "onnx";
  Json::Value members( Json::arrayValue );
  members.append( "det-e1" );
  members.append( "det-e2" );
  ensemble["ensemble"]["members"] = members;
  ensemble["ensemble"]["combination"] = "wbf";
  registerManifest( ensemble,
                    dir.filePath( QStringLiteral( "det-ens-empty/model.json" ) ).toStdString() );

  const QString input = dir.filePath( QStringLiteral( "det_input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 3, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );

  const QString output = dir.filePath( QStringLiteral( "empty.gpkg" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "det-ens-empty";
  request.asDetection = true;
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );
  CHECK( result.payload["detections"].asUInt64() == 0 );

  const std::vector<VectorFeature> features = readDetectionVector( output );
  CHECK( features.empty() );
  // No residue anywhere in the output directory.
  const QStringList residue = QDir( dir.path() ).entryList( { "*.tmp~*", ".*.tmp~*" },
                                                            QDir::Files | QDir::Hidden );
  CHECK( residue.empty() );
}

TEST_CASE( "single-model detection runs stay behavior-compatible",
           "[models][ensemble][detection][compat]" )
{
  RegistryReset reset;
  DetectionHead head;
  head.candidates = { { 8, 8, 4, 4, 1.0f, 0.9f, 0.1f },
                      { 8, 8, 4, 4, 1.0f, 0.2f, 0.2f } };
  const DetectionProviderGuard guard( "detfw-solo", head );
  QTemporaryDir dir;
  registerManifest( detectionMemberManifest( "det-solo", "detfw-solo", { "tree", "shrub" } ),
                    dir.filePath( QStringLiteral( "det-solo/model.json" ) ).toStdString() );

  const QString input = dir.filePath( QStringLiteral( "det_input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 3, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );

  const QString output = dir.filePath( QStringLiteral( "solo.gpkg" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "det-solo";
  request.asDetection = true;
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );
  CHECK( result.backend == "detection_fake" );
  CHECK( result.payload["detections"].asUInt64() == 1 ); // 0.9 gate, 0.2 suppressed

  const std::vector<VectorFeature> features = readDetectionVector( output );
  REQUIRE( features.size() == 1 );
  CHECK( features[0].className == "tree" );
  CHECK( features[0].confidence == Catch::Approx( 0.9f ).margin( 1e-4f ) );
  // cx8,cy8,w4,h4 at scale 1 → raster (6,6,4,4); map x == raster x.
  CHECK( features[0].minX == Catch::Approx( 6.0 ).margin( 1e-3 ) );
  CHECK( features[0].maxX == Catch::Approx( 10.0 ).margin( 1e-3 ) );
}
