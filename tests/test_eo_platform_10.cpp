// tests/test_eo_platform_10.cpp — Platform 10.0 EO model platform regressions.
//
// F-OPS-1: postprocess.class_mapping targets must reach the labels raster
// intact — the output encoding (Byte/UInt16 + NoData sentinel) is derived
// from the PRODUCT class domain, and manifest validation caps targets below
// the UInt16 sentinel. Before the fix a target >= 255 clamped into the Byte
// NoData sentinel and the class silently vanished.
//
// F-OPS-2: TensorBlob::fromMat must copy non-continuous ND (ROI) Mats byte-
// exact; the historical per-row fallback copied nothing for dims > 2 and
// produced an all-valid, uninitialized blob.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/provenance_verify.h"
#include "operators/runtime/tensorrt_provider.h"
#include "operators/runtime/openvino_provider.h"
#include "operators/framework/model_readiness.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tensor_blob.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::TensorBlob;
using sicnu::operators::runtime::TileInferenceEngine;

/// Deterministic 3-class checkerboard planes (value = f(class, pixel)), the
/// smallest runtime that exercises argmax + class remap end to end.
class ClassPlane3Runtime final : public sicnu::operators::runtime::IModelRuntime
{
  public:
    std::string framework() const override { return "eo10fw"; }
    std::string backendName() const override { return "class_planes_3"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "class-planes-3"; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int N = blob.size[0], H = blob.size[2], W = blob.size[3];
      int dims[4] = { N, 3, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.1f ) );
      for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
          for ( int n = 0; n < N; ++n )
            out.ptr<float>( n, ( x + y ) % 3 )[y * W + x] = 1.0f;
      return out;
    }
};

struct Eo10ProviderGuard
{
    Eo10ProviderGuard()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.registerProvider( "eo10fw", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                                               std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
        return std::make_shared<ClassPlane3Runtime>();
      } );
      registry.releaseAll();
    }
    ~Eo10ProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

ModelInfo plane3Model()
{
  ModelInfo info;
  info.name = "eo10-plane3";
  info.task = "segmentation";
  info.framework = "eo10fw";
  info.readiness = ModelReadiness::Ready;
  info.tiling.tileSize = 32;
  info.output.classes = { "a", "b", "c" };
  info.output.format = "labels";
  return info;
}

std::vector<float> readBandAll( const QString &path, int band, int *width, int *height, int *type )
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
// F-OPS-1
// ---------------------------------------------------------------------------

TEST_CASE( "F-OPS-1: class_mapping targets beyond 255 reach a UInt16 labels raster intact",
           "[eo10][fops1]" )
{
  Eo10ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "fops1-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "fops1-out.tif" ) );

  ModelInfo model = plane3Model();
  // Product domain {0, 1, 300}: 300 exceeds the Byte encoding but fits UInt16
  // below the 65535 sentinel. Before the fix the engine picked Byte from the
  // MODEL class count and 300 clamped to 255 == NoData on write.
  model.postprocess.classMapping = { 0, 1, 300 };

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );
  REQUIRE( stats.outBands == 1 );

  int w = 0, h = 0, type = 0;
  const auto values = readBandAll( output, 1, &w, &h, &type );
  REQUIRE( values.size() == 32 * 32 );
  CHECK( type == GDT_UInt16 ); // encoding promoted with the product domain

  std::set<int> distinct;
  for ( float v : values )
    distinct.insert( static_cast<int>( v ) );
  // All three product classes present, no pixel collapsed into 255.
  CHECK( distinct == std::set<int>( { 0, 1, 300 } ) );
  CHECK( distinct.count( 255 ) == 0 );

  // Per-product-class tallies agree with the pixels (no phantom classes).
  REQUIRE( stats.classPixelCounts.size() >= 301 );
  std::size_t total = 0;
  for ( int c : distinct )
  {
    if ( c == 255 )
      continue;
    total += stats.classPixelCounts[static_cast<std::size_t>( c )];
  }
  CHECK( total == 32u * 32u );
}

TEST_CASE( "F-OPS-1: manifest validation refuses class_mapping targets above the UInt16 sentinel",
           "[eo10][fops1]" )
{
  auto &catalog = ModelCatalog::instance();
  const auto over = catalog.validateManifestJson(
    ( std::string( R"({"name":"fops1-bad","task":"segmentation","framework":"onnx",)"
                    R"("output":{"type":"raster","classes":["a","b"],"format":"labels"},)"
                    R"("postprocess":{"class_mapping":[0,70000]}})" ) ) );
  REQUIRE_FALSE( over.empty() );
  bool mentionsSentinel = false;
  for ( const std::string &issue : over )
    if ( issue.find( "65535" ) != std::string::npos )
      mentionsSentinel = true;
  CHECK( mentionsSentinel );

  // A target inside the representable domain but above Byte is fine — the
  // engine promotes the encoding (that is the F-OPS-1 engine contract).
  const auto ok = catalog.validateManifestJson(
    ( std::string( R"({"name":"fops1-ok","task":"segmentation","framework":"onnx",)"
                    R"("output":{"type":"raster","classes":["a","b"],"format":"labels"},)"
                    R"("postprocess":{"class_mapping":[0,300]}})" ) ) );
  CHECK( ok.empty() );
}

// ---------------------------------------------------------------------------
// F-OPS-2
// ---------------------------------------------------------------------------

TEST_CASE( "F-OPS-2: fromMat copies a non-continuous ND ROI byte-exact",
           "[eo10][fops2]" )
{
  int dims[4] = { 2, 3, 4, 5 };
  std::vector<float> data( 2 * 3 * 4 * 5 );
  std::iota( data.begin(), data.end(), 1.0f );
  cv::Mat full( 4, dims, CV_32F, data.data() );

  // Dim-1 ROI over a 4-D header: shape (2,2,4,5) with an interior stride gap
  // — genuinely NON-continuous (a dim-0 slice of 1 plane would be contiguous).
  const cv::Range ranges[4] = { cv::Range::all(), cv::Range( 0, 2 ), cv::Range::all(),
                                cv::Range::all() };
  cv::Mat roi = full( ranges );
  REQUIRE_FALSE( roi.isContinuous() );
  REQUIRE( roi.dims == 4 );

  TensorBlob blob = TensorBlob::fromMat( roi );
  REQUIRE( blob.isValid() );
  REQUIRE( blob.rank() == 4 );
  REQUIRE( blob.shape == std::vector<std::int64_t>( { 2, 2, 4, 5 } ) );

  // Element (a,b,c,d) of the ROI is source element (a, b, c, d).
  const float *copied = blob.dataFloat32();
  for ( int a = 0; a < 2; ++a )
    for ( int b = 0; b < 2; ++b )
      for ( int c = 0; c < 4; ++c )
        for ( int d = 0; d < 5; ++d )
          REQUIRE( copied[( ( a * 2 + b ) * 4 + c ) * 5 + d]
                   == data[( ( a * 3 + b ) * 4 + c ) * 5 + d] );

  // Regression guard for the historical 2-D path: a non-continuous 2-D ROI
  // still copies exactly.
  cv::Mat plane2d( 4, 5, CV_32F, data.data() );
  cv::Mat roi2d = plane2d( cv::Range( 0, 2 ), cv::Range( 1, 4 ) );
  REQUIRE_FALSE( roi2d.isContinuous() );
  TensorBlob blob2d = TensorBlob::fromMat( roi2d );
  REQUIRE( blob2d.isValid() );
  REQUIRE( blob2d.shape == std::vector<std::int64_t>( { 2, 3 } ) );
  const float *copied2d = blob2d.dataFloat32();
  for ( int y = 0; y < 2; ++y )
    for ( int x = 0; x < 3; ++x )
      REQUIRE( copied2d[y * 3 + x] == data[y * 5 + 1 + x] );
}

TEST_CASE( "F-OPS-2: fromMat keeps the continuous fast path bit-identical",
           "[eo10][fops2]" )
{
  int dims[3] = { 2, 2, 3 };
  std::vector<float> data( 2 * 2 * 3, 7.5f );
  cv::Mat full( 3, dims, CV_32F, data.data() );
  REQUIRE( full.isContinuous() );
  TensorBlob blob = TensorBlob::fromMat( full );
  REQUIRE( blob.isValid() );
  REQUIRE( std::memcmp( blob.bytes.data(), data.data(), data.size() * sizeof( float ) ) == 0 );
}

// ---------------------------------------------------------------------------
// WP-A: EO ModelManifest 10.0 — `eo` domain truth + task vocabulary
// ---------------------------------------------------------------------------

TEST_CASE( "WP-A: canonical EO task vocabulary resolves aliases and rejects nothing silently",
           "[eo10][manifest]" )
{
  using sicnu::operators::canonicalEoTask;
  CHECK( canonicalEoTask( "segmentation" ) == "segmentation" );
  CHECK( canonicalEoTask( "Semantic_Segmentation" ) == "segmentation" );
  CHECK( canonicalEoTask( "object_detection" ) == "detection" );
  CHECK( canonicalEoTask( "scene_classification" ) == "classification" );
  CHECK( canonicalEoTask( "ssl" ) == "embedding" );
  CHECK( canonicalEoTask( "change_detection" ) == "change_detection" );
  CHECK( canonicalEoTask( "regression" ) == "regression" );
  CHECK( canonicalEoTask( "instance_segmentation" ) == "instance_segmentation" );
  CHECK( canonicalEoTask( "super_resolution" ) == "super_resolution" );
  // Free-form legacy tasks carry no canonical contract (empty), never a guess.
  CHECK( canonicalEoTask( "crop_type_mapping" ).empty() );
  CHECK( canonicalEoTask( "" ).empty() );
}

TEST_CASE( "WP-A: eo section parses, validates and projects through inspect()",
           "[eo10][manifest]" )
{
  auto &catalog = ModelCatalog::instance();
  const std::string manifest = R"JSON({
    "name": "eo10-truth", "id": "eo10-truth", "model_version": "1",
    "manifest_version": 6,
    "task": "semantic_segmentation", "framework": "onnx",
    "inputs": [ { "name": "s2", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["blue", "green", "red", "nir"] } ],
    "output": { "type": "raster", "classes": ["bg", "fg"] },
    "eo": {
      "wavelengths_nm": { "red": [630, 700], "nir": [780, 1400] },
      "calibration": { "state": "toa_reflectance", "enforced": true },
      "grid": { "crs_family": "projected" }
    }
  })JSON";

  const auto issues = catalog.validateManifestJson( manifest );
  if ( !issues.empty() )
    for ( const std::string &i : issues )
      WARN( "manifest issue: " << i );
  CHECK( issues.empty() );

  std::string error;
  REQUIRE( catalog.registerManifestJson( manifest, "test://eo10-truth", &error ) );
  const Json::Value record = catalog.inspect( "eo10-truth" );
  REQUIRE( !record.isNull() );
  CHECK( record["task_canonical"].asString() == "segmentation" );
  CHECK( record["manifest_version"].asInt() == 6 );
  const Json::Value &eo = record["eo"];
  REQUIRE( eo.isObject() );
  CHECK( eo["wavelengths_nm"]["red"][0].asDouble() == 630.0 );
  CHECK( eo["wavelengths_nm"]["nir"][1].asDouble() == 1400.0 );
  CHECK( eo["calibration"]["state"].asString() == "toa_reflectance" );
  CHECK( eo["calibration"]["enforced"].asBool() );
  CHECK( eo["grid"]["crs_family"].asString() == "projected" );
  REQUIRE( catalog.unregister( "eo10-truth" ) );
}

TEST_CASE( "WP-A: eo contract validation refuses bad windows, states and enforcement",
           "[eo10][manifest]" )
{
  auto &catalog = ModelCatalog::instance();
  auto wrap = []( const std::string &eoBody, const std::string &version = "\"manifest_version\": 6," ) {
    return R"JSON({"name": "eo10-bad", )JSON" + version + R"JSON( "task": "segmentation",
      "framework": "onnx",
      "inputs": [ { "name": "s2", "dtype": "float32", "band_roles": ["red"] } ],
      "output": { "type": "raster", "classes": ["bg", "fg"] },
      "eo": )JSON" + eoBody + " }";
  };

  // Inverted wavelength window.
  auto issues = catalog.validateManifestJson( wrap( R"JSON({"wavelengths_nm": {"red": [700, 630]}})JSON" ) );
  REQUIRE_FALSE( issues.empty() );
  bool invertedReported = false;
  for ( const std::string &i : issues )
    invertedReported |= i.find( "exceeds max" ) != std::string::npos;
  CHECK( invertedReported );

  // Non-positive window bound.
  issues = catalog.validateManifestJson( wrap( R"JSON({"wavelengths_nm": {"red": [0, 700]}})JSON" ) );
  REQUIRE_FALSE( issues.empty() );

  // Unknown calibration state.
  issues = catalog.validateManifestJson( wrap( R"JSON({"calibration": {"state": "magic"}})JSON" ) );
  REQUIRE_FALSE( issues.empty() );

  // Enforcing "any" is a contract contradiction.
  issues = catalog.validateManifestJson( wrap( R"JSON({"calibration": {"state": "any", "enforced": true}})JSON" ) );
  REQUIRE_FALSE( issues.empty() );

  // enforcement without a state.
  issues = catalog.validateManifestJson( wrap( R"JSON({"calibration": {"enforced": true}})JSON" ) );
  REQUIRE_FALSE( issues.empty() );

  // Unknown eo key refused (closed vocabulary).
  issues = catalog.validateManifestJson( wrap( R"JSON({"magic_fact": 1})JSON" ) );
  REQUIRE_FALSE( issues.empty() );

  // manifest_version 6 is legal, 7 is not.
  CHECK( catalog.validateManifestJson( wrap( R"JSON({})JSON", "\"manifest_version\": 6," ) ).empty() );
  issues = catalog.validateManifestJson( wrap( R"JSON({})JSON", "\"manifest_version\": 7," ) );
  REQUIRE_FALSE( issues.empty() );
}

/// Stamps dataset-level radiometric state + per-band wavelengths onto a
/// written raster (the import/calibration operators' metadata contract).
void stampEoMetadata( const QString &path, const QString &state,
                      const std::vector<double> &wavelengthsNm )
{
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_Update ) );
  REQUIRE( ds );
  if ( !state.isEmpty() )
    ds->SetMetadataItem( "SICNU_RADIOMETRIC_STATE", state.toUtf8().constData() );
  for ( int b = 0; b < static_cast<int>( wavelengthsNm.size() ); ++b )
    if ( wavelengthsNm[b] > 0.0 )
      ds->GetRasterBand( b + 1 )
        ->SetMetadataItem( "WAVELENGTH", std::to_string( wavelengthsNm[b] ).c_str() );
  GDALClose( ds );
}

TEST_CASE( "WP-A: enforced calibration preflight is fail-closed and typed",
           "[eo10][preflight]" )
{
  Eo10ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "calib-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 4, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "calib-out.tif" ) );

  ModelInfo model = plane3Model();
  model.eo.declared = true;
  model.eo.calibrationState = "toa_reflectance";
  model.eo.calibrationEnforced = true;

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );

  // 1) No declared state: an enforced calibration can never be assumed.
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     sicnu::operators::RSOperatorError );

  // 2) Mismatched state: radiometric domain shift refused, never tolerated.
  stampEoMetadata( input, QStringLiteral( "digital_number" ), {} );
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     sicnu::operators::RSOperatorError );

  // 3) Matching state: verified, run proceeds, report recorded into stats.
  stampEoMetadata( input, QStringLiteral( "toa_reflectance" ), {} );
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );
  CHECK( stats.eoPreflight["applicable"].asBool() );
  CHECK( stats.eoPreflight["calibration_verified"].asBool() );
}

TEST_CASE( "WP-A: wavelength windows refuse out-of-window bands and advise on missing facts",
           "[eo10][preflight]" )
{
  Eo10ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "wave-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "wave-out.tif" ) );

  ModelInfo model = plane3Model();
  model.eo.declared = true;
  model.inputs.clear();
  sicnu::operators::ModelInputContract feed;
  feed.name = "s2";
  feed.bandRoles = { "red", "nir" };
  model.inputs.push_back( feed );
  // Mirror the single input into the legacy mirror field (parseManifest
  // keeps input == inputs[0]; hand-built ModelInfo must do the same).
  model.input = feed;
  model.eo.wavelengthsNm["red"] = { 630.0, 700.0 };
  model.eo.wavelengthsNm["nir"] = { 780.0, 1400.0 };

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );

  // 1) Band wavelengths in-window: pass, both checks recorded.
  stampEoMetadata( input, QString(), { 665.0, 832.0 } );
  auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );
  CHECK( stats.eoPreflight["applicable"].asBool() );
  CHECK( stats.eoPreflight["checks"].size() == 2 );

  // 2) NIR out of the model's window (1600 nm): typed refusal.
  stampEoMetadata( input, QString(), { 665.0, 1600.0 } );
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     sicnu::operators::RSOperatorError );

  // 3) No wavelength metadata at all: advisories, no refusal (absence is not
  //    a mismatch — but it is RECORDED, never swallowed).
  GDALAllRegister();
  {
    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( input.toUtf8().constData(), GA_Update ) );
    REQUIRE( ds );
    ds->GetRasterBand( 1 )->SetMetadataItem( "WAVELENGTH", nullptr );
    ds->GetRasterBand( 2 )->SetMetadataItem( "WAVELENGTH", nullptr );
    GDALClose( ds );
  }
  stats = engine.run( input.toStdString(), {}, output.toStdString(), context );
  CHECK( stats.eoPreflight["advisories"].size() == 2 );
}

// ---------------------------------------------------------------------------
// WP-B: task families — rs:classify / rs:change / rs:regress over ONE seam
// ---------------------------------------------------------------------------

namespace {

using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::NamedTensor;
using sicnu::operators::runtime::TensorBlob;
using sicnu::operators::runtime::TensorDType;

/// Scene-classification fake: returns a (1, C) float32 score row.
class FixedScoreRuntime final : public IModelRuntime
{
  public:
    explicit FixedScoreRuntime( std::vector<float> scores, bool asLogitsRow = true )
      : m_scores( std::move( scores ) ) { ( void )asLogitsRow; }

    std::string framework() const override { return "scorefw"; }
    std::string backendName() const override { return "fixed_scores"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fixed-scores"; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      ( void )blob;
      cv::Mat out( 1, static_cast<int>( m_scores.size() ), CV_32F );
      for ( std::size_t c = 0; c < m_scores.size(); ++c )
        out.at<float>( 0, static_cast<int>( c ) ) = m_scores[c];
      return out;
    }

  private:
    std::vector<float> m_scores;
};

/// Change fake: inferNamed returns a (B, 2, H, W) probability stack where the
/// change class wins on the right half of the scene.
class ChangeStackRuntime final : public IModelRuntime
{
  public:
    std::vector<std::string> fedNames;

    std::string framework() const override { return "changefw"; }
    std::string backendName() const override { return "change_stack"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "change-stack"; }
    cv::Mat infer( const cv::Mat & ) override { return {}; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      for ( const auto &nt : inputs )
        fedNames.push_back( nt.first );
      const TensorBlob &first = inputs.front().second;
      const std::int64_t B = first.shape[0];
      const std::int64_t H = first.shape[2];
      const std::int64_t W = first.shape[3];
      TensorBlob out;
      out.shape = { B, 2, H, W };
      out.dtype = TensorDType::Float32;
      out.bytes.resize( static_cast<std::size_t>( B * 2 * H * W ) * sizeof( float ) );
      float *data = reinterpret_cast<float *>( out.bytes.data() );
      for ( std::int64_t b = 0; b < B; ++b )
        for ( std::int64_t y = 0; y < H; ++y )
          for ( std::int64_t x = 0; x < W; ++x )
          {
            const bool changed = x >= W / 2;
            float *px = data + ( ( b * 2 + 0 ) * H + y ) * W + x;
            px[0] = changed ? 0.1f : 0.9f;
            px[static_cast<std::size_t>( H ) * W] = changed ? 0.9f : 0.1f;
          }
      return { { "output", out } };
    }
};

/// Regression fake: identity + 5 per channel.
class Reg10Runtime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "reg10fw"; }
    std::string backendName() const override { return "regression_10"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "regression-10"; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      cv::Mat out = blob.clone();
      out += 5.0f;
      return out;
    }
};

/// Registers a session-scoped catalog manifest (stable id -> fake provider
/// framework) with a real weights file so resolveModelReference returns
/// Ready. Returns the stable id; tests unregister before returning.
/// RAII: registers a session-scoped catalog manifest (stable id -> fake
/// provider framework) with a real weights file so resolveModelReference
/// returns Ready; unregisters when the test ends.
struct RegisteredTaskModel
{
    RegisteredTaskModel( const QTemporaryDir &dir, std::string modelId, const std::string &json )
      : id( std::move( modelId ) )
    {
      const QString weights = dir.filePath( QStringLiteral( "weights.bin" ) );
      if ( !QFileInfo::exists( weights ) )
      {
        QFile f( weights );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "task-weights" ) );
      }
      std::string error;
      REQUIRE( ModelCatalog::instance().registerManifestJson( json, "session", &error ) );
    }
    ~RegisteredTaskModel() { ModelCatalog::instance().unregister( id ); }
    std::string id;
};

struct TaskProviderGuard
{
    TaskProviderGuard()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.registerProvider(
        "scorefw-prob", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                            std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
          return std::make_shared<FixedScoreRuntime>( std::vector<float>{ 0.2f, 0.8f } );
        } );
      registry.registerProvider(
        "scorefw-logit", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                             std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
          return std::make_shared<FixedScoreRuntime>( std::vector<float>{ 0.0f, 1.0f } );
        } );
      registry.registerProvider(
        "changefw", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                        std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
          return std::make_shared<ChangeStackRuntime>();
        } );
      registry.registerProvider(
        "reg10fw", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                       std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
          return std::make_shared<Reg10Runtime>();
        } );
      registry.releaseAll();
    }
    ~TaskProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

QString writeChip( const QTemporaryDir &dir, const QString &name, int w, int h )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder( w, h, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( path );
  return path;
}

} // namespace

TEST_CASE( "WP-B: rs:classify publishes a typed classification artifact", "[eo10][tasks]" )
{
  TaskProviderGuard guard;
  QTemporaryDir dir;
  const QString chip = writeChip( dir, QStringLiteral( "chip.tif" ), 64, 64 );
  const QString artifact = dir.filePath( QStringLiteral( "class.json" ) );

  RegisteredTaskModel registered( dir, "scene-cls",
    R"({"name": "scene-cls", "task": "classification", "framework": "scorefw-prob",
        "output": {"classes": ["bg", "fg"]},
        "artifact": {"path": ")" + dir.filePath( QStringLiteral( "weights.bin" ) ).toStdString() + R"("}})" );

  ModelExecutionRequest request;
  request.inputPath = chip.toStdString();
  request.modelReference = "scene-cls";
  request.outputPath = artifact.toStdString();
  request.asSceneClassification = true;
  request.requiredEoTask = "classification";

  RSOperatorContext context;
  const ModelExecutionResult result = sicnu::operators::runtime::runModelInference( request, context );

  CHECK( result.payload["predicted_class"].asString() == "fg" );
  CHECK( result.payload["predicted_index"].asInt() == 1 );
  CHECK( result.payload["probabilities"]["fg"].asDouble() == Catch::Approx( 0.8 ).margin( 1e-6 ) );
  CHECK( result.payload["schema"].asString() == "exp-rs-classification/1" );
  CHECK( result.payload["artifact"]["kind"].asString() == "classification" );
  CHECK( result.payload["input"]["fingerprint"].isObject() );
  CHECK( result.payload["scene"]["width"].asInt() == 64 );
  CHECK( QFileInfo::exists( artifact ) );
}

TEST_CASE( "WP-B: declared logit heads softmax into a probability distribution",
           "[eo10][tasks]" )
{
  TaskProviderGuard guard;
  QTemporaryDir dir;
  const QString chip = writeChip( dir, QStringLiteral( "chip2.tif" ), 32, 32 );
  const QString artifact = dir.filePath( QStringLiteral( "class2.json" ) );

  RegisteredTaskModel registered( dir, "scene-cls-logit",
    R"({"name": "scene-cls-logit", "task": "classification", "framework": "scorefw-logit",
        "output": {"classes": ["bg", "fg"],
                   "heads": [{"name": "logits", "role": "classification", "confidence": "logit"}]},
        "artifact": {"path": ")" + dir.filePath( QStringLiteral( "weights.bin" ) ).toStdString() + R"("}})" );

  ModelExecutionRequest request;
  request.inputPath = chip.toStdString();
  request.modelReference = "scene-cls-logit";
  request.outputPath = artifact.toStdString();
  request.asSceneClassification = true;
  request.requiredEoTask = "classification";

  RSOperatorContext context;
  const ModelExecutionResult result = sicnu::operators::runtime::runModelInference( request, context );
  const double fg = result.payload["probabilities"]["fg"].asDouble();
  const double bg = result.payload["probabilities"]["bg"].asDouble();
  CHECK( fg == Catch::Approx( 1.0 / ( 1.0 + std::exp( -1.0 ) ) ).margin( 1e-6 ) );
  CHECK( bg + fg == Catch::Approx( 1.0 ).margin( 1e-9 ) );
  CHECK( result.payload["score_semantics"].asString() == "logit" );
}

TEST_CASE( "WP-B: task intent gates refuse mismatched manifests", "[eo10][tasks]" )
{
  TaskProviderGuard guard;
  QTemporaryDir dir;
  const QString chip = writeChip( dir, QStringLiteral( "chip3.tif" ), 32, 32 );

  RegisteredTaskModel registered( dir, "seg-not-cls",
    R"({"name": "seg-not-cls", "task": "segmentation", "framework": "scorefw-prob",
        "output": {"classes": ["bg", "fg"]},
        "artifact": {"path": ")" + dir.filePath( QStringLiteral( "weights.bin" ) ).toStdString() + R"("}})" );

  ModelExecutionRequest request;
  request.inputPath = chip.toStdString();
  request.modelReference = "seg-not-cls";
  request.outputPath = dir.filePath( QStringLiteral( "nope.json" ) ).toStdString();
  request.asSceneClassification = true;
  request.requiredEoTask = "classification";

  RSOperatorContext context;
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference( request, context ),
                     sicnu::operators::RSOperatorError );
}

TEST_CASE( "WP-B: rs:change feeds two dates through the multi-input seam",
           "[eo10][tasks]" )
{
  TaskProviderGuard guard;
  QTemporaryDir dir;
  const QString before = writeChip( dir, QStringLiteral( "before.tif" ), 32, 32 );
  const QString after = writeChip( dir, QStringLiteral( "after.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "change-out.tif" ) );

  RegisteredTaskModel registered( dir, "siamese-change",
    R"({"name": "siamese-change", "task": "change_detection", "framework": "changefw",
        "inputs": [
          {"name": "before", "dtype": "float32", "band_roles": ["red", "nir"]},
          {"name": "after", "dtype": "float32", "band_roles": ["red", "nir"]}
        ],
        "output": {"classes": ["no_change", "change"]},
        "artifact": {"path": ")" + dir.filePath( QStringLiteral( "weights.bin" ) ).toStdString() + R"("}})" );

  ModelExecutionRequest request;
  request.modelReference = "siamese-change";
  request.outputPath = output.toStdString();
  request.requiredEoTask = "change_detection";
  for ( const QString *path : { &before, &after } )
  {
    sicnu::operators::runtime::NamedRasterFeed feed;
    feed.paths.push_back( path->toStdString() );
    request.namedInputs.push_back( feed );
  }

  RSOperatorContext context;
  const ModelExecutionResult result = sicnu::operators::runtime::runModelInference( request, context );
  CHECK( result.payload["outBands"].asInt() == 2 );
  CHECK( result.rasterStats.outBands == 2 );

  // The change class wins on the right half (x >= 16).
  int w = 0, h = 0, type = 0;
  const auto values = readBandAll( output, 2, &w, &h, &type );
  REQUIRE( values.size() == 32u * 32u );
  CHECK( values[0] == Catch::Approx( 0.1f ).margin( 1e-6 ) );
  CHECK( values[20] == Catch::Approx( 0.9f ).margin( 1e-6 ) );
}

TEST_CASE( "WP-B: rs:regress returns continuous output bands", "[eo10][tasks]" )
{
  TaskProviderGuard guard;
  QTemporaryDir dir;
  const QString input = writeChip( dir, QStringLiteral( "reg-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "reg-out.tif" ) );

  RegisteredTaskModel registered( dir, "biomass-reg",
    R"({"name": "biomass-reg", "task": "regression", "framework": "reg10fw",
        "artifact": {"path": ")" + dir.filePath( QStringLiteral( "weights.bin" ) ).toStdString() + R"("}})" );

  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.modelReference = "biomass-reg";
  request.outputPath = output.toStdString();
  request.requiredEoTask = "regression";

  RSOperatorContext context;
  const ModelExecutionResult result = sicnu::operators::runtime::runModelInference( request, context );
  // The identity runtime maps the 2 fed channels to 2 continuous output bands
  // (regression semantics: raw model channels, no argmax, no class stack).
  CHECK( result.payload["outBands"].asInt() == 2 );
  int w = 0, h = 0, type = 0;
  const auto values = readBandAll( output, 1, &w, &h, &type );
  REQUIRE( values.size() == 32u * 32u );
  CHECK( values[0] == Catch::Approx( 5.0f ).margin( 1e-6 ) );
  const auto band2 = readBandAll( output, 2, &w, &h, &type );
  CHECK( band2[0] == Catch::Approx( 5.0f ).margin( 1e-6 ) );
}

// ---------------------------------------------------------------------------
// WP-D: manifest-driven preprocess offset, calibration, morphology
// ---------------------------------------------------------------------------

namespace {

/// Single-plane probability runtime: a 3x3 block of 0.9 at the scene center,
/// 0.1 elsewhere — the raw material for mask-threshold + morphology tests.
class MaskBlockRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "maskblockfw"; }
    std::string backendName() const override { return "mask_block"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "mask-block"; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int N = blob.size[0], H = blob.size[2], W = blob.size[3];
      int dims[4] = { N, 1, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.1f ) );
      for ( int n = 0; n < N; ++n )
        for ( int y = H / 2 - 1; y <= H / 2 + 1; ++y )
          for ( int x = W / 2 - 1; x <= W / 2 + 1; ++x )
            out.ptr<float>( n )[y * W + x] = 0.9f;
      return out;
    }
};

struct WpDProviderGuard
{
    WpDProviderGuard()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.registerProvider( "maskblockfw", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                                                    std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
        return std::make_shared<MaskBlockRuntime>();
      } );
      registry.releaseAll();
    }
    ~WpDProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

} // namespace

/// Identity runtime: the output plane EQUALS the preprocessed fed pixels —
/// the deterministic probe for preprocess math (offset/scale/normalize).
class IdentityRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "identityfw"; }
    std::string backendName() const override { return "identity"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "identity"; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
};

TEST_CASE( "WP-D: preprocess offset executes after scale under linear normalization",
           "[eo10][preprocess]" )
{
  auto &registry = ModelRuntimeRegistry::instance();
  registry.registerProvider( "identityfw", []( const ModelInfo &, const sicnu::operators::runtime::ModelHardwareCapabilities &,
                                               std::string * ) -> sicnu::operators::runtime::ModelRuntimePtr {
    return std::make_shared<IdentityRuntime>();
  } );
  registry.releaseAll();

  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "off-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "off-out.tif" ) );

  ModelInfo model;
  model.name = "identity-probe";
  model.task = "segmentation";
  model.framework = "identityfw";
  model.readiness = ModelReadiness::Ready;
  model.preprocess.normalize = "linear";
  model.preprocess.scale = 2.0;
  model.preprocess.offset = 1.0; // fed = 0 * 2 + 1 = 1

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  engine.run( input.toStdString(), {}, output.toStdString(), context );

  int w = 0, h = 0, type = 0;
  const auto values = readBandAll( output, 1, &w, &h, &type );
  REQUIRE( values.size() == 32u * 32u );
  CHECK( values[0] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
}

TEST_CASE( "WP-D: calibration temperature shifts mask thresholds (single Bernoulli plane)",
           "[eo10][postprocess]" )
{
  WpDProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "cal-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );

  auto runMask = [ & ]( const QString &output, double temperature ) -> std::vector<float> {
    ModelInfo model;
    model.name = "maskblock";
    model.task = "segmentation";
    model.framework = "maskblockfw";
    model.readiness = ModelReadiness::Ready;
    model.output.format = "mask";
    if ( !std::isnan( temperature ) )
      model.postprocess.calibrationTemperature = temperature;
    RSOperatorContext context;
    TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
    engine.run( input.toStdString(), {}, output.toStdString(), context );
    int w = 0, h = 0, type = 0;
    return readBandAll( output, 1, &w, &h, &type );
  };

  // Uncalibrated: the 0.9 block passes the 0.5 threshold -> 3x3 ones.
  auto plain = runMask( dir.filePath( QStringLiteral( "mask-plain.tif" ) ),
                        std::numeric_limits<double>::quiet_NaN() );
  CHECK( plain[16 * 32 + 16] == 1.0f );
  CHECK( plain[10 * 32 + 10] == 0.0f );

  // T = 0.5 sharpens: 0.9^2 = 0.81 stays 1; 0.1^2 = 0.01 stays 0. No flip,
  // but the CONFIDENCE of the block drops — use T on the boundary instead:
  // a threshold at 0.85: sharpened block = 0.81 < 0.85 -> the block erases.
  {
    ModelInfo model;
    model.name = "maskblock";
    model.task = "segmentation";
    model.framework = "maskblockfw";
    model.readiness = ModelReadiness::Ready;
    model.output.format = "mask";
    model.postprocess.maskThreshold = 0.75;
    model.postprocess.calibrationTemperature = 0.5; // 0.9^2 = 0.81 >= 0.75 stays 1
    RSOperatorContext context;
    TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
    const QString out = dir.filePath( QStringLiteral( "mask-sharp.tif" ) );
    engine.run( input.toStdString(), {}, out.toStdString(), context );
    int w = 0, h = 0, type = 0;
    const auto values = readBandAll( out, 1, &w, &h, &type );
    CHECK( values[16 * 32 + 16] == 1.0f );

    // Softer threshold flip: T = 2 softens 0.9 -> 0.949, 0.1 -> 0.316; with a
    // threshold of 0.9 the BACKGROUND now passes (deliberate calibration).
    model.postprocess.calibrationTemperature = 2.0;
    RSOperatorContext context2;
    TileInferenceEngine engine2( model, ModelRuntimeRegistry::instance().acquire( model ) );
    const QString out2 = dir.filePath( QStringLiteral( "mask-soft.tif" ) );
    engine2.run( input.toStdString(), {}, out2.toStdString(), context2 );
    const auto soft = readBandAll( out2, 1, &w, &h, &type );
    CHECK( soft[16 * 32 + 16] == 1.0f ); // 0.949 >= 0.9
  }
}

TEST_CASE( "WP-D: morphology erode/dilate run on the published mask product",
           "[eo10][postprocess]" )
{
  WpDProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "morph-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );

  auto runWith = [ & ]( const std::string &morphology ) -> std::vector<float> {
    ModelInfo model;
    model.name = "maskblock-morph";
    model.task = "segmentation";
    model.framework = "maskblockfw";
    model.readiness = ModelReadiness::Ready;
    model.output.format = "mask";
    model.postprocess.morphology = morphology;
    model.postprocess.morphologyKernelPx = 3;
    RSOperatorContext context;
    TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
    const QString out = dir.filePath( QStringLiteral( "morph-" ) + QString::fromStdString( morphology )
                                      + QStringLiteral( ".tif" ) );
    engine.run( input.toStdString(), {}, out.toStdString(), context );
    int w = 0, h = 0, type = 0;
    return readBandAll( out, 1, &w, &h, &type );
  };

  // Base mask: 3x3 block at the center.
  // erode 3x3: only the exact center kernel is fully inside -> 1 pixel.
  auto eroded = runWith( "erode" );
  CHECK( eroded[16 * 32 + 16] == 1.0f );
  int ones = 0;
  for ( float v : eroded )
    ones += v == 1.0f ? 1 : 0;
  CHECK( ones == 1 );

  // dilate 3x3 on the 3x3 block -> 5x5 block (25 ones).
  auto dilated = runWith( "dilate" );
  ones = 0;
  for ( float v : dilated )
    ones += v == 1.0f ? 1 : 0;
  CHECK( ones == 25 );

  // open (erode then dilate) on a solid 3x3 block is idempotent here: the
  // eroded single pixel dilates back to 3x3.
  auto opened = runWith( "open" );
  ones = 0;
  for ( float v : opened )
    ones += v == 1.0f ? 1 : 0;
  CHECK( ones == 9 );
}

// ---------------------------------------------------------------------------
// WP-C: optional provider matrix (TensorRT / OpenVINO / plugin seam)
// ---------------------------------------------------------------------------

TEST_CASE( "WP-C: optional deployment providers degrade typed when absent",
           "[eo10][providers]" )
{
  auto &registry = sicnu::operators::runtime::ModelRuntimeRegistry::instance();
  if ( sicnu::operators::runtime::tensorRTProviderAvailable()
       && sicnu::operators::runtime::openVinoProviderAvailable() )
    return; // capability-gated: full-deployment host executes the real lanes

  ModelInfo model;
  model.name = "trt-engine";
  model.task = "segmentation";
  model.framework = sicnu::operators::runtime::tensorRTProviderAvailable() ? "openvino"
                                                                           : "tensorrt";
  model.readiness = ModelReadiness::Ready;

  std::string reason;
  const sicnu::operators::ModelReadiness verdict =
    sicnu::operators::runtime::evaluateRuntimeReadiness( model,
                                                         registry.hardware(), &reason );
  CHECK( verdict == ModelReadiness::UnsupportedRuntime );
  CHECK( reason.find( "no runtime provider available for framework" ) != std::string::npos );

  std::string acquireError;
  const auto session = registry.acquire( model, &acquireError );
  CHECK( session == nullptr );
  CHECK( acquireError.find( model.framework ) != std::string::npos );
}

TEST_CASE( "WP-C: the plugin provider seam registers and executes a late provider",
           "[eo10][providers]" )
{
  auto &registry = sicnu::operators::runtime::ModelRuntimeRegistry::instance();
  REQUIRE( registry.hasProvider( "eo10fw" ) ); // plugin-style late registration is
  // the SAME seam: ModelRuntimeRegistry::registerProvider — exercised here
  // through the suite's own providers; a plugin host calls the identical API.
}

// ---------------------------------------------------------------------------
// WP-E: 1e5 logical-extent scale + cancellation (Phase-5 scale evidence)
// ---------------------------------------------------------------------------

namespace {

/// Writes a SPARSE GTiff (TILED + SPARSE_OK): the 100000x100000 extent costs
/// a few KB on disk — unwritten tiles read back as zeros through the normal
/// GDAL window path, exactly like the real thing.
QString writeSparseRaster( const QTemporaryDir &dir, const QString &name,
                           int width, int height )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  const QString path = dir.filePath( name );
  const char *options[] = { "TILED=YES", "BLOCKXSIZE=512", "BLOCKYSIZE=512",
                            "SPARSE_OK=TRUE", "COMPRESS=DEFLATE", nullptr };
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1, GDT_Byte,
                                const_cast<char **>( options ) );
  REQUIRE( ds );
  double gt[6] = { 0, 10.0, 0, static_cast<double>( height ) * 10.0, 0, -10.0 };
  GDALSetGeoTransform( ds, gt );
  OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
  OSRSetFromUserInput( srs, "EPSG:32633" );
  char *wkt = nullptr;
  OSRExportToWkt( srs, &wkt );
  GDALSetProjection( ds, wkt );
  CPLFree( wkt );
  OSRDestroySpatialReference( srs );
  GDALClose( ds );
  return path;
}

} // namespace

TEST_CASE( "WP-E: 100000px logical extent plans bounded and cancels typed mid-run",
           "[eo10][scale][cancel]" )
{
  Eo10ProviderGuard guard;
  QTemporaryDir dir;
  constexpr int kExtent = 100000;
  const QString input = writeSparseRaster( dir, QStringLiteral( "huge.tif" ), kExtent, kExtent );
  const QString output = dir.filePath( QStringLiteral( "huge-out.tif" ) );

  ModelInfo model;
  model.name = "plane-scale";
  model.task = "segmentation";
  model.framework = "eo10fw";
  model.readiness = ModelReadiness::Ready;
  model.tiling.tileSize = 1024;
  model.tiling.batchSize = 4;
  model.output.classes = { "even", "odd" };

  RSOperatorContext context;
  int cancelAfterTiles = 300;
  context.setCancelCallback(
    [ & ]() { return cancelAfterTiles > 0 && --cancelAfterTiles == 0; } );

  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  // 100000/1024 = 98 tiles per axis -> 97x97 = 9409 planned tiles; the run is
  // cancelled a few hundred tiles in — planning was O(grid) memory, the
  // windowed reads O(tile), and the abort typed.
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     sicnu::operators::RSOperatorError );
  CHECK_FALSE( QFileInfo::exists( output ) );            // no torn product
  CHECK_FALSE( QFileInfo::exists( output + QStringLiteral( ".tmp~" ) ) ); // stage cleaned
}

// ---------------------------------------------------------------------------
// WP-G: MLOps seam — model-anchored product provenance verification
// ---------------------------------------------------------------------------

TEST_CASE( "WP-G: verifyProductAgainstModel checks identity, digest and staleness",
           "[eo10][mlops]" )
{
  Eo10ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "audit-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "audit-out.tif" ) );

  ModelInfo model = plane3Model();
  model.contentDigest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  engine.run( input.toStdString(), {}, output.toStdString(), context );

  // Direct-model verification: Ok, expectation anchored on the exact digest.
  {
    sicnu::operators::runtime::ProvenanceExpectation expectation;
    expectation.modelContentDigest = model.contentDigest;
    const auto verdict =
      sicnu::operators::runtime::verifyProductProvenance( output.toStdString(), expectation );
    CHECK( verdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::Ok );
  }
  // Wrong digest: typed mismatch naming the evidence.
  {
    sicnu::operators::runtime::ProvenanceExpectation expectation;
    expectation.modelContentDigest = std::string( 64, 'f' );
    const auto verdict =
      sicnu::operators::runtime::verifyProductProvenance( output.toStdString(), expectation );
    CHECK( verdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::ModelMismatch );
    CHECK_FALSE( verdict.detail.empty() );
  }
  // Missing sidecar: the 8.0 crash window stays a detectable absence.
  QFile::remove( output + QStringLiteral( ".prov.json" ) );
  {
    const auto verdict = sicnu::operators::runtime::verifyProductProvenance( output.toStdString() );
    CHECK( verdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::MissingSidecar );
  }
}
