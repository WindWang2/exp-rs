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
