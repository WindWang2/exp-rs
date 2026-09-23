// tests/test_model_tasks.cpp — Platform 4.0 task outputs and the unified
// execution seam: derived raster formats (labels/mask/confidence), detection
// decode/NMS/dedup, the registry-seam convergence of the four model
// operators, and the embedding/regression semantics.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <stdexcept>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/runtime/detection_postprocess.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provenance_verify.h"
#include "operators/runtime/tile_inference_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorRegistry;
using sicnu::operators::runtime::DetectionBox;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::RasterOutputMode;
using sicnu::operators::runtime::TileInferenceEngine;

/// Deterministic class-plane generator: NCHW out, value = f(class, pixel).
/// 2 classes: plane0 = 1.0 where (x+y) even, plane1 = 1.0 where odd — the
/// argmax is a checkerboard; confidences are exact 1.0.
class ClassPlaneRuntime final : public IModelRuntime
{
  public:
    ClassPlaneRuntime( int classes ) : m_classes( classes ) {}

    std::string framework() const override { return "planefw"; }
    std::string backendName() const override { return "class_planes"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "class-planes"; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int N = blob.size[0], H = blob.size[2], W = blob.size[3];
      int dims[4] = { N, m_classes, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.25f ) );
      for ( int y = 0; y < H; ++y )
      {
        for ( int x = 0; x < W; ++x )
        {
          const int winner = ( x + y ) % m_classes;
          for ( int n = 0; n < N; ++n )
            out.ptr<float>( n, winner )[y * W + x] = 1.0f;
        }
      }
      return out;
    }

  private:
    int m_classes;
};

/// Single-channel identity-with-offset runtime (regression semantics).
class RegressionRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "regfw"; }
    std::string backendName() const override { return "regression"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "regression"; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      cv::Mat out = blob.clone();
      out += 5.0f;
      return out;
    }
};

struct ProviderGuard
{
    ProviderGuard()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.registerProvider( "planefw", []( const ModelInfo &, const ModelHardwareCapabilities &,
                                                std::string * ) -> ModelRuntimePtr {
        return std::make_shared<ClassPlaneRuntime>( 2 );
      } );
      registry.registerProvider( "regfw", []( const ModelInfo &, const ModelHardwareCapabilities &,
                                              std::string * ) -> ModelRuntimePtr {
        return std::make_shared<RegressionRuntime>();
      } );
      registry.releaseAll();
    }
    ~ProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

ModelInfo planeModel( const std::string &task = "segmentation" )
{
  ModelInfo info;
  info.name = "plane-model";
  info.task = task;
  info.framework = "planefw";
  info.readiness = ModelReadiness::Ready;
  info.tiling.tileSize = 32;
  info.output.classes = { "even", "odd" };
  return info;
}

QString writeRaster( const QTemporaryDir &dir, const QString &name, int w, int h )
{
  sicnu::testing::RsSyntheticRasterBuilder( w, h, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( dir.filePath( name ) );
  return dir.filePath( name );
}

bool fileExists( const QString &path )
{
  return QFileInfo::exists( path );
}

std::vector<float> readBand( const QString &path, int band, int *width, int *height, int *type = nullptr )
{
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  *width = ds->GetRasterXSize();
  *height = ds->GetRasterYSize();
  if ( type )
    *type = ds->GetRasterBand( band )->GetRasterDataType();
  std::vector<float> row( static_cast<std::size_t>( *width ) );
  std::vector<float> values( static_cast<std::size_t>( *width ) * ( *height ) );
  for ( int y = 0; y < *height; ++y )
  {
    REQUIRE( ds->GetRasterBand( band )->RasterIO( GF_Read, 0, y, *width, 1, row.data(), *width, 1,
                                                  GDT_Float32, 0, 0 ) == CE_None );
    std::copy( row.begin(), row.end(), values.begin() + static_cast<std::size_t>( y ) * ( *width ) );
  }
  GDALClose( ds );
  return values;
}

} // namespace

// ---------------------------------------------------------------------------
// Derived raster outputs (T13/T14/T15)
// ---------------------------------------------------------------------------

TEST_CASE( "labels output writes the argmax class raster with a palette", "[models][post]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "labels-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "labels-out.tif" ) );

  ModelInfo model = planeModel();
  model.output.format = "labels";
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );

  CHECK( stats.outBands == 1 );
  int w = 0, h = 0, type = 0;
  const auto values = readBand( output, 1, &w, &h, &type );
  CHECK( type == GDT_Byte );
  REQUIRE( values.size() == 32 * 32 );
  CHECK( values[0] == 0.0f ); // (0,0) even -> class 0
  CHECK( values[1] == 1.0f ); // (1,0) odd -> class 1
  // The palette metadata names the classes deterministically.
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( output.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  const char *names = ds->GetMetadataItem( "SICNU_CLASS_NAMES", "" );
  REQUIRE( names );
  CHECK( std::string( names ).find( "even" ) != std::string::npos );
  GDALClose( ds );
}

TEST_CASE( "mask output binarizes against the background class", "[models][post]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "mask-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "mask-out.tif" ) );

  ModelInfo model = planeModel();
  model.output.format = "mask";
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  engine.run( input.toStdString(), {}, output.toStdString(), context );

  int w = 0, h = 0, type = 0;
  const auto values = readBand( output, 1, &w, &h, &type );
  CHECK( type == GDT_Byte );
  // Checkerboard winner alternates between class 0 (background -> 0) and
  // class 1 (foreground -> 1).
  int ones = 0;
  for ( float v : values )
    ones += v == 1.0f ? 1 : 0;
  CHECK( ones == 16 * 32 ); // half the checkerboard
}

TEST_CASE( "confidence output writes the top-1 score band", "[models][post]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "conf-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "conf-out.tif" ) );

  ModelInfo model = planeModel();
  model.output.format = "confidence";
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  engine.run( input.toStdString(), {}, output.toStdString(), context );

  int w = 0, h = 0;
  const auto values = readBand( output, 1, &w, &h );
  for ( float v : values )
    CHECK( v == Catch::Approx( 1.0f ) ); // the checkerboard winner scores 1.0
}

TEST_CASE( "regression models run through the same engine as continuous rasters", "[models][post]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "reg-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "reg-out.tif" ) );

  ModelInfo model;
  model.name = "regression-model";
  model.task = "regression";
  model.framework = "regfw";
  model.readiness = ModelReadiness::Ready;
  model.tiling.tileSize = 32;
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  // Feed band 1 only: the runtime mirrors the fed channels (C'=1) so the
  // continuous single-channel raster is unambiguous.
  const auto stats = engine.run( input.toStdString(), { 1 }, output.toStdString(), context );

  CHECK( stats.outBands == 1 );
  int w = 0, h = 0;
  const auto values = readBand( output, 1, &w, &h );
  // The synthetic input is constant 0 -> the regression offset shows directly.
  CHECK( values[0] == Catch::Approx( 5.0f ) );
}

// ---------------------------------------------------------------------------
// Detection decode / NMS / dedup (T17/T18/T19)
// ---------------------------------------------------------------------------

TEST_CASE( "detection decode maps v5 and v8 layouts to raster boxes", "[models][detect]" )
{
  using sicnu::operators::runtime::decodeDetections;
  sicnu::operators::ModelDetectionContract contract;
  contract.classes = { "ship" };
  contract.layout = "xywh_objectness";
  contract.tensorLayout = "channels_first";

  // One candidate: center (16,16), size (8,4), objectness 0.9, class score 1.0.
  // v5 (channels_first): C = 5 + 1, N = 1.
  float v5[6] = { 16.0f, 16.0f, 8.0f, 4.0f, 0.9f, 1.0f };
  cv::Mat t5( 3, std::vector<int>{ 1, 6, 1 }.data(), CV_32F, v5 );
  std::vector<DetectionBox> boxes;
  REQUIRE( decodeDetections( t5, contract, 0, 0, 1.0, 1.0, 32, 32, boxes ).empty() );
  REQUIRE( boxes.size() == 1 );
  CHECK( boxes[0].x == Catch::Approx( 12.0f ) );
  CHECK( boxes[0].y == Catch::Approx( 14.0f ) );
  CHECK( boxes[0].w == Catch::Approx( 8.0f ) );
  CHECK( boxes[0].h == Catch::Approx( 4.0f ) );
  CHECK( boxes[0].confidence == Catch::Approx( 0.9f ) );
  CHECK( boxes[0].classId == 0 );

  // The same candidate as v8, channels_last: C = 4 + 1, N = 1, no objectness.
  float v8[5] = { 16.0f, 16.0f, 8.0f, 4.0f, 0.8f };
  cv::Mat t8( 3, std::vector<int>{ 1, 1, 5 }.data(), CV_32F, v8 );
  contract.layout = "xywh_class_scores";
  contract.tensorLayout = "channels_last";
  boxes.clear();
  REQUIRE( decodeDetections( t8, contract, 0, 0, 1.0, 1.0, 32, 32, boxes ).empty() );
  REQUIRE( boxes.size() == 1 );
  CHECK( boxes[0].confidence == Catch::Approx( 0.8f ) );

  // Scale mapping: fed tensor was a 2x downsample of a 64 px window
  // (the v8 tensor above keeps its channels_last layout).
  boxes.clear();
  REQUIRE( decodeDetections( t8, contract, 0, 0, 2.0, 2.0, 64, 32, boxes ).empty() );
  REQUIRE( boxes.size() == 1 );
  CHECK( boxes[0].x == Catch::Approx( 24.0f ) );
  CHECK( boxes[0].w == Catch::Approx( 16.0f ) );
  // Raster-bounds clamp: a window straddling the raster edge keeps the
  // in-raster slice (56..64) as a clipped box; a fully outside candidate is
  // dropped (no zero-area boxes).
  boxes.clear();
  REQUIRE( decodeDetections( t8, contract, 32, 0, 2.0, 2.0, 64, 32, boxes ).empty() );
  REQUIRE( boxes.size() == 1 );
  CHECK( boxes[0].x == Catch::Approx( 56.0f ) );
  CHECK( boxes[0].w == Catch::Approx( 8.0f ) );
  boxes.clear();
  REQUIRE( decodeDetections( t8, contract, 100, 0, 2.0, 2.0, 64, 32, boxes ).empty() );
  CHECK( boxes.empty() );
}

TEST_CASE( "detection decode accepts a strided (non-contiguous) head output (#1056)",
           "[models][detect]" )
{
  using sicnu::operators::runtime::decodeDetections;
  sicnu::operators::ModelDetectionContract contract;
  contract.classes = { "ship" };
  contract.layout = "xywh_objectness";
  contract.tensorLayout = "channels_first";

  // Reference: one candidate per 4 candidates, values on a contiguous
  // (1, 6, N) tensor.
  constexpr int kC = 6;
  constexpr int kN = 4;
  std::vector<float> values( static_cast<std::size_t>( kC ) * kN );
  for ( int n = 0; n < kN; ++n )
  {
    values[static_cast<std::size_t>( 0 ) * kN + n] = 16.0f + 8.0f * n; // x center
    values[static_cast<std::size_t>( 1 ) * kN + n] = 16.0f;            // y center
    values[static_cast<std::size_t>( 2 ) * kN + n] = 8.0f;             // w
    values[static_cast<std::size_t>( 3 ) * kN + n] = 4.0f;             // h
    values[static_cast<std::size_t>( 4 ) * kN + n] = 0.9f;             // objectness
    values[static_cast<std::size_t>( 5 ) * kN + n] = 1.0f;             // class score
  }
  const cv::Mat continuous( 3, std::vector<int>{ 1, kC, kN }.data(), CV_32F,
                            values.data() );

  // Fixture: the SAME values on a strided view — a (1, 6, 8) buffer sliced
  // to the last 4 candidate columns, exactly the padded/strided header a
  // provider may hand back. Pre-#1056 the decode read it with continuous
  // strides and produced garbage boxes.
  std::vector<float> padded( static_cast<std::size_t>( kC ) * 8, -7.0f );
  for ( int c = 0; c < kC; ++c )
    for ( int n = 0; n < kN; ++n )
      padded[static_cast<std::size_t>( c ) * 8 + n] =
        values[static_cast<std::size_t>( c ) * kN + n];
  cv::Mat paddedMat( 3, std::vector<int>{ 1, kC, 8 }.data(), CV_32F, padded.data() );
  const cv::Mat strided =
    paddedMat( { cv::Range::all(), cv::Range::all(), cv::Range( 0, kN ) } ).clone();
  REQUIRE_FALSE( paddedMat( { cv::Range::all(), cv::Range::all(), cv::Range( 0, kN ) } )
                   .isContinuous() );

  std::vector<DetectionBox> fromContinuous;
  REQUIRE( decodeDetections( continuous, contract, 0, 0, 1.0, 1.0, 64, 32, fromContinuous )
             .empty() );
  REQUIRE( fromContinuous.size() == kN );

  std::vector<DetectionBox> fromStrided;
  REQUIRE( decodeDetections( paddedMat( { cv::Range::all(), cv::Range::all(),
                                          cv::Range( 0, kN ) } ),
                             contract, 0, 0, 1.0, 1.0, 64, 32, fromStrided )
             .empty() );
  REQUIRE( fromStrided.size() == fromContinuous.size() );
  for ( std::size_t i = 0; i < fromContinuous.size(); ++i )
  {
    CHECK( fromStrided[i].x == Catch::Approx( fromContinuous[i].x ) );
    CHECK( fromStrided[i].y == Catch::Approx( fromContinuous[i].y ) );
    CHECK( fromStrided[i].w == Catch::Approx( fromContinuous[i].w ) );
    CHECK( fromStrided[i].h == Catch::Approx( fromContinuous[i].h ) );
    CHECK( fromStrided[i].confidence == Catch::Approx( fromContinuous[i].confidence ) );
    CHECK( fromStrided[i].classId == fromContinuous[i].classId );
  }
}

TEST_CASE( "NMS is deterministic and resolves tile-overlap duplicates", "[models][detect]" )
{
  using sicnu::operators::runtime::nonMaxSuppression;
  std::vector<DetectionBox> boxes;
  // The same object detected by two overlapping tiles (bit-equal box).
  boxes.push_back( DetectionBox{ 10, 10, 20, 20, 0, 0.9f } );
  boxes.push_back( DetectionBox{ 10, 10, 20, 20, 0, 0.9f } );
  // A weaker shifted duplicate (IoU > threshold).
  boxes.push_back( DetectionBox{ 12, 12, 20, 20, 0, 0.7f } );
  // A distinct object.
  boxes.push_back( DetectionBox{ 60, 60, 10, 10, 0, 0.5f } );
  // Score-tied boxes must fall back to the deterministic geometry order.
  boxes.push_back( DetectionBox{ 30, 30, 5, 5, 1, 0.5f } );
  boxes.push_back( DetectionBox{ 80, 80, 5, 5, 1, 0.5f } );

  const auto kept = nonMaxSuppression( boxes, 0.45 );
  // The 0.7 shifted duplicate (IoU 0.68 with the winner) is suppressed; the
  // bit-equal twin collapses; the three non-overlapping 0.5 boxes survive.
  REQUIRE( kept.size() == 4 );
  CHECK( kept[0].confidence == Catch::Approx( 0.9f ) );          // strongest first
  CHECK( kept[0].x == Catch::Approx( 10.0f ) );
  bool hasDistinct = false, hasTieFirst = false;
  for ( const auto &box : kept )
  {
    if ( box.x == 60 )
      hasDistinct = true;
    if ( box.x == 30 )
      hasTieFirst = true; // 30 < 80: the lower-coordinate tie wins
  }
  CHECK( hasDistinct );
  CHECK( hasTieFirst );
}

TEST_CASE( "NMS bucketing reproduces the dense kept set and stays cancelable (F-OPS-5)",
           "[models][detect][f-ops-5]" )
{
  using sicnu::operators::runtime::nonMaxSuppression;
  using sicnu::operators::runtime::dedupDetections;

  // Deterministic pseudo-random candidate set: overlapping clusters across a
  // 2000x2000 canvas. The bucketed pass must keep EXACTLY the set a dense
  // O(n^2) reference keeps — bucketing prunes comparison pairs that cannot
  // overlap, never the suppression decisions.
  std::mt19937 rng( 20260913 );
  std::uniform_real_distribution<float> pos( 0.0f, 2000.0f );
  std::uniform_real_distribution<float> size( 8.0f, 60.0f );
  std::uniform_real_distribution<float> conf( 0.1f, 1.0f );
  std::vector<DetectionBox> boxes;
  boxes.reserve( 4000 );
  for ( int i = 0; i < 4000; ++i )
    boxes.push_back( DetectionBox{ pos( rng ), pos( rng ), size( rng ), size( rng ),
                                   i % 3, conf( rng ) } );

  const auto bucketed = nonMaxSuppression( boxes, 0.45 );
  // Dense reference: the pre-F-OPS-5 algorithm, verbatim.
  std::vector<DetectionBox> ordered = boxes;
  std::sort( ordered.begin(), ordered.end(), []( const DetectionBox &a, const DetectionBox &b ) {
    if ( a.confidence != b.confidence ) return a.confidence > b.confidence;
    if ( a.classId != b.classId ) return a.classId < b.classId;
    if ( a.x != b.x ) return a.x < b.x;
    if ( a.y != b.y ) return a.y < b.y;
    if ( a.w != b.w ) return a.w < b.w;
    return a.h < b.h;
  } );
  std::vector<DetectionBox> reference;
  std::vector<bool> suppressed( ordered.size(), false );
  auto iouOf = []( const DetectionBox &a, const DetectionBox &b ) {
    const double ax1 = a.x, ay1 = a.y, ax2 = a.x + a.w, ay2 = a.y + a.h;
    const double bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
    const double interW = std::max( 0.0, std::min( ax2, bx2 ) - std::max( ax1, bx1 ) );
    const double interH = std::max( 0.0, std::min( ay2, by2 ) - std::max( ay1, by1 ) );
    const double inter = interW * interH;
    const double areaA = std::max( 0.0, ax2 - ax1 ) * std::max( 0.0, ay2 - ay1 );
    const double areaB = std::max( 0.0, bx2 - bx1 ) * std::max( 0.0, by2 - by1 );
    const double unionArea = areaA + areaB - inter;
    return unionArea > 0.0 ? inter / unionArea : 0.0;
  };
  for ( std::size_t i = 0; i < ordered.size(); ++i )
  {
    if ( suppressed[i] ) continue;
    reference.push_back( ordered[i] );
    for ( std::size_t j = i + 1; j < ordered.size(); ++j )
      if ( !suppressed[j] && iouOf( ordered[i], ordered[j] ) > 0.45 )
        suppressed[j] = true;
  }
  REQUIRE( bucketed.size() == reference.size() );
  for ( std::size_t k = 0; k < reference.size(); ++k )
  {
    CAPTURE( k );
    CHECK( bucketed[k].confidence == reference[k].confidence );
    CHECK( bucketed[k].x == reference[k].x );
    CHECK( bucketed[k].y == reference[k].y );
  }

  // Cancellation: the probe throws through the pass instead of letting a
  // near-budget candidate set block the worker until the pass ends.
  std::vector<DetectionBox> victim = boxes;
  REQUIRE_THROWS_AS( dedupDetections( victim, 0.45,
                                      [] { throw std::runtime_error( "cancelled" ); } ),
                     std::runtime_error );

  // Near the default max_detections budget, disjoint boxes must resolve in
  // bounded time: the dense pass was O(n^2) (10^10 comparisons at 100k).
  std::vector<DetectionBox> budget;
  budget.reserve( 100000 );
  for ( int i = 0; i < 100000; ++i )
  {
    const float x = static_cast<float>( ( i % 500 ) * 4 );
    const float y = static_cast<float>( ( i / 500 ) * 4 );
    budget.push_back( DetectionBox{ x, y, 2.0f, 2.0f, 0, 0.5f } );
  }
  const auto kept = nonMaxSuppression( budget, 0.45 );
  CHECK( kept.size() == 100000 ); // nothing overlaps; everything survives
}

// ---------------------------------------------------------------------------
// The unified seam (T24/T25): four operators, one execution path
// ---------------------------------------------------------------------------

TEST_CASE( "the four model operators share the execution seam", "[models][seam]" )
{
  auto &registry = RSOperatorRegistry::instance();
  for ( const char *id : { "rs:infer", "rs:segment", "rs:detect", "rs:embedding" } )
  {
    const auto op = registry.create( id );
    REQUIRE( op );
    CHECK_FALSE( op->schema().isNull() );
    // Every model operator declares the streaming memory policy — none of
    // them materializes a whole raster.
    CHECK( op->memoryPolicy() == sicnu::operators::RSOperatorMemoryPolicy::Streaming );
  }
}

TEST_CASE( "rs:segment runs the service end-to-end with derived labels", "[models][seam]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "seg-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "seg-out.tif" ) );
  const QString weights = dir.filePath( QStringLiteral( "plane.onnx" ) );
  {
    QFile f( weights );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "plane-weights" ) );
  }
  // The registry is authoritative: the operator resolves the STABLE ID.
  std::string regError;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    R"({"name": "plane-model", "task": "segmentation", "framework": "planefw",
        "artifact": {"path": ")" + weights.toStdString() + R"("}})",
    "session", &regError ) );

  auto &registry = RSOperatorRegistry::instance();
  const auto op = registry.create( "rs:segment" );
  REQUIRE( op );

  Json::Value params( Json::objectValue );
  params["input"] = input.toStdString();
  params["model"] = "plane-model";
  params["output"] = output.toStdString();
  params["format"] = "labels";
  RSOperatorContext context;
  Json::Value result = op->run( params, context );

  CHECK( result["output"].asString() == output.toStdString() );
  CHECK( result["outBands"].asInt() == 1 );
  CHECK( fileExists( output ) );
  CHECK_FALSE( QFileInfo::exists( output + QStringLiteral( ".tmp~" ) ) );
  ModelCatalog::instance().unregister( "plane-model" );
}

TEST_CASE( "rs:embedding reports the feature dimension and mean vector", "[models][seam]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "emb-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "emb-out.tif" ) );

  ModelInfo model = planeModel( "embedding" );
  model.output.classes.clear();
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );

  CHECK( stats.outBands == 2 ); // the 2-channel feature stack
  int w = 0, h = 0;
  const auto values = readBand( output, 1, &w, &h );
  CHECK( values[0] == Catch::Approx( 1.0f ) );
  // The feature stack IS the embedding product: bounded, georeferenced.
  CHECK( stats.outWidth == 32 );
}

// ---------------------------------------------------------------------------
// Hardening 15/20: a sidecar-publish failure must restore the previous
// product WITH its provenance sidecar — the old code unlinked the old
// sidecar before the new one landed and never restored it, downgrading a
// verified product to MissingSidecar on rollback.
// ---------------------------------------------------------------------------

TEST_CASE( "a sidecar failure keeps the previous raster product and its provenance (hardening 15/20)",
           "[models][seam][publish][p15]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "p15-roll-in.tif" ), 16, 16 );
  const QString output = dir.filePath( QStringLiteral( "p15-roll-out.tif" ) );
  const QString sidecar = output + QStringLiteral( ".prov.json" );
  const QString weights = dir.filePath( QStringLiteral( "p15-plane.onnx" ) );
  {
    QFile f( weights );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "p15-plane-weights" ) );
  }
  std::string regError;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    R"({"name": "p15-plane-model", "task": "segmentation", "framework": "planefw",
        "artifact": {"path": ")" + weights.toStdString() + R"("}})",
    "session", &regError ) );

  auto runSegment = [ & ]() {
    const auto op = RSOperatorRegistry::instance().create( "rs:segment" );
    REQUIRE( op );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["model"] = "p15-plane-model";
    params["output"] = output.toStdString();
    RSOperatorContext context;
    return op->run( params, context );
  };

  // First run publishes the product + its provenance sidecar (Platform 8.0).
  REQUIRE_NOTHROW( runSegment() );
  REQUIRE( fileExists( output ) );
  REQUIRE( fileExists( sidecar ) );
  QFile originalSidecar( sidecar );
  REQUIRE( originalSidecar.open( QIODevice::ReadOnly ) );
  const QByteArray originalBytes = originalSidecar.readAll();
  originalSidecar.close();

  // Second run with the sidecar stage forced to fail (a DIRECTORY occupies
  // the stage path, so the stage write cannot succeed): the run must throw
  // AND the previous product+sidecar pair must come back intact.
  {
    const QString hostileStage = output + QStringLiteral( ".prov.json.stage~" );
    REQUIRE( QDir().mkpath( hostileStage ) );
    REQUIRE_THROWS( runSegment() );
    CHECK( fileExists( output ) );
    CHECK( fileExists( sidecar ) );
    QFile restoredSidecar( sidecar );
    REQUIRE( restoredSidecar.open( QIODevice::ReadOnly ) );
    CHECK( restoredSidecar.readAll() == originalBytes );
    CHECK_FALSE( QFileInfo::exists( output + QStringLiteral( ".prev~" ) ) );
    CHECK_FALSE( QFileInfo::exists( output + QStringLiteral( ".tmp~" ) ) );
    QDir().rmdir( hostileStage );
  }

  // With the obstacle gone the run succeeds and republishes normally.
  REQUIRE_NOTHROW( runSegment() );
  CHECK( fileExists( sidecar ) );
  ModelCatalog::instance().unregister( "p15-plane-model" );
}

// ---------------------------------------------------------------------------
// Completion 13/15: single-model detection products carry the SAME publish
// contract as every other model lane — a provenance sidecar (exp-rs-prov/1)
// the consumer-side verifier accepts, with rollback that restores the
// previous product+sidecar PAIR. Master published detection vectors with no
// sidecar at all: verifyProductAgainstModel reported MissingSidecar
// deterministically for the whole lane.
// ---------------------------------------------------------------------------

namespace {

/// Detection head fake with an explicit provider identity: emits one
/// candidate per batch element in the xywh_objectness / channels_first
/// layout, and reports execution-provider details so the test can prove the
/// execution identity reaches BOTH the payload and the sidecar.
class DetectHeadRuntime final : public IModelRuntime
{
  public:
    DetectHeadRuntime( std::string artifact )
        : m_artifact( std::move( artifact ) ) {}

    std::string framework() const override { return "dethead-c"; }
    std::string backendName() const override { return "detection_fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    sicnu::operators::runtime::ProviderRuntimeDetails providerDetails() const override
    {
      return { "DummyExecutionProvider", "0.0.0-test" };
    }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int batch = blob.size[0];
      const int channels = 5 + 2; // xywh + objectness + 2 classes
      int dims[3] = { batch, channels, 1 };
      cv::Mat out( 3, dims, CV_32F );
      out.setTo( 0 );
      // One candidate per batch element: center (8,8), 4x4, objectness 1.0,
      // class-0 score 0.9 — clears a 0.25 conf gate, class 1 stays silent.
      for ( int b = 0; b < batch; ++b )
      {
        float *plane = out.ptr<float>( b );
        plane[0] = 8.0f;
        plane[dims[2]] = 8.0f;
        plane[2 * dims[2]] = 4.0f;
        plane[3 * dims[2]] = 4.0f;
        plane[4 * dims[2]] = 1.0f;
        plane[5 * dims[2]] = 0.9f;
        plane[6 * dims[2]] = 0.2f;
      }
      return out;
    }

  private:
    std::string m_artifact;
};

struct DetectProviderGuard
{
    DetectProviderGuard()
    {
      ModelRuntimeRegistry::instance().registerProvider(
        "dethead-c",
        []( const ModelInfo &model, const ModelHardwareCapabilities &,
            std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          return std::make_shared<DetectHeadRuntime>( model.resolvedArtifactPath );
        } );
    }
};

/// Registers the detection manifest and returns the catalog identity tag.
std::string registerDetectModel( const QTemporaryDir &dir )
{
  const QString weights = dir.filePath( QStringLiteral( "det-c.onnx" ) );
  {
    QFile f( weights );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "det-c-weights" ) );
  }
  Json::Value json( Json::objectValue );
  json["name"] = "det-sidecar-model";
  json["task"] = "detection";
  json["framework"] = "dethead-c";
  json["artifact"]["path"] = weights.toStdString();
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
  Json::Value classes( Json::arrayValue );
  classes.append( "tree" );
  classes.append( "shrub" );
  detection["classes"] = classes;
  std::string error;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QStringLiteral( "det-sidecar-model/model.json" ) ).toStdString(), &error ) );
  const auto model = ModelCatalog::instance().find( "det-sidecar-model" );
  REQUIRE( model.has_value() );
  return model->identityTag();
}

ModelExecutionRequest detectRequest( const QTemporaryDir &dir )
{
  const QString input = dir.filePath( QStringLiteral( "det-c-input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 3, GDT_Float32 );
  builder.withConstantValue( 1, 10.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = dir.filePath( QStringLiteral( "det-c-out.gpkg" ) ).toStdString();
  request.modelReference = "det-sidecar-model";
  request.asDetection = true;
  return request;
}

QByteArray readFileBytes( const QString &path )
{
  QFile f( path );
  REQUIRE( f.open( QIODevice::ReadOnly ) );
  return f.readAll();
}

} // namespace

TEST_CASE( "a single-model detection product publishes a verifier-accepted provenance sidecar",
           "[models][detect][publish][p13c]" )
{
  ProviderGuard guard;
  const DetectProviderGuard detectGuard;
  QTemporaryDir dir;
  const std::string identityTag = registerDetectModel( dir );
  ModelExecutionRequest request = detectRequest( dir );
  const QString output = QString::fromStdString( request.outputPath );
  const QString sidecar = output + QStringLiteral( ".prov.json" );

  RSOperatorContext context;
  sicnu::operators::runtime::ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );
  REQUIRE( fileExists( output ) );

  // The lane publishes the same sidecar contract as the raster lanes.
  REQUIRE( fileExists( sidecar ) );
  const Json::Value prov = [ & ] {
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    const QByteArray bytes = readFileBytes( sidecar );
    std::string errors;
    REQUIRE( reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &parsed,
                            &errors ) );
    return parsed;
  }();
  CHECK( prov["schema"].asString() == "exp-rs-prov/1" );
  CHECK( prov["model"]["identity_tag"].asString() == identityTag );
  CHECK( prov["model"]["task"].asString() == "detection" );
  CHECK( prov["model"]["framework"].asString() == "dethead-c" );
  CHECK( prov["execution"]["backend"].asString() == "detection_fake" );
  CHECK( prov["execution"]["device"].asString() == "cpu" );
  CHECK( prov["execution"]["execution_provider"].asString() == "DummyExecutionProvider" );
  CHECK( prov["execution"]["runtime_version"].asString() == "0.0.0-test" );
  // The run identity: effective thresholds + the class vocabulary + counts.
  CHECK( prov["detection"]["conf_threshold"].asDouble() == Catch::Approx( 0.25 ) );
  CHECK( prov["detection"]["nms_iou"].asDouble() == Catch::Approx( 0.45 ) );
  CHECK( prov["detection"]["classes"][0].asString() == "tree" );
  CHECK( prov["detection"]["detections_kept"].asInt() == 1 );
  CHECK( prov["output"]["format"].asString() == "vector" );
  CHECK( prov["output"]["features"].asInt() == 1 );
  // The input grid: what was actually fed (grid provenance parity).
  REQUIRE( prov["inputs"].isArray() );
  REQUIRE( prov["inputs"].size() == 1 );
  CHECK( prov["inputs"][0]["path"].asString() == request.inputPath );
  CHECK( prov["inputs"][0]["width"].asInt() == 16 );
  CHECK( prov["inputs"][0]["height"].asInt() == 16 );
  CHECK( prov["inputs"][0]["crs"].asString() == "EPSG:4326" );
  CHECK( !prov["created_utc"].asString().empty() );

  // The consumer-side verifier accepts the product instead of reporting the
  // deterministic MissingSidecar the lane used to earn.
  const auto verdict =
    sicnu::operators::runtime::verifyProductAgainstModel( request.outputPath, "det-sidecar-model" );
  CHECK( verdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::Ok );

  // Payload parity: the detection payload carries the provider identity block
  // the raster payload already publishes (Platform 9.0 M8).
  CHECK( result.payload["provider"]["execution_provider"].asString() == "DummyExecutionProvider" );
  CHECK( result.payload["provider"]["runtime_version"].asString() == "0.0.0-test" );
  ModelCatalog::instance().unregister( "det-sidecar-model" );
}

TEST_CASE( "a detection sidecar failure restores the previous vector product and its provenance",
           "[models][detect][publish][p13c]" )
{
  ProviderGuard guard;
  const DetectProviderGuard detectGuard;
  QTemporaryDir dir;
  ( void )registerDetectModel( dir );
  ModelExecutionRequest request = detectRequest( dir );
  const QString output = QString::fromStdString( request.outputPath );
  const QString sidecar = output + QStringLiteral( ".prov.json" );
  RSOperatorContext context;

  auto runOnce = [ & ]() {
    ModelExecutionRequest once = request;
    REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( once, context ) );
  };

  // First run publishes the product + its provenance sidecar.
  runOnce();
  REQUIRE( fileExists( output ) );
  REQUIRE( fileExists( sidecar ) );
  const QByteArray originalSidecar = readFileBytes( sidecar );

  // Second run with the sidecar stage forced to fail (a DIRECTORY occupies
  // the stage path): the run throws AND the previous product+sidecar pair
  // comes back intact — never a verified product downgraded to MissingSidecar.
  {
    const QString hostileStage = sidecar + QStringLiteral( ".stage~" );
    REQUIRE( QDir().mkpath( hostileStage ) );
    ModelExecutionRequest second = request;
    REQUIRE_THROWS( sicnu::operators::runtime::runModelInference( second, context ) );
    CHECK( fileExists( output ) );
    CHECK( fileExists( sidecar ) );
    CHECK( readFileBytes( sidecar ) == originalSidecar );
    CHECK_FALSE( QFileInfo::exists( output + QStringLiteral( ".det-prev~" ) ) );
    QDir().rmdir( hostileStage );
  }

  // With the obstacle gone the run succeeds and republishes normally.
  runOnce();
  CHECK( fileExists( sidecar ) );
  CHECK_FALSE( QFileInfo::exists( output + QStringLiteral( ".det-prev~" ) ) );
  ModelCatalog::instance().unregister( "det-sidecar-model" );
}
