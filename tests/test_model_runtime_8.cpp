// tests/test_model_runtime_8.cpp — Model Runtime 8.0: CRS-aware grid
// authority + alignment provenance (WP-C), temporal sequence lane with
// dynamic T / timestamps / quality masks (WP-D), placement policy +
// pressure observability (WP-B) and the Labels class remap (WP-E).
// Runs in EVERY build (fake providers + synthetic rasters only) — the real
// ORT lane has its own capability-gated target (test_onnxruntime_provider).
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

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelInputContract;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Multi-input fake that records the FED tensor shapes and produces a
/// rank-4 (B,1,H,W) output carrying the last channel of the last feed
/// (the known-answer convention of the 7.0 multimodal tests).
class RecordingMultiRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m8-fake"; }
    std::string backendName() const override { return "m8"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m8"; }

    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
    bool supportsMultiInput() const override { return true; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      fedRanks.clear();
      fedShapes.clear();
      for ( const auto &nt : inputs )
      {
        fedRanks.push_back( nt.second.rank() );
        fedShapes.push_back( nt.second.shape );
      }
      const TensorBlob &last = inputs.back().second;
      const std::int64_t B = last.shape[0];
      const std::int64_t H = last.rank() >= 4 ? last.shape[last.rank() - 2] : 1;
      const std::int64_t W = last.rank() >= 4 ? last.shape[last.rank() - 1] : 1;
      TensorBlob out;
      out.shape = { B, 1, H, W };
      out.dtype = TensorDType::Float32;
      out.bytes.assign( static_cast<std::size_t>( B * H * W ) * sizeof( float ), 0 );
      float *outData = reinterpret_cast<float *>( out.bytes.data() );
      // Known answer: the value of the LAST channel of the LAST feed.
      const std::int64_t C = last.shape[1];
      const float *inData = last.dataFloat32();
      for ( std::int64_t b = 0; b < B; ++b )
        for ( std::int64_t y = 0; y < H; ++y )
          for ( std::int64_t x = 0; x < W; ++x )
            outData[( b * H + y ) * W + x] =
              inData[( ( b * C + ( C - 1 ) ) * H + y ) * W + x];
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    std::vector<int> fedRanks;
    std::vector<std::vector<std::int64_t>> fedShapes;
};

/// Three-class fake for the Labels remap: model class = (x + y) % 3 planes.
class ThreeClassRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m8-fake"; }
    std::string backendName() const override { return "m8classes"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m8c"; }

    // The single-input engine drives the cv::Mat fast path: the OUTPUT
    // carries 3 class planes regardless of the fed channel count, with the
    // one-hot model class = (x + y) % 3.
    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int B = blob.size[0];
      const int H = blob.size[2];
      const int W = blob.size[3];
      const int dims[4] = { B, 3, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.0f ) );
      for ( int b = 0; b < B; ++b )
        for ( int y = 0; y < H; ++y )
          for ( int x = 0; x < W; ++x )
            out.ptr<float>( b, static_cast<int>( ( x + y ) % 3 ) )[y * W + x] = 1.0f;
      return out;
    }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      const cv::Mat out = infer( inputs.front().second.toMat() );
      return { NamedTensor{ std::string(), TensorBlob::fromMat( out ) } };
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

ModelInfo twoInputModel( int tile = 16 )
{
  ModelInfo model;
  model.name = "m8-dual";
  model.framework = "m8-fake";
  model.task = "change_detection";
  model.tiling.tileSize = tile;
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

/// RAII registry cleanup for programmatically registered catalog entries.
struct CatalogEntry
{
    explicit CatalogEntry( std::string id ) : id( std::move( id ) ) {}
    ~CatalogEntry() { ModelCatalog::instance().unregister( id ); }
    std::string id;
};

} // namespace

// ---------------------------------------------------------------------------
// WP-C: CRS authority
// ---------------------------------------------------------------------------

TEST_CASE( "crsMismatch compares CRS semantically, not syntactically", "[models][crs]" )
{
  // Undeclared on both sides: historical behavior (geometry-only).
  CHECK( TileInferenceEngine::crsMismatch( "a", "", "b", "", false ).empty() );
  CHECK( TileInferenceEngine::crsMismatch( "a", "", "b", "", true ).empty() );
  // Same CRS twice (identical authority strings).
  CHECK( TileInferenceEngine::crsMismatch( "a", "EPSG:4326", "b", "EPSG:4326", true ).empty() );
  // Different declared CRS → refusal, strict or not (geotransform numbers
  // under different CRS describe different ground).
  CHECK_THAT( TileInferenceEngine::crsMismatch( "a.tif", "EPSG:4326", "b.tif", "EPSG:32633", false ),
              Catch::Matchers::ContainsSubstring( "coordinate reference systems" ) );
  CHECK_THAT( TileInferenceEngine::crsMismatch( "a.tif", "EPSG:4326", "b.tif", "EPSG:32633", true ),
              Catch::Matchers::ContainsSubstring( "coordinate reference systems" ) );
  // Exactly one declared: non-strict passes, alignment=reference refuses.
  CHECK( TileInferenceEngine::crsMismatch( "a.tif", "EPSG:4326", "b.tif", "", false ).empty() );
  CHECK_THAT( TileInferenceEngine::crsMismatch( "a.tif", "EPSG:4326", "b.tif", "", true ),
              Catch::Matchers::ContainsSubstring( "alignment=reference" ) );
  // Equivalent WKT forms of one CRS must compare SAME.
  const char *wkt4326 =
    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563,"
    "AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],"
    "PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],"
    "UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],"
    "AUTHORITY[\"EPSG\",\"4326\"]]";
  CHECK( TileInferenceEngine::crsMismatch( "a", wkt4326, "b", "EPSG:4326", true ).empty() );
}

TEST_CASE( "same geotransform under different CRS is a co-registration refusal",
           "[models][crs][multimodal]" )
{
  QTemporaryDir dir;
  auto primary = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withCrs( QStringLiteral( "EPSG:4326" ) )
                   .withGeoTransform( 10.0, 1.0, 50.0, -1.0 )
                   .withConstantValue( 1, 1.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "primary.tif" ) ) );
  auto other = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                 .withCrs( QStringLiteral( "EPSG:32633" ) ) // SAME numbers, different CRS
                 .withGeoTransform( 10.0, 1.0, 50.0, -1.0 )
                 .withConstantValue( 1, 2.0f )
                 .writeToDisk( dir.filePath( QStringLiteral( "other.tif" ) ) );
  REQUIRE_FALSE( primary.isEmpty() );
  REQUIRE_FALSE( other.isEmpty() );

  auto runtime = std::make_shared<RecordingMultiRuntime>();
  TileInferenceEngine engine( twoInputModel(), runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  REQUIRE_THROWS_WITH( engine.runMultiInput(
                         { NamedRasterFeed{ "before", { primary.toStdString() }, {} },
                           NamedRasterFeed{ "after", { other.toStdString() }, {} } },
                         out.toStdString(), context, {} ),
                       Catch::Matchers::ContainsSubstring( "coordinate reference systems" ) );
  // No product, no sidecar on refusal.
  CHECK_FALSE( QFile::exists( out ) );
  CHECK_FALSE( QFile::exists( out + QStringLiteral( ".prov.json" ) ) );
}

TEST_CASE( "matching CRS feeds run and carry grid provenance with prepared_from",
           "[models][crs][provenance]" )
{
  QTemporaryDir dir;
  auto primary = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withCrs( QStringLiteral( "EPSG:4326" ) )
                   .withGeoTransform( 10.0, 1.0, 50.0, -1.0 )
                   .withConstantValue( 1, 5.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "primary.tif" ) ) );
  auto other = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                 .withCrs( QStringLiteral( "EPSG:4326" ) )
                 .withGeoTransform( 10.0, 1.0, 50.0, -1.0 )
                 .withConstantValue( 1, 3.0f )
                 .writeToDisk( dir.filePath( QStringLiteral( "other.tif" ) ) );
  REQUIRE_FALSE( primary.isEmpty() );
  REQUIRE_FALSE( other.isEmpty() );

  auto runtime = std::make_shared<RecordingMultiRuntime>();
  TileInferenceEngine engine( twoInputModel(), runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  NamedRasterFeed before{ "before", { primary.toStdString() }, {}, { "raw-before.tif" } };
  NamedRasterFeed after{ "after", { other.toStdString() }, {}, { "raw-after.tif" } };
  const TileInferenceStats stats = engine.runMultiInput( { before, after }, out.toStdString(),
                                                         context, {} );

  // Product + provenance sidecar both exist.
  REQUIRE( QFile::exists( out ) );
  REQUIRE( QFile::exists( out + QStringLiteral( ".prov.json" ) ) );

  REQUIRE( stats.inputGrids.size() == 2 );
  CHECK( stats.inputGrids[0].name == "before" );
  CHECK( stats.inputGrids[0].preparedFrom == "raw-before.tif" );
  CHECK( stats.inputGrids[0].crsVerified );
  CHECK( stats.inputGrids[0].width == 32 );
  CHECK( stats.inputGrids[1].crsVerified );
  CHECK( stats.inputGrids[1].crs.find( "4326" ) != std::string::npos );

  // The sidecar records the same story (schema + inputs + model identity).
  QFile sidecar( out + QStringLiteral( ".prov.json" ) );
  REQUIRE( sidecar.open( QIODevice::ReadOnly ) );
  const QByteArray asText = sidecar.readAll();
  CHECK( asText.contains( "\"exp-rs-prov/1\"" ) );
  CHECK( asText.contains( "raw-after.tif" ) );
  CHECK( asText.contains( "m8-dual" ) );
}

// ---------------------------------------------------------------------------
// WP-D: temporal sequence lane
// ---------------------------------------------------------------------------

TEST_CASE( "temporal sequence manifests parse and refuse broken shapes",
           "[models][temporal8][manifest]" )
{
  const std::string manifestTemplate = R"({
    "name": "m8-seq",
    "task": "segmentation",
    "framework": "onnx",
    "artifact": { "path": "missing.onnx" },
    "inputs": [ %1 ]
  })";
  const std::string good =
    R"({"name":"t","temporal_collapse":"sequence","layout":"NCTHW","temporal_length":2})";
  auto &catalog = ModelCatalog::instance();
  {
    std::string error;
    REQUIRE( catalog.registerManifestJson(
      QString( manifestTemplate.c_str() ).arg( good.c_str() ).toStdString(), "m8-test",
      &error ) );
    CatalogEntry guard( "m8-seq" );
    const auto model = catalog.find( "m8-seq" );
    REQUIRE( model.has_value() );
    REQUIRE( model->inputs.size() == 1 );
    CHECK( model->inputs[0].temporalCollapse == "sequence" );
    CHECK( model->inputs[0].temporalLength == 2 );
    CHECK_FALSE( model->inputs[0].temporalDynamic );
  }
  // sequence + no length + no dynamic → invalid
  {
    const std::string bad =
      R"({"name":"t","temporal_collapse":"sequence","layout":"NCTHW"})";
    const auto issues = catalog.validateManifestJson(
      QString( manifestTemplate.c_str() ).arg( bad.c_str() ).toStdString() );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "temporal_dynamic" ) != std::string::npos );
  }
  // sequence + wrong layout → invalid
  {
    const std::string bad =
      R"({"name":"t","temporal_collapse":"sequence","layout":"NCHW","temporal_length":2})";
    const auto issues = catalog.validateManifestJson(
      QString( manifestTemplate.c_str() ).arg( bad.c_str() ).toStdString() );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "NCTHW" ) != std::string::npos );
  }
  // dynamic without sequence → invalid
  {
    const std::string bad = R"({"name":"t","temporal_dynamic":true,"layout":"NCHW"})";
    const auto issues = catalog.validateManifestJson(
      QString( manifestTemplate.c_str() ).arg( bad.c_str() ).toStdString() );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "sequence" ) != std::string::npos );
  }
  // Fully dynamic sequence is legal.
  {
    const std::string dynamicGood =
      R"({"name":"t","temporal_collapse":"sequence","layout":"NCTHW","temporal_dynamic":true})";
    std::string error;
    REQUIRE( catalog.registerManifestJson(
      QString( manifestTemplate.c_str() ).arg( dynamicGood.c_str() ).toStdString(), "m8-test",
      &error ) );
    CatalogEntry guard( "m8-seq" );
    const auto model = catalog.find( "m8-seq" );
    REQUIRE( model.has_value() );
    CHECK( model->inputs[0].temporalDynamic );
    CHECK( model->inputs[0].temporalLength == 0 );
  }
}

TEST_CASE( "sequence feeds carry an explicit time axis with known answers",
           "[models][temporal8]" )
{
  QTemporaryDir dir;
  auto t0 = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
              .withConstantValue( 1, 1.0f )
              .withConstantValue( 2, 2.0f )
              .writeToDisk( dir.filePath( QStringLiteral( "t0.tif" ) ) );
  auto t1 = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
              .withConstantValue( 1, 10.0f )
              .withConstantValue( 2, 20.0f )
              .writeToDisk( dir.filePath( QStringLiteral( "t1.tif" ) ) );
  REQUIRE_FALSE( t0.isEmpty() );
  REQUIRE_FALSE( t1.isEmpty() );

  ModelInfo model = twoInputModel();
  model.inputs[0].temporalCollapse = "sequence";
  model.inputs[0].layout = "NCTHW";
  model.inputs[0].temporalLength = 2;
  model.inputs[1].temporalCollapse = "sequence";
  model.inputs[1].layout = "NCTHW";
  model.inputs[1].temporalLength = 2;

  auto runtime = std::make_shared<RecordingMultiRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  const TileInferenceStats stats = engine.runMultiInput(
    { NamedRasterFeed{ "before",
                       { t0.toStdString(), t1.toStdString() },
                       { 1 },
                       {},
                       { "2024-01-01T00:00:00Z", "2024-02-01T00:00:00Z" } },
      NamedRasterFeed{ "after", { t0.toStdString(), t1.toStdString() }, { 1 } } },
    out.toStdString(), context, {} );

  CHECK( stats.tilesProcessed == 4 );
  REQUIRE( runtime->fedRanks.size() == 2 );
  CHECK( runtime->fedRanks[0] == 5 ); // (B, T, C, H, W) — the explicit time axis
  CHECK( runtime->fedShapes[0] == std::vector<std::int64_t>( { 1, 2, 1, 16, 16 } ) );
  CHECK( runtime->fedRanks[1] == 5 );
}

TEST_CASE( "dynamic T is defined by the feed length", "[models][temporal8]" )
{
  QTemporaryDir dir;
  std::vector<QString> frames;
  for ( int i = 0; i < 3; ++i )
  {
    auto frame = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withConstantValue( 1, static_cast<float>( ( i + 1 ) * 10 ) )
                   .writeToDisk( dir.filePath( QStringLiteral( "f%1.tif" ).arg( i ) ) );
    REQUIRE_FALSE( frame.isEmpty() );
    frames.push_back( frame );
  }

  ModelInfo model = twoInputModel();
  for ( auto &in : model.inputs )
  {
    in.temporalCollapse = "sequence";
    in.layout = "NCTHW";
    in.temporalDynamic = true;
  }

  auto runtime = std::make_shared<RecordingMultiRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  const TileInferenceStats stats = engine.runMultiInput(
    { NamedRasterFeed{ "before",
                       { frames[0].toStdString(), frames[1].toStdString(),
                         frames[2].toStdString() },
                       { 1 } },
      NamedRasterFeed{ "after", { frames[0].toStdString(), frames[1].toStdString() }, { 1 } } },
    out.toStdString(), context, {} );
  ( void )stats;

  REQUIRE( runtime->fedRanks.size() == 2 );
  // Each dynamic feed carries its OWN time axis: T follows the feed length.
  CHECK( runtime->fedShapes[0] == std::vector<std::int64_t>( { 1, 3, 1, 16, 16 } ) );
  CHECK( runtime->fedShapes[1] == std::vector<std::int64_t>( { 1, 2, 1, 16, 16 } ) );
}

TEST_CASE( "feed timestamps must parse and be strictly increasing", "[models][temporal8]" )
{
  QTemporaryDir dir;
  auto t0 = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
              .withConstantValue( 1, 1.0f )
              .writeToDisk( dir.filePath( QStringLiteral( "t0.tif" ) ) );
  auto t1 = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
              .withConstantValue( 1, 2.0f )
              .writeToDisk( dir.filePath( QStringLiteral( "t1.tif" ) ) );
  REQUIRE_FALSE( t0.isEmpty() );
  REQUIRE_FALSE( t1.isEmpty() );

  ModelInfo model = twoInputModel( 16 );
  model.inputs[0].temporalLength = 2;
  auto runtime = std::make_shared<RecordingMultiRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );

  // Out of order → refusal naming the order violation.
  REQUIRE_THROWS_WITH(
    engine.runMultiInput(
      { NamedRasterFeed{ "before",
                         { t0.toStdString(), t1.toStdString() },
                         {},
                         {},
                         { "2024-03-01T00:00:00Z", "2024-01-01T00:00:00Z" } },
        NamedRasterFeed{ "after", { t0.toStdString() } } },
      out.toStdString(), context, {} ),
    Catch::Matchers::ContainsSubstring( "not strictly increasing" ) );

  // Unparsable instant → typed refusal.
  REQUIRE_THROWS_WITH(
    engine.runMultiInput(
      { NamedRasterFeed{ "before",
                         { t0.toStdString(), t1.toStdString() },
                         {},
                         {},
                         { "not-a-time", "2024-01-01T00:00:00Z" } },
        NamedRasterFeed{ "after", { t0.toStdString() } } },
      out.toStdString(), context, {} ),
    Catch::Matchers::ContainsSubstring( "ISO 8601" ) );

  // Wrong arity → parameter refusal.
  REQUIRE_THROWS_WITH(
    engine.runMultiInput(
      { NamedRasterFeed{ "before",
                         { t0.toStdString(), t1.toStdString() },
                         {},
                         {},
                         { "2024-01-01T00:00:00Z" } },
        NamedRasterFeed{ "after", { t0.toStdString() } } },
      out.toStdString(), context, {} ),
    Catch::Matchers::ContainsSubstring( "timestamps must parallel" ) );
}

TEST_CASE( "quality masks invalidate pixels and shrink the coverage gate",
           "[models][temporal8][quality]" )
{
  QTemporaryDir dir;
  auto primary = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withCrs( QStringLiteral( "EPSG:4326" ) )
                   .withConstantValue( 1, 7.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "primary.tif" ) ) );
  REQUIRE_FALSE( primary.isEmpty() );
  // A single-frame feed with a mask that zeroes the LEFT half.
  auto other = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                 .withCrs( QStringLiteral( "EPSG:4326" ) )
                 .withConstantValue( 1, 9.0f )
                 .writeToDisk( dir.filePath( QStringLiteral( "other.tif" ) ) );
  // BOTH feeds wear the same quality mask: a pixel is valid when ANY feed
  // sees data, so masking one feed alone would never move the gate.
  auto mask = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Byte )
                .withCrs( QStringLiteral( "EPSG:4326" ) )
                .withConstantValue( 1, 1.0f )
                .writeToDisk( dir.filePath( QStringLiteral( "mask.tif" ) ) );
  REQUIRE_FALSE( other.isEmpty() );
  REQUIRE_FALSE( mask.isEmpty() );
  {
    // Overwrite the left half of the mask with 0 (invalid).
    GDALDataset *ds = static_cast<GDALDataset *>(
      GDALOpen( mask.toUtf8().constData(), GA_Update ) );
    REQUIRE( ds );
    std::vector<std::uint8_t> zeros( 16, 0 );
    for ( int row = 0; row < 32; ++row )
      REQUIRE( ds->GetRasterBand( 1 )
                 ->RasterIO( GF_Write, 0, row, 16, 1, zeros.data(), 16, 1, GDT_Byte, 0,
                             0 ) == CE_None );
    GDALClose( ds );
  }

  ModelInfo model = twoInputModel();
  model.tiling.tileSize = 32; // single tile so the gate verdict is global
  model.tiling.minValidCoverage = 0.75; // 50% masked → tile falls below the gate

  auto runtime = std::make_shared<RecordingMultiRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  const TileInferenceStats stats = engine.runMultiInput(
    { NamedRasterFeed{ "before", { primary.toStdString() }, {}, {}, {}, { mask.toStdString() } },
      NamedRasterFeed{ "after", { other.toStdString() }, {}, {}, {}, { mask.toStdString() } } },
    out.toStdString(), context, {} );

  // The masked tile drops below the coverage gate and is written as NoData.
  // (The engine may run ONE all-nodata probe forward to learn the output
  // band count — the 7.0 deferred-NoData mechanism — so only the skip
  // verdict and the NaN output are asserted here.)
  CHECK( stats.tilesSkippedNoData == 1 );
  CHECK( stats.tilesProcessed == 1 ); // the deferred tile counts once written
  int width = 0;
  int height = 0;
  const std::vector<float> band = readBand( out, 1, width, height );
  REQUIRE( width == 32 );
  for ( float v : band )
    CHECK( std::isnan( v ) );

  // A mask that does not sit on the primary grid is a typed refusal.
  auto smallMask = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Byte )
                     .withConstantValue( 1, 1.0f )
                     .writeToDisk( dir.filePath( QStringLiteral( "small_mask.tif" ) ) );
  REQUIRE_FALSE( smallMask.isEmpty() );
  TileInferenceEngine engine2( twoInputModel(), runtime );
  REQUIRE_THROWS_WITH(
    engine2.runMultiInput(
      { NamedRasterFeed{ "before", { primary.toStdString() } },
        NamedRasterFeed{ "after", { other.toStdString() }, {}, {}, {}, { smallMask.toStdString() } } },
      dir.filePath( QStringLiteral( "out2.tif" ) ).toStdString(), context, {} ),
    Catch::Matchers::ContainsSubstring( "align the mask" ) );

  // A mask under a DIFFERENT CRS is refused even with identical geometry.
  auto otherCrsMask =
    sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Byte )
      .withCrs( QStringLiteral( "EPSG:32633" ) )
      .withGeoTransform( 10.0, 1.0, 50.0, -1.0 )
      .withConstantValue( 1, 1.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "crs_mask.tif" ) ) );
  REQUIRE_FALSE( otherCrsMask.isEmpty() );
  REQUIRE_THROWS_WITH(
    engine2.runMultiInput(
      { NamedRasterFeed{ "before", { primary.toStdString() } },
        NamedRasterFeed{ "after",
                         { other.toStdString() },
                         {},
                         {},
                         {},
                         { otherCrsMask.toStdString() } } },
      dir.filePath( QStringLiteral( "out3.tif" ) ).toStdString(), context, {} ),
    Catch::Matchers::ContainsSubstring( "coordinate reference systems" ) );
}

// ---------------------------------------------------------------------------
// WP-B: placement policy + pressure report
// ---------------------------------------------------------------------------

TEST_CASE( "LeastLoaded picks the fitting device with the most free VRAM",
           "[models][planner8]" )
{
  const ModelHardwareCapabilities hw = [] {
    ModelHardwareCapabilities caps;
    caps.cudaAvailable = true;
    caps.vramBudgetMb = 0;
    caps.cudaDeviceCount = 2;
    return caps;
  }();
  // Device 0: 1 GiB free. Device 1: 7 GiB free. A 512 MiB model FITS both
  // cards: LowestFitting picks 0, LeastLoaded picks 1 (more headroom).
  const std::vector<int> freeByIndex = { 1024, 7168 };

  ResolvedDevice out;
  std::string why;
  RequestedDevice autoReq = RequestedDevice::autoDetect();
  REQUIRE( resolveDevice( autoReq, hw, /*modelWantsGpu*/ true, /*estimateMb*/ 512,
                          /*maxAddressable*/ 1, /*allowCpuFallback*/ false, freeByIndex,
                          DevicePlacementPolicy::LowestFitting, &out, &why ) );
  CHECK( out.cudaIndex == 0 );
  REQUIRE( resolveDevice( autoReq, hw, true, 512, 1, false, freeByIndex,
                          DevicePlacementPolicy::LeastLoaded, &out, &why ) );
  CHECK( out.cudaIndex == 1 );
  ( void )why;

  // Only ONE fitting device (device 1's free VRAM is below the estimate) →
  // both policies agree on the single candidate.
  const std::vector<int> oneFit = { 1024, 512 };
  REQUIRE( resolveDevice( autoReq, hw, true, 1024, 1, false, oneFit,
                          DevicePlacementPolicy::LeastLoaded, &out, &why ) );
  CHECK( out.cudaIndex == 0 );
  // Unknown free on the only fitting slot still resolves deterministically.
  const std::vector<int> unknown = { -1 };
  REQUIRE( resolveDevice( autoReq, hw, true, 512, 0, false, unknown,
                          DevicePlacementPolicy::LeastLoaded, &out, &why ) );
  CHECK( out.cudaIndex == 0 );

  // Nothing fits (both devices below the estimate) → typed refusal.
  const std::vector<int> noneFit = { 1024, 2048 };
  std::string refusal;
  CHECK_FALSE( resolveDevice( autoReq, hw, true, 4096, 1, false, noneFit,
                              DevicePlacementPolicy::LeastLoaded, &out, &refusal ) );
  CHECK_THAT( refusal, Catch::Matchers::ContainsSubstring( "cpu_fallback" ) );

  // Registry policy plumbing + device report.
  auto &registry = ModelRuntimeRegistry::instance();
  registry.setPlacementPolicy( DevicePlacementPolicy::LeastLoaded );
  CHECK( registry.placementPolicy() == DevicePlacementPolicy::LeastLoaded );
  registry.setPlacementPolicy( DevicePlacementPolicy::LowestFitting );
  CHECK( registry.placementPolicy() == DevicePlacementPolicy::LowestFitting );
  registry.vramLedger().reset();
  registry.vramLedger().setCapacity( 0, 4096 );
  REQUIRE( registry.vramLedger().tryReserve( 0, 1024, "m8-holder" ) );
  const auto report = registry.deviceReport();
  bool found = false;
  for ( const auto &state : report )
    if ( state.index == 0 )
    {
      found = true;
      CHECK( state.capacityMb == 4096 );
      CHECK( state.reservedMb == 1024 );
      CHECK( state.holders == 1 );
    }
  CHECK( found );
  registry.vramLedger().release( 0, 1024, "m8-holder" );
}

// ---------------------------------------------------------------------------
// WP-E: Labels class remap
// ---------------------------------------------------------------------------

TEST_CASE( "class_mapping manifests validate injectivity", "[models][mapping][manifest]" )
{
  auto &catalog = ModelCatalog::instance();
  const std::string manifestTemplate = R"({
    "name": "m8-mapped",
    "task": "segmentation",
    "framework": "onnx",
    "artifact": { "path": "missing.onnx" },
    "output": { "format": "labels", "classes": ["a", "b", "c"] },
    "postprocess": { "class_mapping": %1 }
  })";
  {
    std::string error;
    REQUIRE( catalog.registerManifestJson(
      QString( manifestTemplate.c_str() ).arg( "[0, 2, 1]" ).toStdString(), "m8-test", &error ) );
    CatalogEntry guard( "m8-mapped" );
    const auto model = catalog.find( "m8-mapped" );
    REQUIRE( model.has_value() );
    const std::vector<int> expected = { 0, 2, 1 };
    CHECK( model->postprocess.classMapping == expected );
    // Round-trips through toJson.
    const Json::Value json = model->toJson();
    CHECK( json["postprocess"]["class_mapping"].size() == 3 );
  }
  {
    // Collision → refusal.
    const auto issues = catalog.validateManifestJson(
      QString( manifestTemplate.c_str() ).arg( "[1, 1, 0]" ).toStdString() );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "colliding remap" ) != std::string::npos );
  }
  {
    // Negative → refusal.
    const auto issues = catalog.validateManifestJson(
      QString( manifestTemplate.c_str() ).arg( "[0, -1]" ).toStdString() );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( ">= 0" ) != std::string::npos );
  }
}

TEST_CASE( "Labels products carry the remapped product classes", "[models][mapping]" )
{
  QTemporaryDir dir;
  auto input = sicnu::testing::RsSyntheticRasterBuilder( 24, 24, 1, GDT_Float32 )
                 .withConstantValue( 1, 1.0f )
                 .writeToDisk( dir.filePath( QStringLiteral( "in.tif" ) ) );
  REQUIRE_FALSE( input.isEmpty() );

  ModelInfo model;
  model.name = "m8-classes";
  model.framework = "m8-fake";
  model.tiling.tileSize = 24;
  model.output.format = "labels";
  model.output.tensorNames = { "classes" };
  model.output.classes = { "c0", "c1", "c2" };
  model.postprocess.classMapping = { 2, 0, 1 }; // model 0→2, 1→0, 2→1

  auto runtime = std::make_shared<ThreeClassRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "labels.tif" ) );
  TileInferenceRunOptions options;
  options.outputMode = RasterOutputMode::Labels;
  const TileInferenceStats stats = engine.run( input.toStdString(), {}, out.toStdString(),
                                               context, options );
  CHECK( stats.outBands == 1 );

  int width = 0;
  int height = 0;
  const std::vector<float> band = readBand( out, 1, width, height );
  REQUIRE( width == 24 );
  // The fake assigns model class (x+y)%3; the remap sends 0→2, 1→0, 2→1,
  // so the product value must be (x+y+2)%3.
  for ( int y = 0; y < 24; ++y )
    for ( int x = 0; x < 24; ++x )
      CHECK( band[static_cast<std::size_t>( y ) * 24 + x]
             == Catch::Approx( static_cast<float>( ( x + y + 2 ) % 3 ) ).margin( 1e-4 ) );

  // A mapping that does not cover the head's classes refuses loudly.
  ModelInfo broken = model;
  broken.postprocess.classMapping = { 0, 1 }; // head produces 3 planes
  TileInferenceEngine brokenEngine( broken, runtime );
  REQUIRE_THROWS_WITH( brokenEngine.run( input.toStdString(), {},
                                         dir.filePath( QStringLiteral( "broken.tif" ) )
                                           .toStdString(),
                                         context, options ),
                       Catch::Matchers::ContainsSubstring( "class_mapping" ) );
}

// ---------------------------------------------------------------------------
// WP-D: the service refuses temporal models run without feeds
// ---------------------------------------------------------------------------

TEST_CASE( "a dynamic-T model refuses the single-input service path",
           "[models][temporal8][service]" )
{
  QTemporaryDir dir;
  // Register a ready model with a REAL (opencv-loadable) artifact so the
  // service reaches the contract gate.
  const QString modelsDir = dir.filePath( QStringLiteral( "models" ) );
  QDir( modelsDir ).mkpath( QStringLiteral( "m8-dyn" ) );
  QFile::copy(
    QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" ),
    modelsDir + QStringLiteral( "/m8-dyn/model.onnx" ) );
  QFile manifest( modelsDir + QStringLiteral( "/m8-dyn/model.json" ) );
  REQUIRE( manifest.open( QIODevice::WriteOnly ) );
  manifest.write( R"({
    "name": "m8-dyn",
    "task": "segmentation",
    "framework": "onnx",
    "artifact": { "path": "model.onnx" },
    "inputs": [ { "name": "t", "temporal_collapse": "sequence", "layout": "NCTHW",
                  "temporal_dynamic": true } ]
  })" );
  manifest.close();
  ModelCatalog::instance().setDirectory( modelsDir.toStdString() );
  CatalogEntry guard( "m8-dyn" );

  const auto tinyInput = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
                           .withConstantValue( 1, 1.0f )
                           .writeToDisk( dir.filePath( QStringLiteral( "in.tif" ) ) );
  REQUIRE_FALSE( tinyInput.isEmpty() );
  ModelExecutionRequest request;
  request.inputPath = tinyInput.toStdString(); // must EXIST so the temporal gate fires
  request.modelReference = "m8-dyn";
  request.outputPath = dir.filePath( QStringLiteral( "out.tif" ) ).toStdString();
  RSOperatorContext context;
  REQUIRE_THROWS_WITH( runModelInference( request, context ),
                       Catch::Matchers::ContainsSubstring( "temporal input" ) );
  ModelCatalog::instance().setDirectory( "/nonexistent-mr8-restore" );
}
