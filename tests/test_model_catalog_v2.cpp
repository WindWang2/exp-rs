// tests/test_model_catalog_v2.cpp — manifest v2 contracts, real readiness
// states (artifact resolution, checksums, diagnostics) and ranking gates.
// Catch2; QtCore only (no OpenCV — the catalog is backend-agnostic).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <optional>
#include <string>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::modelReadinessName;

/// Writes a manifest (and optionally an artifact file) under
/// <dir>/<modelName>/model.json, mimicking the models/ layout.
void writeManifest( const QTemporaryDir &dir, const QString &modelName,
                    const QByteArray &manifestJson,
                    const QByteArray &artifactBytes = QByteArray(),
                    const QString &artifactName = QStringLiteral( "weights.onnx" ) )
{
  const QDir modelDir( dir.path() );
  REQUIRE( modelDir.mkpath( modelName ) );
  QFile manifest( dir.filePath( modelName + QStringLiteral( "/model.json" ) ) );
  REQUIRE( manifest.open( QIODevice::WriteOnly ) );
  manifest.write( manifestJson );
  manifest.close();
  if ( !artifactBytes.isNull() )
  {
    QFile artifact( dir.filePath( modelName + QStringLiteral( "/" ) + artifactName ) );
    REQUIRE( artifact.open( QIODevice::WriteOnly ) );
    artifact.write( artifactBytes );
    artifact.close();
  }
}

QString sha256( const QByteArray &data )
{
  return QString::fromLatin1( QCryptographicHash::hash( data, QCryptographicHash::Sha256 ).toHex() );
}

} // namespace

TEST_CASE( "v1 template manifests report missing artifact honestly", "[models][catalog]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "sam-building" ), R"({
      "name": "sam-building",
      "task": "segmentation",
      "input": "raster",
      "output": "polygon",
      "framework": "onnx",
      "path": "",
      "gpu": true,
      "accuracy": 0.89
  })" );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "sam-building" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::MissingArtifact );
  CHECK( model->readinessReason.find( "artifact" ) != std::string::npos );
  // Legacy fields still parse exactly as before.
  CHECK( model->task == "segmentation" );
  CHECK( model->gpu );
  CHECK( model->accuracy == Catch::Approx( 0.89 ) );
  CHECK( model->framework == "onnx" );
  // A template must not be selectable as a ready model.
  const auto ranked = catalog.rankModels( {} );
  REQUIRE( ranked.size() == 1 );
  CHECK_FALSE( ranked[0].compatible );
}

TEST_CASE( "v1 manifest artifacts resolve relative to the manifest directory", "[models][catalog]" )
{
  QTemporaryDir dir;
  const QByteArray artifactBytes = "fake-onnx-weights";
  writeManifest( dir, QStringLiteral( "ident" ), R"({
      "name": "ident",
      "task": "segmentation",
      "framework": "onnx",
      "path": "weights.onnx"
  })",
                 artifactBytes );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "ident" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::Ready );
  CHECK( model->readinessReason.empty() );
  // Absolute and pointing at the file next to the manifest — NOT the CWD.
  CHECK( QFileInfo( QString::fromStdString( model->resolvedArtifactPath ) ).isAbsolute() );
  CHECK( QFileInfo( QString::fromStdString( model->resolvedArtifactPath ) ).fileName()
         == QStringLiteral( "weights.onnx" ) );
  CHECK( QFile::exists( QString::fromStdString( model->resolvedArtifactPath ) ) );
}

TEST_CASE( "v2 manifests parse the full inference contract", "[models][catalog]" )
{
  QTemporaryDir dir;
  const QByteArray artifactBytes = "v2-weights";
  const QString checksum = sha256( artifactBytes );
  const QByteArray manifest = QString( R"({
      "name": "v2-model",
      "version": "1.2.3",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": {
          "path": "weights.onnx",
          "checksum": "%1",
          "size_bytes": %2
      },
      "input": {
          "data_type": "raster",
          "band_roles": ["Red", "Green", "Blue"],
          "dtype": "float32",
          "layout": "NCHW",
          "width": 512,
          "height": 512
      },
      "preprocess": {
          "normalize": "mean_std",
          "mean": [0.485, 0.456, 0.406],
          "std": [0.229, 0.224, 0.225],
          "scale": 0.00392,
          "resize": "to_input",
          "interpolation": "bilinear",
          "nodata_policy": "zero"
      },
      "tiling": {
          "supported": true,
          "tile_size": 512,
          "overlap": 32,
          "batch_size": 4
      },
      "output": {
          "type": "raster",
          "tensor_names": ["probability"],
          "classes": ["background", "building"]
      },
      "postprocess": {
          "mask_threshold": 0.5
      },
      "domain": {
          "sensors": ["GF-2"],
          "resolution_range": [0.5, 2.0]
      },
      "runtime": {
          "gpu": true,
          "cpu_fallback": true,
          "estimated_ram_mb": 768,
          "estimated_vram_mb": 2048
      }
  })" )
                                   .arg( checksum )
                                   .arg( artifactBytes.size() )
                                   .toUtf8();
  writeManifest( dir, QStringLiteral( "v2-model" ), manifest, artifactBytes );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "v2-model" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::Ready );

  CHECK( model->artifact.path == "weights.onnx" );
  CHECK( model->artifact.sizeBytes == static_cast<unsigned long long>( artifactBytes.size() ) );
  CHECK( !model->artifact.checksum.empty() );

  CHECK( model->input.dataType == "raster" );
  CHECK( model->input.dtype == "float32" );
  CHECK( model->input.layout == "NCHW" );
  REQUIRE( model->input.bandRoles.size() == 3 );
  CHECK( model->input.bandRoles[0] == "Red" );
  CHECK( model->input.width == 512 );
  CHECK( model->input.height == 512 );
  CHECK( model->supportedBandRoles.size() == 3 );

  CHECK( model->preprocess.normalize == "mean_std" );
  REQUIRE( model->preprocess.mean.size() == 3 );
  CHECK( model->preprocess.mean[0] == Catch::Approx( 0.485 ) );
  REQUIRE( model->preprocess.stdv.size() == 3 );
  CHECK( model->preprocess.stdv[2] == Catch::Approx( 0.225 ) );
  CHECK( model->preprocess.scale == Catch::Approx( 0.00392 ) );
  CHECK( model->preprocess.resize == "to_input" );
  CHECK( model->preprocess.interpolation == "bilinear" );
  CHECK( model->preprocess.nodataPolicy == "zero" );

  CHECK( model->tiling.supported );
  CHECK( model->tiling.tileSize == 512 );
  CHECK( model->tiling.overlap == 32 );
  CHECK( model->tiling.batchSize == 4 );

  CHECK( model->output.type == "raster" );
  CHECK( model->output.classes.size() == 2 );
  // #646: output.threshold / postprocess.nms / polygonize / simplify are
  // declared-but-unenforced keys — no valid manifest declares them anymore
  // (the enforcement test proves they are rejected). The enforceable mask
  // threshold stays.
  CHECK( model->output.threshold < 0.0 );

  CHECK( model->postprocess.maskThreshold == Catch::Approx( 0.5 ) );
  CHECK_FALSE( model->postprocess.polygonize );
  CHECK( model->postprocess.simplify == Catch::Approx( 0.0 ) );

  CHECK( model->runtime.gpu );
  CHECK( model->runtime.cpuFallback );
  CHECK( model->runtime.estimatedRamMb == 768 );
  CHECK( model->runtime.estimatedVramMb == 2048 );
  // Legacy mirrors stay consistent.
  CHECK( model->gpu );
  CHECK( model->estimatedVramMb == 2048 );
  CHECK( model->supportsTiling );

  // toJson exposes the v2 surface for PART B consumers.
  const Json::Value json = model->toJson();
  CHECK( json["readiness"].asString() == "ready" );
  CHECK( json.isMember( "artifact" ) );
  CHECK( json.isMember( "preprocess" ) );
  CHECK( json["tiling"]["tile_size"].asInt() == 512 );
  CHECK( json["runtime"]["estimated_ram_mb"].asInt() == 768 );
  // Legacy keys unchanged.
  CHECK( json["name"].asString() == "v2-model" );
  CHECK( json["gpu"].asBool() );
}

TEST_CASE( "artifact checksum mismatches are detected", "[models][catalog]" )
{
  QTemporaryDir dir;
  const QByteArray artifactBytes = "real-weights";
  const QString wrongChecksum = QString( 'f' ).repeated( 64 );
  const QByteArray manifest = QString( R"({
      "name": "corrupted",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx", "checksum": "%1" }
  })" )
                                   .arg( wrongChecksum )
                                   .toUtf8();
  writeManifest( dir, QStringLiteral( "corrupted" ), manifest, artifactBytes );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "corrupted" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::ChecksumMismatch );
  CHECK( model->readinessReason.find( "checksum mismatch" ) != std::string::npos );

  // Correct checksum (sha256: prefix + uppercase accepted) → Ready.
  const QString right = sha256( artifactBytes );
  QFile fixup( dir.filePath( QStringLiteral( "corrupted/model.json" ) ) );
  REQUIRE( fixup.open( QIODevice::WriteOnly ) );
  fixup.write( QString( R"({
      "name": "corrupted",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx", "checksum": "SHA256:%1" }
  })" )
                   .arg( right.toUpper() )
                   .toUtf8() );
  fixup.close();
  catalog.reload();
  const auto fixed = catalog.find( "corrupted" );
  REQUIRE( fixed.has_value() );
  CHECK( fixed->readiness == ModelReadiness::Ready );
}

TEST_CASE( "declared artifact size mismatches are detected", "[models][catalog]" )
{
  QTemporaryDir dir;
  const QByteArray manifest = R"({
      "name": "wrong-size",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx", "size_bytes": 4 }
  })";
  writeManifest( dir, QStringLiteral( "wrong-size" ), manifest, QByteArray( "much-larger-file" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "wrong-size" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::ChecksumMismatch );
  CHECK( model->readinessReason.find( "size mismatch" ) != std::string::npos );
}

TEST_CASE( "broken manifests surface as catalog issues", "[models][catalog]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "bad-json" ), "{ not json" );
  writeManifest( dir, QStringLiteral( "no-name" ), R"({ "task": "segmentation" })" );
  // Duplicate names: first (alphabetical) wins, second recorded as an issue.
  writeManifest( dir, QStringLiteral( "dup-a" ), R"({ "name": "dup", "task": "a" })" );
  writeManifest( dir, QStringLiteral( "dup-b" ), R"({ "name": "dup", "task": "b" })" );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto issues = catalog.issues();
  REQUIRE( issues.size() == 3 );
  bool sawBadJson = false, sawNoName = false, sawDuplicate = false;
  for ( const auto &issue : issues )
  {
    if ( issue.manifestPath.find( "bad-json" ) != std::string::npos )
      sawBadJson = true;
    if ( issue.manifestPath.find( "no-name" ) != std::string::npos )
      sawNoName = true;
    if ( issue.manifestPath.find( "dup-b" ) != std::string::npos )
      sawDuplicate = true;
  }
  CHECK( sawBadJson );
  CHECK( sawNoName );
  CHECK( sawDuplicate );

  CHECK( catalog.models().size() == 1 ); // only "dup" from dup-a
  const auto dup = catalog.find( "dup" );
  REQUIRE( dup.has_value() );
  CHECK( dup->task == "a" );
}

TEST_CASE( "internally inconsistent v2 contracts are invalid manifests", "[models][catalog]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "bad-mean" ), R"({
      "name": "bad-mean",
      "input": { "band_roles": ["Red", "Green", "Blue"] },
      "preprocess": { "normalize": "mean_std", "mean": [0.5, 0.5] }
  })",
                 QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "bad-resize" ), R"({
      "name": "bad-resize",
      "preprocess": { "resize": "to_input" }
  })",
                 QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto badMean = catalog.find( "bad-mean" );
  REQUIRE( badMean.has_value() );
  CHECK( badMean->readiness == ModelReadiness::InvalidManifest );
  CHECK( badMean->readinessReason.find( "mean" ) != std::string::npos );

  const auto badResize = catalog.find( "bad-resize" );
  REQUIRE( badResize.has_value() );
  CHECK( badResize->readiness == ModelReadiness::InvalidManifest );
  CHECK( badResize->readinessReason.find( "to_input" ) != std::string::npos );
}

TEST_CASE( "manifest fields that the runtime does not execute are rejected", "[models][catalog][enforcement]" )
{
  QTemporaryDir dir;
  // #646: every manifest field must either be enforced at inference or fail
  // the manifest loudly at parse. Declared-but-identity behaviour is the
  // read-but-never-enforced class #632 closed for input.dtype.
  writeManifest( dir, QStringLiteral( "bad-normalize" ), R"({
      "name": "bad-normalize",
      "preprocess": { "normalize": "Mean_Std " }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "bad-nodata" ), R"({
      "name": "bad-nodata",
      "preprocess": { "nodata_policy": "nan" }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "bad-halo" ), R"({
      "name": "bad-halo",
      "tiling": { "tile_size": 256, "halo": 100000, "overlap": 8 }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "bad-halo-no-tile" ), R"({
      "name": "bad-halo-no-tile",
      "tiling": { "halo": 4 }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "bad-postprocess" ), R"({
      "name": "bad-postprocess",
      "postprocess": { "polygonize": true, "nms": true, "simplify": 2.0 }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "bad-threshold" ), R"({
      "name": "bad-threshold",
      "output": { "type": "mask", "threshold": 0.5 }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "good-tiling" ), R"({
      "name": "good-tiling",
      "tiling": { "tile_size": 256, "halo": 64, "overlap": 32, "batch_size": 4 },
      "preprocess": { "normalize": "linear", "scale": 0.00387, "nodata_policy": "zero" }
  })", QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  struct Expectation {
    const char *name;
    const char *reasonFragment;
  };
  for ( const auto &e : {
           Expectation{ "bad-normalize", "normalize" },
           Expectation{ "bad-nodata", "nodata_policy" },
           Expectation{ "bad-halo", "halo" },
           Expectation{ "bad-halo-no-tile", "tile_size" },
           Expectation{ "bad-postprocess", "polygonize" },
           Expectation{ "bad-threshold", "output.threshold" },
       } )
  {
    const auto model = catalog.find( e.name );
    INFO( "manifest: " << e.name );
    REQUIRE( model.has_value() );
    CHECK( model->readiness == ModelReadiness::InvalidManifest );
    CHECK( model->readinessReason.find( e.reasonFragment ) != std::string::npos );
  }

  // The supported surface (bounded tiling, linear scale, zero nodata) stays
  // loadable: validation must not over-reject.
  const auto good = catalog.find( "good-tiling" );
  REQUIRE( good.has_value() );
  CHECK( good->readiness != ModelReadiness::InvalidManifest );
}

TEST_CASE( "rankModels honors band roles and readiness", "[models][catalog][ranking]" )
{
  QTemporaryDir dir;
  // RGB model, artifact present → ready.
  writeManifest( dir, QStringLiteral( "rgb-model" ), R"({
      "name": "rgb-model",
      "task": "segmentation",
      "input": { "band_roles": ["Red", "Green", "Blue"] },
      "path": "weights.onnx"
  })",
                 QByteArray( "rgb" ) );
  // Same task, wrong bands (SAR), artifact present.
  writeManifest( dir, QStringLiteral( "sar-model" ), R"({
      "name": "sar-model",
      "task": "segmentation",
      "input": { "band_roles": ["VV", "VH"] },
      "path": "weights.onnx"
  })",
                 QByteArray( "sar" ) );
  // No declared roles, artifact present.
  writeManifest( dir, QStringLiteral( "agnostic" ), R"({
      "name": "agnostic",
      "task": "segmentation",
      "path": "weights.onnx"
  })",
                 QByteArray( "agn" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  sicnu::operators::ModelQueryCriteria criteria;
  criteria.task = "segmentation";
  criteria.bandRoles = { "Red", "Green", "Blue" };

  const auto ranked = catalog.rankModels( criteria );
  REQUIRE( ranked.size() == 3 );

  // RGB model ranks first and is compatible.
  CHECK( ranked[0].model.name == "rgb-model" );
  CHECK( ranked[0].compatible );
  bool sawBandMatch = false;
  for ( const auto &r : ranked[0].matchReasons )
    sawBandMatch = sawBandMatch || r.find( "Band roles" ) != std::string::npos;
  CHECK( sawBandMatch );

  // The other two: agnostic (roles unspecified) still compatible and ranked
  // below the exact match; SAR model incompatible.
  const sicnu::operators::ModelCandidate *sar = nullptr;
  const sicnu::operators::ModelCandidate *agn = nullptr;
  for ( const auto &c : ranked )
  {
    if ( c.model.name == "sar-model" )
      sar = &c;
    if ( c.model.name == "agnostic" )
      agn = &c;
  }
  REQUIRE( sar );
  REQUIRE( agn );
  CHECK_FALSE( sar->compatible );
  bool sawBandReject = false;
  for ( const auto &r : sar->incompatibilityReasons )
    sawBandReject = sawBandReject || r.find( "band roles" ) != std::string::npos;
  CHECK( sawBandReject );
  CHECK( agn->compatible );
}

TEST_CASE( "model listing exposes band roles and GPU availability for ranking", "[models][catalog][ranking]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "listed" ), R"({
      "name": "listed",
      "task": "segmentation",
      "gpu": true,
      "input": { "band_roles": ["Red", "Green", "Blue", "NIR"] },
      "path": "weights.onnx"
  })",
                 QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  // The spatial:list_models payload (ModelInfo::toJson) must carry the fields
  // the query side ranks on: band_roles for role scoring and gpu /
  // runtime.gpu for hardware-aware ordering (#705).
  const auto model = catalog.find( "listed" );
  REQUIRE( model.has_value() );
  const Json::Value json = model->toJson();
  REQUIRE( json.isMember( "band_roles" ) );
  REQUIRE( json["band_roles"].size() == 4 );
  CHECK( json["band_roles"][0].asString() == "Red" );
  CHECK( json["band_roles"][3].asString() == "NIR" );
  CHECK( json["gpu"].asBool() );
  REQUIRE( json.isMember( "runtime" ) );
  CHECK( json["runtime"]["gpu"].asBool() );

  // Query side: ranking by a subset of the declared roles stays compatible.
  sicnu::operators::ModelQueryCriteria criteria;
  criteria.bandRoles = { "Red", "NIR" };
  const auto ranked = catalog.rankModels( criteria );
  REQUIRE( ranked.size() == 1 );
  CHECK( ranked[0].compatible );
}

TEST_CASE( "resolveArtifactPath resolves files, names, and explains failures", "[models][catalog]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "ready-model" ), R"({
      "name": "ready-model",
      "task": "segmentation",
      "path": "weights.onnx"
  })",
                 QByteArray( "ready" ) );
  writeManifest( dir, QStringLiteral( "template-model" ), R"({
      "name": "template-model",
      "task": "segmentation",
      "path": ""
  })" );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  // Direct file reference.
  const QString artifactPath = dir.filePath( QStringLiteral( "ready-model/weights.onnx" ) );
  const auto direct = ModelCatalog::resolveArtifactPath( artifactPath.toStdString() );
  REQUIRE( direct.has_value() );
  CHECK( QFileInfo( QString::fromStdString( *direct ) ).isAbsolute() );

  // Catalog name, ready.
  const auto named = ModelCatalog::resolveArtifactPath( "ready-model" );
  REQUIRE( named.has_value() );
  CHECK( QFile::exists( QString::fromStdString( *named ) ) );

  // Catalog name, template → error explains the state.
  std::string error;
  const auto tmpl = ModelCatalog::resolveArtifactPath( "template-model", &error );
  CHECK_FALSE( tmpl.has_value() );
  CHECK( error.find( "not ready" ) != std::string::npos );
  CHECK( error.find( "missing_artifact" ) != std::string::npos );

  // Unknown reference.
  const auto unknown = ModelCatalog::resolveArtifactPath( "no-such-model", &error );
  CHECK_FALSE( unknown.has_value() );
  CHECK( error.find( "neither an existing file nor a catalog name" ) != std::string::npos );
}

TEST_CASE( "v2 manifests mirror their input contract into the v3 inputs list", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "v2-mirror" ), R"({
      "name": "v2-mirror",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx" },
      "input": {
          "data_type": "raster",
          "band_roles": ["Red", "Green", "Blue"],
          "dtype": "float32",
          "layout": "NCHW",
          "width": 256,
          "height": 256
      }
  })", QByteArray( "v2" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "v2-mirror" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::Ready );
  CHECK( model->readinessReason.empty() );
  REQUIRE( model->inputs.size() == 1 );
  // The legacy single-input member and the v3 list stay in sync, field by field.
  CHECK( model->inputs[0].dataType == model->input.dataType );
  CHECK( model->inputs[0].dtype == model->input.dtype );
  CHECK( model->inputs[0].layout == model->input.layout );
  CHECK( model->inputs[0].bandRoles == model->input.bandRoles );
  CHECK( model->inputs[0].width == model->input.width );
  CHECK( model->inputs[0].height == model->input.height );
  CHECK( model->inputs[0].name.empty() );  // single input: default (unnamed)
  CHECK( model->inputs[0].temporalLength == 0 );
  CHECK( model->inputs[0].temporalCollapse == "channels" );
  CHECK( model->supportedBandRoles.size() == 3 );
}

TEST_CASE( "v3 multi-input manifests parse every declared input in order", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "bitemporal" ), R"({
      "name": "bitemporal",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx" },
      "inputs": [
          {
              "name": "before",
              "data_type": "raster",
              "band_roles": ["Red", "Green", "Blue", "NIR"],
              "dtype": "float32",
              "temporal_length": 4,
              "temporal_collapse": "channels"
          },
          {
              "name": "after",
              "data_type": "raster",
              "band_roles": ["Red", "Green", "Blue", "NIR"],
              "dtype": "float32"
          }
      ]
  })", QByteArray( "v3" ) );
  // Precedence: when both `inputs` and `input` exist, `inputs` wins and the
  // legacy mirror is filled from inputs[0] — the `input` section is ignored.
  writeManifest( dir, QStringLiteral( "both-keys" ), R"({
      "name": "both-keys",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx" },
      "input": {
          "band_roles": ["Blue"],
          "width": 999,
          "height": 999
      },
      "inputs": [
          { "name": "before", "band_roles": ["Red", "Green"], "width": 256, "height": 256 },
          { "name": "after", "band_roles": ["Red", "Green"] }
      ]
  })", QByteArray( "v3" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "bitemporal" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::Ready );
  CHECK( model->readinessReason.empty() );
  REQUIRE( model->inputs.size() == 2 );
  CHECK( model->inputs[0].name == "before" );
  CHECK( model->inputs[0].dtype == "float32" );
  CHECK( model->inputs[0].temporalLength == 4 );
  CHECK( model->inputs[0].temporalCollapse == "channels" );
  REQUIRE( model->inputs[0].bandRoles.size() == 4 );
  CHECK( model->inputs[1].name == "after" );
  CHECK( model->inputs[1].temporalLength == 0 );
  CHECK( model->inputs[1].temporalCollapse == "channels" );
  // Legacy mirror: always inputs[0].
  CHECK( model->input.name == "before" );
  CHECK( model->input.bandRoles == model->inputs[0].bandRoles );
  CHECK( model->supportedBandRoles.size() == 4 );

  const auto both = catalog.find( "both-keys" );
  REQUIRE( both.has_value() );
  CHECK( both->readiness == ModelReadiness::Ready );
  REQUIRE( both->inputs.size() == 2 );
  CHECK( both->inputs[0].name == "before" );
  CHECK( both->inputs[0].width == 256 );
  // `inputs` wins: the legacy `input` section (Blue, 999) is ignored.
  CHECK( both->input.name == "before" );
  CHECK( both->input.width == 256 );
  CHECK( both->supportedBandRoles.size() == 2 );
}

TEST_CASE( "multi-input manifests need a unique name per input", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "unnamed-second" ), R"({
      "name": "unnamed-second",
      "framework": "onnx",
      "inputs": [
          { "name": "before", "band_roles": ["Red"] },
          { "band_roles": ["Green"] }
      ]
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "duplicate-names" ), R"({
      "name": "duplicate-names",
      "framework": "onnx",
      "inputs": [
          { "name": "band", "band_roles": ["Red"] },
          { "name": "band", "band_roles": ["Green"] }
      ]
  })", QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto unnamed = catalog.find( "unnamed-second" );
  REQUIRE( unnamed.has_value() );
  CHECK( unnamed->readiness == ModelReadiness::InvalidManifest );
  CHECK( unnamed->readinessReason.find( "name" ) != std::string::npos );

  const auto duplicated = catalog.find( "duplicate-names" );
  REQUIRE( duplicated.has_value() );
  CHECK( duplicated->readiness == ModelReadiness::InvalidManifest );
  CHECK( duplicated->readinessReason.find( "name" ) != std::string::npos );
}

TEST_CASE( "per-input temporal contracts parse or fail loudly", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "temporal-ok" ), R"({
      "name": "temporal-ok",
      "framework": "onnx",
      "path": "weights.onnx",
      "input": { "band_roles": ["Red", "Green", "Blue"], "temporal_length": 4 }
  })", QByteArray( "fake-weights" ) );
  writeManifest( dir, QStringLiteral( "temporal-negative" ), R"({
      "name": "temporal-negative",
      "framework": "onnx",
      "input": { "temporal_length": -1 }
  })", QByteArray( "w" ) );
  writeManifest( dir, QStringLiteral( "collapse-bogus" ), R"({
      "name": "collapse-bogus",
      "framework": "onnx",
      "input": { "temporal_collapse": "bogus" }
  })", QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto ok = catalog.find( "temporal-ok" );
  REQUIRE( ok.has_value() );
  CHECK( ok->readiness == ModelReadiness::Ready );
  REQUIRE( ok->inputs.size() == 1 );
  CHECK( ok->inputs[0].temporalLength == 4 );
  CHECK( ok->inputs[0].temporalCollapse == "channels" );
  CHECK( ok->input.temporalLength == 4 );

  const auto negative = catalog.find( "temporal-negative" );
  REQUIRE( negative.has_value() );
  CHECK( negative->readiness == ModelReadiness::InvalidManifest );
  CHECK( negative->readinessReason.find( "temporal_length" ) != std::string::npos );

  const auto collapse = catalog.find( "collapse-bogus" );
  REQUIRE( collapse.has_value() );
  CHECK( collapse->readiness == ModelReadiness::InvalidManifest );
  CHECK( collapse->readinessReason.find( "temporal_collapse" ) != std::string::npos );
}

TEST_CASE( "output.uncertainty parses or is rejected", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "uncertainty-entropy" ), R"({
      "name": "uncertainty-entropy",
      "framework": "onnx",
      "path": "weights.onnx",
      "output": { "type": "raster", "uncertainty": "entropy" }
  })", QByteArray( "fake-weights" ) );
  writeManifest( dir, QStringLiteral( "uncertainty-margin" ), R"({
      "name": "uncertainty-margin",
      "framework": "onnx",
      "path": "weights.onnx",
      "output": { "type": "raster", "uncertainty": "margin" }
  })", QByteArray( "fake-weights" ) );
  writeManifest( dir, QStringLiteral( "uncertainty-bogus" ), R"({
      "name": "uncertainty-bogus",
      "framework": "onnx",
      "output": { "type": "raster", "uncertainty": "bogus" }
  })", QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto entropy = catalog.find( "uncertainty-entropy" );
  REQUIRE( entropy.has_value() );
  CHECK( entropy->readiness == ModelReadiness::Ready );
  CHECK( entropy->output.uncertainty == "entropy" );

  const auto margin = catalog.find( "uncertainty-margin" );
  REQUIRE( margin.has_value() );
  CHECK( margin->readiness == ModelReadiness::Ready );
  CHECK( margin->output.uncertainty == "margin" );

  const auto bogus = catalog.find( "uncertainty-bogus" );
  REQUIRE( bogus.has_value() );
  CHECK( bogus->readiness == ModelReadiness::InvalidManifest );
  CHECK( bogus->readinessReason.find( "uncertainty" ) != std::string::npos );
}

TEST_CASE( "toJson serializes the v3 inputs surface for re-parse", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  const QByteArray artifactBytes = "v3-weights";
  writeManifest( dir, QStringLiteral( "v3-rt" ), R"({
      "name": "v3-rt",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "weights.onnx" },
      "inputs": [
          {
              "name": "before",
              "data_type": "raster",
              "band_roles": ["Red", "Green", "Blue", "NIR"],
              "dtype": "float32",
              "width": 256,
              "height": 256,
              "temporal_length": 4
          },
          {
              "name": "after",
              "data_type": "raster",
              "band_roles": ["Red", "Green", "Blue", "NIR"]
          }
      ],
      "preprocess": {
          "normalize": "mean_std",
          "mean": [0.1, 0.2, 0.3, 0.4],
          "std": [0.1, 0.1, 0.1, 0.1]
      },
      "output": { "type": "raster", "uncertainty": "entropy" }
  })", artifactBytes );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  const auto model = catalog.find( "v3-rt" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::Ready );

  const Json::Value json = model->toJson();
  REQUIRE( json.isMember( "inputs" ) );
  REQUIRE( json["inputs"].size() == 2 );
  CHECK( json["inputs"][0]["name"].asString() == "before" );
  CHECK( json["inputs"][0]["temporal_length"].asInt() == 4 );
  CHECK( json["inputs"][0]["width"].asInt() == 256 );
  CHECK( json["inputs"][1]["name"].asString() == "after" );
  CHECK( json["inputs"][1]["band_roles"].size() == 4 );
  // Legacy flat mirror stays for v2 consumers.
  CHECK( json.isMember( "input_contract" ) );
  CHECK( json["input_contract"]["band_roles"].size() == 4 );
  REQUIRE( json.isMember( "output_contract" ) );
  CHECK( json["output_contract"]["uncertainty"].asString() == "entropy" );

  // Round trip: the serialized surface is itself a valid manifest — feed it
  // back through the catalog and the v3 fields survive. The echo needs its
  // own unique name (the catalog refuses duplicate ids).
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  Json::Value echoJson = json;
  echoJson["name"] = "v3-rt-echo";
  echoJson["artifact"]["path"] = "weights.onnx";
  const std::string serialized = Json::writeString( builder, echoJson );
  writeManifest( dir, QStringLiteral( "v3-rt-echo" ), QByteArray( serialized.c_str() ), artifactBytes );
  catalog.reload();
  const auto echoed = catalog.find( "v3-rt-echo" );
  REQUIRE( echoed.has_value() );
  CHECK( echoed->readiness == ModelReadiness::Ready );
  REQUIRE( echoed->inputs.size() == 2 );
  CHECK( echoed->inputs[0].name == "before" );
  CHECK( echoed->inputs[0].temporalLength == 4 );
  CHECK( echoed->inputs[0].width == 256 );
  CHECK( echoed->inputs[1].name == "after" );
  CHECK( echoed->input.name == "before" );
  CHECK( echoed->input.bandRoles == echoed->inputs[0].bandRoles );
}

TEST_CASE( "a non-array inputs key is rejected safely", "[models][catalog][v3]" )
{
  QTemporaryDir dir;
  writeManifest( dir, QStringLiteral( "inputs-object" ), R"({
      "name": "inputs-object",
      "framework": "onnx",
      "inputs": { "name": "before" }
  })", QByteArray( "w" ) );

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );

  // Wrong-typed `inputs` must invalidate the manifest, not crash the scan.
  const auto model = catalog.find( "inputs-object" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::InvalidManifest );
  CHECK( model->readinessReason.find( "inputs" ) != std::string::npos );
  CHECK( model->inputs.empty() );
}
