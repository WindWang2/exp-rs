// tests/test_model_runtime_9.cpp — Model Runtime 9.0 regression suite.
//
// M0: contract & runtime truth — the operator schema is the PUBLISHED
// contract; every parameter run() parses must be declared in schema
// properties (#872 class), device tokens parse strictly, and the failure
// taxonomy projects every kind onto a real error code.
// M3: multimodal feed graph — per-feed preprocessing overrides (known
// answer), feed identity fingerprints (structure + bounded content digest)
// and the manifest-level closed vocabulary for the new contract.
// Later milestones append their cases to this suite.
// Runs in EVERY OpenCV-enabled build (no provider dependency).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/rs/rs_inference_operator.h"
#include "operators/rs/rs_model_task_operators.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provenance_verify.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <json/json.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators;
using sicnu::operators::rs::RsDetectOperator;
using sicnu::operators::rs::RsEmbeddingOperator;
using sicnu::operators::rs::RsInferenceOperator;
using sicnu::operators::rs::RsSegmentOperator;
using namespace sicnu::operators::runtime;

/// The parameters `RsInferenceOperator::run()` actually reads from the
/// params object (verified against the implementation; extend ONLY when
/// run() learns a new parameter AND schema declares it).
const std::set<std::string> kInferParsedParams = {
  "input",        "model",     "output",  "device",      "bands",
  "tta",          "batchCap",  "named_inputs",  "blend",
};

/// Per-operator parsed parameter surfaces (truth = each run() implementation;
/// the common helper parses input/model/output/device/bands/tta/batchCap).
const std::set<std::string> kCommonTaskParams = {
  "input", "model", "output", "device", "bands", "tta", "batchCap",
};

std::set<std::string> schemaPropertyNames( const Json::Value &schema )
{
  std::set<std::string> names;
  const Json::Value props = schema["properties"];
  for ( auto it = props.begin(); it != props.end(); ++it )
    names.insert( it.key().asString() );
  return names;
}

} // namespace

TEST_CASE( "M0: rs:infer schema declares every parsed parameter (#872)",
           "[model9][m0][schema]" )
{
  RsInferenceOperator op;
  const Json::Value schema = op.schema();
  const std::set<std::string> declared = schemaPropertyNames( schema );

  for ( const std::string &param : kInferParsedParams )
  {
    INFO( "parameter '" << param << "' is parsed by run() but missing from schema()" );
    CHECK( declared.count( param ) == 1 );
  }
  // The `device` declaration must be a string parameter (strict validators
  // reject undeclared-or-mistyped keys; the drift was the missing key).
  CHECK( schema["properties"]["device"]["type"].asString() == "string" );
}

TEST_CASE( "M0: model task operator schemas declare the shared parameter surface",
           "[model9][m0][schema]" )
{
  using sicnu::operators::RSOperator;
  struct TaskSurface
  {
    std::shared_ptr<RSOperator> op;
    std::set<std::string> params;
  };
  std::set<std::string> segment = kCommonTaskParams;
  segment.insert( "format" ); // probability | labels | mask | confidence
  std::set<std::string> detect = kCommonTaskParams;
  detect.insert( "conf" );
  detect.insert( "nms_iou" );
  std::set<std::string> embedding = kCommonTaskParams;
  embedding.insert( "aggregate" ); // none | mean

  const std::vector<TaskSurface> taskOps = {
    { std::make_shared<RsSegmentOperator>(), std::move( segment ) },
    { std::make_shared<RsDetectOperator>(), std::move( detect ) },
    { std::make_shared<RsEmbeddingOperator>(), std::move( embedding ) },
  };
  for ( const TaskSurface &task : taskOps )
  {
    const std::set<std::string> declared = schemaPropertyNames( task.op->schema() );
    for ( const std::string &param : task.params )
    {
      INFO( task.op->name() << ": parameter '" << param
                            << "' parsed but missing from schema()" );
      CHECK( declared.count( param ) == 1 );
    }
  }
}

TEST_CASE( "M0: device tokens parse strictly — garbage is a refusal, never a fallback",
           "[model9][m0][device]" )
{
  RequestedDevice device;
  CHECK( RequestedDevice::parse( "", &device ) );
  CHECK( device.kind == RequestedDevice::Kind::Auto );
  CHECK( RequestedDevice::parse( "AUTO", &device ) );
  CHECK( device.kind == RequestedDevice::Kind::Auto );
  CHECK( RequestedDevice::parse( "cpu", &device ) );
  CHECK( device.kind == RequestedDevice::Kind::Cpu );
  CHECK( RequestedDevice::parse( "CUDA", &device ) );
  CHECK( device.kind == RequestedDevice::Kind::Cuda );
  CHECK( device.cudaIndex == 0 );
  CHECK( RequestedDevice::parse( "cuda:3", &device ) );
  CHECK( device.kind == RequestedDevice::Kind::Cuda );
  CHECK( device.cudaIndex == 3 );

  CHECK_FALSE( RequestedDevice::parse( "gpu", &device ) );
  CHECK_FALSE( RequestedDevice::parse( "cuda:-1", &device ) );
  CHECK_FALSE( RequestedDevice::parse( "cuda:x", &device ) );
  CHECK_FALSE( RequestedDevice::parse( "cuda:", &device ) );
  CHECK_FALSE( RequestedDevice::parse( "cuda:0 ", &device ) ); // no implicit trim
}

TEST_CASE( "M0: failure taxonomy projects every kind onto a real error code",
           "[model9][m0][taxonomy]" )
{
  // Every taxonomy kind must classify from its canonical message and project
  // to a non-Success code — the CLI/workflow payload contract.
  const std::vector<InferenceFailureKind> kinds = {
    InferenceFailureKind::Unknown,
    InferenceFailureKind::OutOfMemory,
    InferenceFailureKind::Canceled,
    InferenceFailureKind::ShapeMismatch,
    InferenceFailureKind::CorruptModel,
    InferenceFailureKind::NotLoaded,
    InferenceFailureKind::IncompatibleSchema,
    InferenceFailureKind::DeviceUnavailable,
    InferenceFailureKind::ProviderCrash,
    InferenceFailureKind::OutputInvalid,
    InferenceFailureKind::Timeout,
  };
  for ( InferenceFailureKind kind : kinds )
  {
    INFO( "kind " << static_cast<int>( kind ) );
    CHECK( errorCodeForInferenceFailure( kind ) != ErrorCode::Success );
  }
  CHECK( classifyInferenceError( "CUDA error: out of memory" ) ==
         InferenceFailureKind::OutOfMemory );
  CHECK( classifyInferenceError( "worker crashed during forward" ) ==
         InferenceFailureKind::ProviderCrash );
  CHECK( classifyInferenceError( "device cuda:1 is not addressable" ) ==
         InferenceFailureKind::DeviceUnavailable );
  // 9.0 taxonomy: a live-but-unresponsive provider is a Timeout, not a crash
  // — deterministic even when the message also mentions an exit.
  CHECK( classifyInferenceError( "python worker did not report ready (timed out or exited)" ) ==
         InferenceFailureKind::Timeout );
  CHECK( classifyInferenceError( "worker exited before responding" ) ==
         InferenceFailureKind::ProviderCrash );
  CHECK( errorCodeForInferenceFailure( InferenceFailureKind::Timeout ) ==
         ErrorCode::ExternalProcessTimeout );
}

// ---------------------------------------------------------------------------
// M3: multimodal feed graph — per-feed preprocessing + fingerprints
// ---------------------------------------------------------------------------

namespace {

/// Fake multi-input runtime recording the MEAN of every fed input's channel
/// data, so a test can assert exactly what normalization reached the model.
/// Output: one zero plane (band semantics irrelevant to these tests).
class RecordingMeanRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m9-fake"; }
    std::string backendName() const override { return "m9"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m9"; }

    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
    bool supportsMultiInput() const override { return true; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      fedMeans.clear();
      for ( const auto &nt : inputs )
      {
        const float *data = nt.second.dataFloat32();
        const std::size_t n = static_cast<std::size_t>( nt.second.countOrZero() );
        double sum = 0.0;
        for ( std::size_t i = 0; i < n; ++i )
          sum += data[i];
        fedMeans.push_back( n > 0 ? sum / static_cast<double>( n ) : 0.0 );
      }
      const TensorBlob &last = inputs.back().second;
      TensorBlob out;
      out.shape = { last.shape[0], 1,
                    last.rank() >= 4 ? last.shape[last.rank() - 2] : 1,
                    last.rank() >= 4 ? last.shape[last.rank() - 1] : 1 };
      out.bytes.assign( static_cast<std::size_t>( out.countOrZero() ) * sizeof( float ), 0 );
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    std::vector<double> fedMeans;
};

ModelInfo twoFeedModel()
{
  ModelInfo model;
  model.name = "m9-dual";
  model.framework = "m9-fake";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  ModelInputContract optical;
  optical.name = "optical";
  ModelInputContract aux;
  aux.name = "aux";
  model.inputs = { optical, aux };
  model.input = optical;
  model.output.classes = { "class" };
  return model;
}

} // namespace

TEST_CASE( "M3: per-feed preprocessing override normalizes each feed with its OWN contract",
           "[model9][m3][feeds]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString opticalPath =
    sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
      .withConstantValue( 1, 100.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "optical.tif" ) ) );
  const QString auxPath =
    sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
      .withConstantValue( 1, 3.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "aux.tif" ) ) );
  REQUIRE_FALSE( opticalPath.isEmpty() );
  REQUIRE_FALSE( auxPath.isEmpty() );

  auto runtime = std::make_shared<RecordingMeanRuntime>();
  ModelInfo model = twoFeedModel();
  // Per-input overrides: optical uses mean_std, aux uses linear scale —
  // neither matches the (empty) global contract.
  model.inputs[0].preprocessDeclared = true;
  model.inputs[0].preprocess.normalize = "mean_std";
  model.inputs[0].preprocess.mean = { 90.0 };
  model.inputs[0].preprocess.stdv = { 2.0 };
  model.inputs[1].preprocessDeclared = true;
  model.inputs[1].preprocess.normalize = "linear";
  model.inputs[1].preprocess.scale = 10.0;

  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  const TileInferenceStats stats = engine.runMultiInput(
    { NamedRasterFeed{ "optical", { opticalPath.toStdString() }, {} },
      NamedRasterFeed{ "aux", { auxPath.toStdString() }, {} } },
    out.toStdString(), context, {} );

  REQUIRE( runtime->fedMeans.size() == 2 );
  // optical: (100 − 90) / 2 = 5  — with the OLD global-only behavior this
  // would have been 100 (normalize=none), so this regression fails there.
  CHECK( runtime->fedMeans[0] == Catch::Approx( 5.0 ).margin( 1e-3 ) );
  // aux: 3 × 10 = 30 — again 3 under the old behavior.
  CHECK( runtime->fedMeans[1] == Catch::Approx( 30.0 ).margin( 1e-3 ) );
  // Provenance records the EFFECTIVE preprocess per feed.
  REQUIRE( stats.inputGrids.size() == 2 );
  CHECK( stats.inputGrids[0].preprocessNote == "mean_std" );
  CHECK( stats.inputGrids[1].preprocessNote == "linear" );
}

TEST_CASE( "M3: feeds without an override keep the global contract bit-for-bit",
           "[model9][m3][feeds]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path =
    sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
      .withConstantValue( 1, 100.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "a.tif" ) ) );
  REQUIRE_FALSE( path.isEmpty() );

  auto runtime = std::make_shared<RecordingMeanRuntime>();
  ModelInfo model = twoFeedModel();
  model.preprocess.normalize = "linear";
  model.preprocess.scale = 0.5;
  // No preprocessDeclared anywhere: both feeds take the global contract.

  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  engine.runMultiInput(
    { NamedRasterFeed{ "optical", { path.toStdString() }, {} },
      NamedRasterFeed{ "aux", { path.toStdString() }, {} } },
    dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(), context, {} );
  REQUIRE( runtime->fedMeans.size() == 2 );
  CHECK( runtime->fedMeans[0] == Catch::Approx( 50.0 ).margin( 1e-3 ) );
  CHECK( runtime->fedMeans[1] == Catch::Approx( 50.0 ).margin( 1e-3 ) );
}

TEST_CASE( "M3: feed fingerprint is deterministic, structure-complete and honest about content",
           "[model9][m3][fingerprint]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path =
    sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 2, GDT_Float32 )
      .withConstantValue( 1, 1.0f )
      .withConstantValue( 2, 2.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "fp.tif" ) ) );
  REQUIRE_FALSE( path.isEmpty() );

  const Json::Value fp =
    TileInferenceEngine::feedFingerprint( path.toStdString(), { 1, 2 }, 256LL * 1024 * 1024 );
  CHECK( fp["width"].asInt() == 16 );
  CHECK( fp["height"].asInt() == 16 );
  CHECK( fp["band_count"].asInt() == 2 );
  CHECK( fp["band_dtypes"].size() == 2 );
  CHECK( fp["geotransform"].size() == 6 );
  CHECK( fp["selected_bands"].size() == 2 );
  // Small file under the bound: a REAL content digest is recorded.
  CHECK( fp["content"]["sha256"].asString().size() == 64 );
  CHECK( fp["content"]["bytes"].asInt64() > 0 );

  // Determinism: the same raster fingerprints identically.
  const Json::Value again =
    TileInferenceEngine::feedFingerprint( path.toStdString(), { 1, 2 }, 256LL * 1024 * 1024 );
  CHECK( Json::writeString( Json::StreamWriterBuilder(), fp )
         == Json::writeString( Json::StreamWriterBuilder(), again ) );

  // Oversize bound: no digest-shaped lie — the marker names the reason.
  const Json::Value capped =
    TileInferenceEngine::feedFingerprint( path.toStdString(), { 1 }, 1 );
  CHECK( capped["content"]["sha256"].isNull() );
  CHECK( capped["content"]["reason"].asString() == "file-too-large" );
  CHECK( !capped["content"]["mtime_utc"].asString().empty() );
  CHECK( capped["selected_bands"].size() == 1 );
}

TEST_CASE( "M3: manifest closed vocabulary accepts the per-input preprocess and refuses bad tokens",
           "[model9][m3][manifest]" )
{
  const std::string base = R"({
    "name": "m9-manifest", "task": "segmentation", "framework": "onnx",
    "inputs": [
      { "name": "optical", "preprocess": { "normalize": "mean_std", "mean": [90], "std": [2] } },
      { "name": "aux" }
    ]
  })";
  CHECK( ModelCatalog::instance().validateManifestJson( base ).empty() );

  const std::string badNormalize = R"({
    "name": "m9-manifest", "task": "segmentation", "framework": "onnx",
    "inputs": [
      { "name": "optical", "preprocess": { "normalize": "quantum" } }
    ]
  })";
  const auto issues = ModelCatalog::instance().validateManifestJson( badNormalize );
  REQUIRE( !issues.empty() );
  CHECK_THAT( issues.front(), Catch::Matchers::ContainsSubstring( "inputs[].preprocess.normalize" ) );

  // Unknown keys inside the override stay a closed-vocabulary error.
  const std::string unknownKey = R"({
    "name": "m9-manifest", "task": "segmentation", "framework": "onnx",
    "inputs": [
      { "name": "optical", "preprocess": { "magic": 1 } }
    ]
  })";
  const auto unknown = ModelCatalog::instance().validateManifestJson( unknownKey );
  REQUIRE( !unknown.empty() );
  CHECK_THAT( unknown.front(), Catch::Matchers::ContainsSubstring( "inputs[0].preprocess.magic" ) );
}

// ---------------------------------------------------------------------------
// M8: consumer-side provenance verification
// ---------------------------------------------------------------------------

TEST_CASE( "M8: verifyProductProvenance gives typed verdicts for every sidecar state",
           "[model9][m8][provenance]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString product =
    sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 1, GDT_Float32 )
      .withConstantValue( 1, 1.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "product.tif" ) ) );
  REQUIRE_FALSE( product.isEmpty() );
  const std::string productPath = product.toStdString();
  const std::string sidecarPath = provenanceSidecarPath( productPath );

  // Missing product beats every other verdict.
  CHECK( verifyProductProvenance( dir.filePath( "nope.tif" ).toStdString() ).state
         == ProvenanceVerdict::State::ProductMissing );

  // Product WITHOUT sidecar — the 8.0 crash window, now consumer-detectable.
  CHECK( verifyProductProvenance( productPath ).state
         == ProvenanceVerdict::State::MissingSidecar );

  const auto writeSidecar = [ & ]( const std::string &text ) {
    QFile sidecar( QString::fromStdString( sidecarPath ) );
    REQUIRE( sidecar.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    sidecar.write( text.data(), static_cast<qint64>( text.size() ) );
    sidecar.close();
  };

  writeSidecar( "{ this is not json" );
  CHECK( verifyProductProvenance( productPath ).state
         == ProvenanceVerdict::State::MalformedSidecar );

  writeSidecar( R"({ "schema": "some-other-prov/9", "model": {} })" );
  CHECK( verifyProductProvenance( productPath ).state
         == ProvenanceVerdict::State::UnsupportedSchema );

  // A well-formed 8.0 document verifies Ok — with and without expectations.
  const std::string goodSidecar = R"({
    "schema": "exp-rs-prov/1",
    "model": { "name": "m", "identity_tag": "m@1", "content_digest": "abc" },
    "execution": { "backend": "onnxruntime", "device": "cuda:0" },
    "inputs": [],
    "output": { "bands": 1, "width": 8, "height": 8 }
  })";
  writeSidecar( goodSidecar );
  CHECK( verifyProductProvenance( productPath ).state == ProvenanceVerdict::State::Ok );

  ProvenanceExpectation expectation;
  expectation.modelIdentityTag = "m@1";
  expectation.modelContentDigest = "abc";
  expectation.backend = "onnxruntime";
  CHECK( verifyProductProvenance( productPath, expectation ).state
         == ProvenanceVerdict::State::Ok );

  // Any expectation mismatch is a typed ModelMismatch naming the field.
  expectation.backend = "opencv_dnn";
  const ProvenanceVerdict mismatch = verifyProductProvenance( productPath, expectation );
  CHECK( mismatch.state == ProvenanceVerdict::State::ModelMismatch );
  CHECK_THAT( mismatch.detail, Catch::Matchers::ContainsSubstring( "backend" ) );

  // The recorded output geometry must describe the ACTUAL raster.
  writeSidecar( R"({
    "schema": "exp-rs-prov/1",
    "model": { "name": "m" },
    "output": { "bands": 1, "width": 99, "height": 8 }
  })" );
  const ProvenanceVerdict grid = verifyProductProvenance( productPath );
  CHECK( grid.state == ProvenanceVerdict::State::GridMismatch );
  CHECK_THAT( grid.detail, Catch::Matchers::ContainsSubstring( "width" ) );

  // Staleness: a product rewritten AFTER its sidecar is detected via file
  // times (explicit setFileTime — deterministic, no sleeping).
  writeSidecar( goodSidecar );
  REQUIRE( verifyProductProvenance( productPath ).state == ProvenanceVerdict::State::Ok );
  const QDateTime past = QDateTime::currentDateTimeUtc().addDays( -1 );
  {
    QFile sidecarFile( QString::fromStdString( sidecarPath ) );
    REQUIRE( sidecarFile.open( QIODevice::ReadWrite ) );
    REQUIRE( sidecarFile.setFileTime( past, QFileDevice::FileModificationTime ) );
  }
  const ProvenanceVerdict stale = verifyProductProvenance( productPath );
  CHECK( stale.state == ProvenanceVerdict::State::StaleProduct );
  CHECK_THAT( stale.detail, Catch::Matchers::ContainsSubstring( "newer than the sidecar" ) );
}

// ---------------------------------------------------------------------------
// M7: packaging & identity — aux files, package digest, session identity
// ---------------------------------------------------------------------------

namespace {

/// RAII registry cleanup for programmatic catalog entries.
struct CatalogCleanup
{
    explicit CatalogCleanup( std::string idOrName ) : id( std::move( idOrName ) ) {}
    ~CatalogCleanup() { ModelCatalog::instance().unregister( id ); }
    std::string id;
};

std::string sha256OfFile( const QString &path )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return {};
  return QString::fromUtf8(
           QCryptographicHash::hash( file.readAll(), QCryptographicHash::Sha256 ).toHex() )
    .toStdString();
}

/// Reads one band of a raster as float (GDAL convert-on-read).
std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds != nullptr );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  REQUIRE( ds->GetRasterBand( band )
             ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                         GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return data;
}

/// Counts how many sessions the registry actually loaded (cache misses).
struct LoadCounter
{
    LoadCounter()
    {
      ModelRuntimeRegistry::instance().resetLoadCount();
      ModelRuntimeRegistry::instance().releaseAll();
    }
    ~LoadCounter() { ModelRuntimeRegistry::instance().releaseAll(); }
    std::size_t loads() const { return ModelRuntimeRegistry::instance().totalSessionsLoaded(); }
};

} // namespace

TEST_CASE( "M7: package.aux_files closed vocabulary and validation",
           "[model9][m7][manifest]" )
{
  std::string valid = R"({
    "name": "m9-pkg", "task": "segmentation", "framework": "onnx",
    "package": { "aux_files": [
      { "path": "classes.json", "role": "class_ontology",
        "checksum": "0000000000000000000000000000000000000000000000000000000000000000" }
    ] }
  })";
  CHECK( ModelCatalog::instance().validateManifestJson( valid ).empty() );

  const std::string badChecksum = valid.replace(
    valid.find( "0000" ), 4, "zzzz" );
  const auto issues = ModelCatalog::instance().validateManifestJson( badChecksum );
  REQUIRE( !issues.empty() );
  CHECK_THAT( issues.front(), Catch::Matchers::ContainsSubstring( "not a valid SHA-256" ) );

  const std::string unknownKey = R"({
    "name": "m9-pkg", "task": "segmentation", "framework": "onnx",
    "package": { "aux_files": [ { "path": "a.json", "role": "ontology", "magick": 1 } ] }
  })";
  const auto unknown = ModelCatalog::instance().validateManifestJson( unknownKey );
  REQUIRE( !unknown.empty() );
  CHECK_THAT( unknown.front(),
              Catch::Matchers::ContainsSubstring( "package.aux_files[0].magick" ) );
}

TEST_CASE( "M7: aux files are digest-verified at resolve and key the session identity",
           "[model9][m7][package]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Weights + one aux file with known bytes.
  const QString weights = dir.filePath( QStringLiteral( "weights.onnx" ) );
  const QString classes = dir.filePath( QStringLiteral( "classes.json" ) );
  {
    QFile w( weights );
    REQUIRE( w.open( QIODevice::WriteOnly ) );
    w.write( "fake-weights-bytes" );
  }
  {
    QFile c( classes );
    REQUIRE( c.open( QIODevice::WriteOnly ) );
    c.write( R"({ "0": "water", "1": "land" })" );
  }
  const std::string weightsDigest = sha256OfFile( weights );
  REQUIRE( weightsDigest.size() == 64 );
  const std::string classesDigest = sha256OfFile( classes );

  const std::string manifest = R"({
    "name": "m9-pkg", "task": "segmentation", "framework": "m9-fake",
    "artifact": { "path": "weights.onnx", "checksum": ")" + weightsDigest + R"(" },
    "package": { "aux_files": [
      { "path": "classes.json", "role": "class_ontology", "checksum": ")" + classesDigest + R"(" }
    ] }
  })";
  std::string error;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString(), &error ) );
  CatalogCleanup cleanup( "m9-pkg" );

  // Matching aux digest: Ready, with a package digest.
  const auto ok = ModelCatalog::instance().resolve( "m9-pkg", &error );
  REQUIRE( ok.has_value() );
  CHECK( ok->readiness == ModelReadiness::Ready );
  CHECK( ok->packageDigest.size() == 64 );

  // Session identity: the FIRST acquire loads; an identical acquire hits the
  // session cache (no second load).
  auto runtime = std::make_shared<RecordingMeanRuntime>();
  ModelRuntimeRegistry::instance().registerProvider(
    "m9-fake", [ runtime ]( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) {
      return runtime;
    } );
  {
    LoadCounter counter;
    ( void )ModelRuntimeRegistry::instance().acquire( *ok );
    ( void )ModelRuntimeRegistry::instance().acquire( *ok );
    CHECK( counter.loads() == 1 );
  }

  // A CHANGED aux file must invalidate the old session identity: after the
  // byte change the package fails verification (typed) — the stale package
  // can never execute.
  {
    QFile c( classes );
    REQUIRE( c.open( QIODevice::Append ) );
    c.write( " " );
  }
  std::string reRegisterError;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString(),
    &reRegisterError ) );
  const auto changed = ModelCatalog::instance().resolve( "m9-pkg", &error );
  REQUIRE( changed.has_value() );
  CHECK( changed->readiness == ModelReadiness::ChecksumMismatch );
  CHECK_THAT( changed->readinessReason,
              Catch::Matchers::ContainsSubstring( "auxiliary file" ) );
}

TEST_CASE( "M7: aux file removal fails readiness with the file named",
           "[model9][m7][package]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString weights = dir.filePath( QStringLiteral( "weights.onnx" ) );
  const QString classes = dir.filePath( QStringLiteral( "classes.json" ) );
  {
    QFile w( weights );
    REQUIRE( w.open( QIODevice::WriteOnly ) );
    w.write( "w" );
  }
  {
    QFile c( classes );
    REQUIRE( c.open( QIODevice::WriteOnly ) );
    c.write( "{}" );
  }
  const std::string manifest = R"({
    "name": "m9-pkg2", "task": "segmentation", "framework": "m9-fake",
    "artifact": { "path": "weights.onnx" },
    "package": { "aux_files": [ { "path": "classes.json", "role": "class_ontology" } ] }
  })";
  std::string error;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString(), &error ) );
  CatalogCleanup cleanup( "m9-pkg2" );

  REQUIRE( QFile::remove( classes ) );
  // Verification runs at registration time; re-registering re-verifies the
  // package against the CURRENT file state (the freshness check consumers get).
  std::string reError;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    manifest, dir.filePath( QStringLiteral( "model.json" ) ).toStdString(), &reError ) );
  const auto info = ModelCatalog::instance().resolve( "m9-pkg2", &error );
  REQUIRE( info.has_value() );
  CHECK( info->readiness == ModelReadiness::MissingArtifact );
  CHECK_THAT( info->readinessReason, Catch::Matchers::ContainsSubstring( "classes.json" ) );
}

// ---------------------------------------------------------------------------
// M5: feather tile blending — known answers
// ---------------------------------------------------------------------------

namespace {

/// Fake single-input runtime: predicts a CONSTANT plane whose value equals
/// the value the raster carries at the tile-window center (the core center).
/// Two tiles over a step raster therefore predict different constants, and
/// the seam behavior (hard step vs cosine blend) is exactly observable.
class StepConstantRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m9-fake"; }
    std::string backendName() const override { return "m9step"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m9step"; }

    bool supportsMultiInput() const override { return false; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int B = blob.size[0];
      const int C = blob.size[1];
      const int H = blob.size[2];
      const int W = blob.size[3];
      const float v = blob.ptr<float>( 0 )[static_cast<std::size_t>( H / 2 ) * W
                                            + static_cast<std::size_t>( W / 2 )];
      const int dims[4] = { B, C, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( v ) );
      return out;
    }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      const cv::Mat mat = inputs.front().second.toMat();
      return { NamedTensor{ std::string(), TensorBlob::fromMat( infer( mat ) ) } };
    }
};

} // namespace

TEST_CASE( "M5: feather blending averages the halo overlap with cosine weights",
           "[model9][m5][blend]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // A step raster: 10 for the left half, 20 for the right half. The fake
  // model predicts the step value it centered on per tile.
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 16, 1, GDT_Float32 );
  builder.withRect( 1, 0, 0, 15, 15, 10.0f );
  builder.withRect( 1, 16, 0, 31, 15, 20.0f );
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "step.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  ModelInfo model;
  model.name = "m9-blend";
  model.framework = "m9-fake";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  model.tiling.halo = 4;
  model.output.classes = { "class" };

  auto runtime = std::make_shared<StepConstantRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;

  TileInferenceRunOptions none;
  none.blend = TileBlend::None;
  const QString plainPath = dir.filePath( QStringLiteral( "plain.tif" ) );
  engine.run( raster.toStdString(), {}, plainPath.toStdString(), context, none );

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString blendedPath = dir.filePath( QStringLiteral( "blended.tif" ) );
  engine.run( raster.toStdString(), {}, blendedPath.toStdString(), context, feather );

  int w = 0;
  int h = 0;
  const std::vector<float> plain = readBand( plainPath, 1, w, h );
  const std::vector<float> blended = readBand( blendedPath, 1, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 16 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };
  ( void )at;

  // Hard-edge stitch: the seam at x=16 steps 10 → 20; the halo overlap on
  // x in [12,16) is cropped away (tile A's core wins up to x=15).
  CHECK( at( plain, 8, 8 ) == 10.0f );
  CHECK( at( plain, 14, 8 ) == 10.0f );
  CHECK( at( plain, 24, 8 ) == 20.0f );

  // Feather: inside pure cores the predictions are unchanged…
  CHECK( at( blended, 8, 8 ) == 10.0f );
  CHECK( at( blended, 24, 8 ) == 20.0f );
  // …in the overlap the two constants average with the cosine ramp.
  // x=14 (row 8): tile A core weight 1; tile B halo distance 2/4 → wB = 0.5:
  //   (10·1 + 20·0.5) / 1.5 = 40/3
  CHECK( at( blended, 14, 8 ) == Catch::Approx( 40.0f / 3.0f ).margin( 1e-3 ) );
  // x=18: tile A halo distance 3/4 → wA = 0.5·(1−cos(3π/4)) ≈ 0.853553;
  // tile B core weight 1: (10·wA + 20·1) / (wA + 1) ≈ 15.397
  const double wA = 0.5 * ( 1.0 - std::cos( 3.0 * 3.14159265358979323846 / 4.0 ) );
  CHECK( at( blended, 18, 8 ) == Catch::Approx( ( 10.0 * wA + 20.0 ) / ( wA + 1.0 ) ).margin( 1e-3 ) );
}

// ---------------------------------------------------------------------------
// M4: temporal lane regression — dynamic-T sequence feeds on the 9.0 engine
// ---------------------------------------------------------------------------

namespace {

/// Fake runtime recording the SHAPES of every fed input (rank/shape known
/// answers for the sequence lane) and echoing the last input back.
class RecordingShapeRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m9-fake"; }
    std::string backendName() const override { return "m9shape"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m9shape"; }

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
      TensorBlob out;
      if ( rank5Output )
        out = last; // echo: a rank-5 input yields a rank-5 output
      else
      {
        out.shape = { last.shape[0], 1,
                      last.rank() >= 4 ? last.shape[last.rank() - 2] : 1,
                      last.rank() >= 4 ? last.shape[last.rank() - 1] : 1 };
        out.bytes.assign( static_cast<std::size_t>( out.countOrZero() ) * sizeof( float ), 0 );
      }
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    std::vector<int> fedRanks;
    std::vector<std::vector<std::int64_t>> fedShapes;
    bool rank5Output = false;
};

} // namespace

TEST_CASE( "M4: dynamic-T sequence feeds keep NCTHW truth and per-frame provenance",
           "[model9][m4][temporal]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  std::vector<QString> frames;
  for ( int i = 0; i < 3; ++i )
  {
    frames.push_back(
      sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
        .withConstantValue( 1, static_cast<float>( i + 1 ) )
        .writeToDisk( dir.filePath( QStringLiteral( "f%1.tif" ).arg( i ) ) ) );
    REQUIRE_FALSE( frames.back().isEmpty() );
  }

  auto runtime = std::make_shared<RecordingShapeRuntime>();
  ModelInfo model;
  model.name = "m9-seq";
  model.framework = "m9-fake";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  ModelInputContract seq;
  seq.name = "series";
  seq.layout = "NCTHW";
  seq.temporalCollapse = "sequence";
  seq.temporalDynamic = true; // the FEED defines T
  model.inputs = { seq };
  model.input = seq;
  model.output.classes = { "class" };

  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  NamedRasterFeed feed;
  feed.name = "series";
  feed.paths = { frames[0].toStdString(), frames[1].toStdString(), frames[2].toStdString() };
  feed.timestamps = { "2026-01-01T00:00:00Z", "2026-01-11T00:00:00Z", "2026-01-21T00:00:00Z" };
  feed.preparedFrom = { "orig-1.tif", "orig-2.tif", "orig-3.tif" };
  const TileInferenceStats stats = engine.runMultiInput(
    { feed }, dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(), context, {} );

  // The model saw ONE rank-5 NCTHW tensor with T=3 (feed-defined) and C=1.
  REQUIRE( runtime->fedRanks.size() == 1 );
  CHECK( runtime->fedRanks[0] == 5 );
  REQUIRE( runtime->fedShapes[0].size() == 5 );
  CHECK( runtime->fedShapes[0][1] == 3 ); // T from the feed
  CHECK( runtime->fedShapes[0][2] == 1 ); // C
  // Per-frame provenance: frames count + parallel preparedFrom recorded.
  REQUIRE( stats.inputGrids.size() == 1 );
  CHECK( stats.inputGrids[0].frames == 3 );
  REQUIRE( stats.inputGrids[0].preparedFrom.size() == 3 );
  CHECK( stats.inputGrids[0].preparedFrom[2] == "orig-3.tif" );
}

TEST_CASE( "M4: a rank-5 model output is a typed refusal on the raster lane",
           "[model9][m4][temporal]" )
{
  // The 8.0 sequence contract fixes RASTER outputs at rank-4; a rank-5 head
  // (per-frame output maps) has no writer semantics yet and must refuse
  // loudly instead of silently stitching the wrong axis.
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster =
    sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 1, GDT_Float32 )
      .withConstantValue( 1, 1.0f )
      .writeToDisk( dir.filePath( QStringLiteral( "in.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  auto runtime = std::make_shared<RecordingShapeRuntime>();
  ModelInfo model;
  model.name = "m9-seq5";
  model.framework = "m9-fake";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  ModelInputContract seq;
  seq.name = "series";
  seq.layout = "NCTHW";
  seq.temporalCollapse = "sequence";
  seq.temporalDynamic = true;
  model.inputs = { seq };
  model.input = seq;
  model.output.classes = { "class" };

  // A rank-5 output: (B,T,C,H,W) — the fake echoes the input shape back.
  RecordingShapeRuntime *raw = runtime.get();
  raw->rank5Output = true;

  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  NamedRasterFeed feed;
  feed.name = "series";
  feed.paths = { raster.toStdString(), raster.toStdString() };
  REQUIRE_THROWS_WITH( engine.runMultiInput(
                         { feed }, dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(),
                         context, {} ),
                       Catch::Matchers::ContainsSubstring( "rank-4" ) );
}

// ---------------------------------------------------------------------------
// M6: per-class product metadata
// ---------------------------------------------------------------------------

/// Two-class one-hot fake: the input raster's own values (10 / 20) select
/// the class plane, so the product splits exactly at the value boundary.
class TwoClassRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m9-fake"; }
    std::string backendName() const override { return "m9two"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://m9two"; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int B = blob.size[0];
      const int H = blob.size[2];
      const int W = blob.size[3];
      const int dims[4] = { B, 2, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.0f ) );
      for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
        {
          const float v = blob.ptr<float>( 0 )[static_cast<std::size_t>( y ) * W + x];
          const int cls = v > 15.0f ? 1 : 0;
          out.ptr<float>( 0, cls )[static_cast<std::size_t>( y ) * W + x] = 1.0f;
        }
      return out;
    }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      const cv::Mat mat = inputs.front().second.toMat();
      return { NamedTensor{ std::string(), TensorBlob::fromMat( infer( mat ) ) } };
    }
};

TEST_CASE( "M6: labels products tally per-product-class pixels into payload metadata",
           "[model9][m6][classes]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // Left half class 0, right half class 1 → a 50/50 labels product.
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 1, GDT_Float32 );
  builder.withRect( 1, 0, 0, 8, 16, 10.0f );  // exclusive x1/y1: left half
  builder.withRect( 1, 8, 0, 16, 16, 20.0f ); // right half
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "two.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  auto runtime = std::make_shared<TwoClassRuntime>();
  ModelInfo model;
  model.name = "m9-classes";
  model.framework = "m9-fake";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  model.output.classes = { "left", "right" };

  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  TileInferenceRunOptions options;
  options.outputMode = RasterOutputMode::Labels;
  const TileInferenceStats stats = engine.run(
    raster.toStdString(), {}, dir.filePath( QStringLiteral( "labels.tif" ) ).toStdString(),
    context, options );

  REQUIRE( stats.classPixelCounts.size() == 2 );
  CHECK( stats.classPixelCounts[0] == 8 * 16 );
  CHECK( stats.classPixelCounts[1] == 8 * 16 );
}
