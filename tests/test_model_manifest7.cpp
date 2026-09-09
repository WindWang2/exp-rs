// tests/test_model_manifest7.cpp — Platform 7.0 manifest surface: multimodal
// input contracts (modality / alignment / missing_timestep), typed heads,
// preprocess clamp+pad, valid-coverage gate, external provider contracts,
// manifest_version 5 and the closed-vocabulary unknown-key refusal.
// Catch2; QtCore only (the catalog is backend-agnostic).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <optional>
#include <string>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;

/// Registers one manifest document programmatically. Registration replaces
/// same-name entries, so tests may reuse names freely.
ModelInfo parseOk( const std::string &json )
{
  auto &catalog = ModelCatalog::instance();
  std::string error;
  REQUIRE( catalog.registerManifestJson( json, "test://manifest7", &error ) );
  // Extract the name the way the catalog did (JSON is an object with "name").
  const std::string name = [&json] {
    const std::size_t pos = json.find( "\"name\"" );
    const std::size_t valueStart = json.find( '"', pos + 6 ) + 1;
    return json.substr( valueStart, json.find( '"', valueStart ) - valueStart );
  }();
  auto found = catalog.find( name );
  REQUIRE( found.has_value() );
  return *found;
}

/// The manifest must be REFUSED with an InvalidManifest reason containing
/// @a needle — through the same pure validation the scan applies.
void expectRefusal( const std::string &json, const std::string &needle )
{
  auto &catalog = ModelCatalog::instance();
  const auto issues = catalog.validateManifestJson( json );
  bool found = false;
  for ( const auto &issue : issues )
    found = found || issue.find( needle ) != std::string::npos;
  if ( !found )
  {
    std::string all;
    for ( const auto &issue : issues )
      all += "\n  - " + issue;
    FAIL( "expected refusal containing '" << needle << "' but validateManifestJson returned:" << all );
  }
}

const char *kBase = R"({
    "name": "m7-base",
    "task": "segmentation",
    "framework": "onnx",
    "inputs": [ { "name": "optical", "band_roles": ["B", "G", "R", "NIR"] } ]
})";

} // namespace

TEST_CASE( "7.0 multimodal input contract parses with documented defaults", "[models][manifest7]" )
{
  const auto model = parseOk( kBase );
  REQUIRE( model.inputs.size() == 1 );
  CHECK( model.inputs[0].modality.empty() );        // "" = optical semantics
  CHECK( model.inputs[0].alignment.empty() );       // "" = none
  CHECK( model.inputs[0].missingTimestep.empty() ); // "" = refuse
}

TEST_CASE( "7.0 multimodal input vocabulary round-trips", "[models][manifest7]" )
{
  const auto model = parseOk( R"({
      "name": "m7-mm",
      "task": "change_detection",
      "framework": "onnx",
      "manifest_version": 5,
      "inputs": [
        { "name": "optical", "modality": "optical", "band_roles": ["B","G","R","NIR"] },
        { "name": "sar", "modality": "sar" },
        { "name": "dem", "modality": "dem", "alignment": "reference" },
        { "name": "cloudmask", "modality": "mask", "alignment": "reference" },
        { "name": "indices", "modality": "aux" },
        { "name": "stack", "modality": "optical", "temporal_length": 6,
          "missing_timestep": "zero", "layout": "NCHW" }
      ]
  })" );
  REQUIRE( model.inputs.size() == 6 );
  CHECK( model.manifestVersion == 5 );
  CHECK( model.inputs[0].modality == "optical" );
  CHECK( model.inputs[2].modality == "dem" );
  CHECK( model.inputs[2].alignment == "reference" );
  CHECK( model.inputs[3].modality == "mask" );
  CHECK( model.inputs[4].modality == "aux" );
  CHECK( model.inputs[5].temporalLength == 6 );
  CHECK( model.inputs[5].missingTimestep == "zero" );
}

TEST_CASE( "7.0 input vocabulary refusals are typed", "[models][manifest7]" )
{
  expectRefusal( R"({
      "name": "m7-badmod", "task": "t", "framework": "onnx",
      "inputs": [ { "name": "x", "modality": "lidar" } ] })",
                 "modality 'lidar' is unsupported" );
  expectRefusal( R"({
      "name": "m7-badalign", "task": "t", "framework": "onnx",
      "inputs": [ { "name": "x", "alignment": "warp_to_reference" } ] })",
                 "alignment 'warp_to_reference' is unsupported" );
  expectRefusal( R"({
      "name": "m7-badmissing", "task": "t", "framework": "onnx",
      "inputs": [ { "name": "x", "missing_timestep": "interpolate" } ] })",
                 "missing_timestep 'interpolate' is unsupported" );
  expectRefusal( R"({
      "name": "m7-orphanpolicy", "task": "t", "framework": "onnx",
      "inputs": [ { "name": "x", "missing_timestep": "zero" } ] })",
                 "missing_timestep is declared but temporal_length is 0" );
}

TEST_CASE( "7.0 typed heads parse with documented defaults", "[models][manifest7]" )
{
  const auto model = parseOk( R"({
      "name": "m7-heads",
      "task": "segmentation",
      "framework": "onnx",
      "output": {
        "type": "raster",
        "tensor_names": ["logits"],
        "heads": [
          { "name": "logits", "role": "segmentation", "classes": ["bg", "road"] },
          { "name": "embed", "role": "embedding", "dtype": "float32",
            "confidence": "distance" },
          { "name": "uncert", "role": "uncertainty" }
        ]
      }
  })" );
  REQUIRE( model.output.headsDeclared );
  REQUIRE( model.output.heads.size() == 3 );
  CHECK( model.output.heads[0].role == "segmentation" );
  CHECK( model.output.heads[0].confidence == "probability" );
  CHECK( model.output.heads[1].confidence == "distance" );
  CHECK( model.output.heads[2].role == "uncertainty" );
}

TEST_CASE( "7.0 typed heads refusals are typed", "[models][manifest7]" )
{
  expectRefusal( R"({
      "name": "m7-badrole", "task": "t", "framework": "onnx",
      "output": { "heads": [ { "name": "h", "role": "depth" } ] } })",
                 "role 'depth' is unsupported" );
  expectRefusal( R"({
      "name": "m7-badconf", "task": "t", "framework": "onnx",
      "output": { "heads": [ { "name": "h", "role": "embedding", "confidence": "percent" } ] } })",
                 "confidence 'percent' is unsupported" );
  expectRefusal( R"({
      "name": "m7-badlayout", "task": "t", "framework": "onnx",
      "output": { "heads": [ { "name": "h", "role": "auxiliary", "layout": "NHWC" } ] } })",
                 "layout 'NHWC' is unsupported" );
  expectRefusal( R"({
      "name": "m7-emptyheads", "task": "t", "framework": "onnx",
      "output": { "heads": [] } })",
                 "declared but empty" );
  expectRefusal( R"({
      "name": "m7-dupheads", "task": "t", "framework": "onnx",
      "output": { "heads": [ { "name": "h", "role": "auxiliary" },
                             { "name": "h", "role": "embedding" } ] } })",
                 "duplicate name 'h'" );
  expectRefusal( R"({
      "name": "m7-detnocontract", "task": "t", "framework": "onnx",
      "output": { "heads": [ { "name": "h", "role": "detection" } ] } })",
                 "requires the output.detection decode contract" );
  expectRefusal( R"({
      "name": "m7-contradiction", "task": "t", "framework": "onnx",
      "output": { "tensor_names": ["seg"],
                   "heads": [ { "name": "mask", "role": "segmentation" } ] } })",
                 "contradicts output.heads[0].name" );
}

TEST_CASE( "7.0 preprocess clamp/pad and coverage gate parse + validate", "[models][manifest7]" )
{
  const auto model = parseOk( R"({
      "name": "m7-prep", "task": "segmentation", "framework": "onnx",
      "preprocess": { "normalize": "linear", "scale": 0.0001,
                       "clamp_min": 0.0, "clamp_max": 1.5, "pad": 8 },
      "tiling": { "tile_size": 256, "min_valid_coverage": 0.35 }
  })" );
  CHECK( model.preprocess.clampMin == Catch::Approx( 0.0 ) );
  CHECK( model.preprocess.clampMax == Catch::Approx( 1.5 ) );
  CHECK( model.preprocess.pad == 8 );
  CHECK( model.tiling.minValidCoverage == Catch::Approx( 0.35 ) );

  expectRefusal( R"({
      "name": "m7-badclamp", "task": "t", "framework": "onnx",
      "preprocess": { "clamp_min": 2.0, "clamp_max": 1.0 } })",
                 "clamp_min must be < clamp_max" );
  expectRefusal( R"({
      "name": "m7-badcov", "task": "t", "framework": "onnx",
      "tiling": { "min_valid_coverage": 1.5 } })",
                 "min_valid_coverage must be in [0, 1]" );
}

TEST_CASE( "7.0 runtime.provider parses and is framework-checked", "[models][manifest7]" )
{
  const auto httpModel = parseOk( R"({
      "name": "m7-remote", "task": "segmentation", "framework": "http",
      "runtime": { "provider": { "url": "http://127.0.0.1:9/infer",
                                  "timeout_ms": 5000, "max_body_mb": 64 } }
  })" );
  CHECK( httpModel.runtime.provider.url == "http://127.0.0.1:9/infer" );
  CHECK( httpModel.runtime.provider.timeoutMs == 5000 );

  const auto pyModel = parseOk( R"({
      "name": "m7-pyworker", "task": "segmentation", "framework": "python",
      "runtime": { "provider": { "worker_script": "workers/unet.py" } }
  })" );
  CHECK( pyModel.runtime.provider.workerScript == "workers/unet.py" );

  expectRefusal( R"({
      "name": "m7-nourl", "task": "t", "framework": "http",
      "runtime": { "provider": { "timeout_ms": 1 } } })",
                 "framework 'http' requires runtime.provider.url" );
  expectRefusal( R"({
      "name": "m7-noscript", "task": "t", "framework": "python",
      "runtime": { "provider": { } } })",
                 "framework 'python' requires runtime.provider.worker_script" );
  expectRefusal( R"({
      "name": "m7-wrongfw", "task": "t", "framework": "onnx",
      "runtime": { "provider": { "url": "http://127.0.0.1:9" } } })",
                 "provider connections apply to the 'http' and 'python' frameworks" );
}

TEST_CASE( "7.0 unknown keys are refused with the section path", "[models][manifest7]" )
{
  expectRefusal( R"({ "name": "m7-u1", "task": "t", "framework": "onnx", "temperal_length": 3 })",
                 "unknown key 'temperal_length'" );
  expectRefusal( R"({
      "name": "m7-u2", "task": "t", "framework": "onnx",
      "inputs": [ { "name": "x", "band_role": ["R"] } ] })",
                 "unknown key 'inputs[0].band_role'" );
  expectRefusal( R"({
      "name": "m7-u3", "task": "t", "framework": "onnx",
      "preprocess": { "Normalize": "linear" } })",
                 "unknown key 'preprocess.Normalize'" );
  expectRefusal( R"({
      "name": "m7-u4", "task": "t", "framework": "onnx",
      "output": { "heads": [ { "name": "h", "role": "auxiliary", "scale": 2 } ] } })",
                 "unknown key 'output.heads[0].scale'" );
  expectRefusal( R"({
      "name": "m7-u5", "task": "t", "framework": "http",
      "runtime": { "provider": { "url": "http://x", "retry_count": 4 } } })",
                 "unknown key 'runtime.provider.retry_count'" );
  expectRefusal( R"({
      "name": "m7-u6", "task": "t", "framework": "onnx",
      "tiling": { "tile_size": 256, "halo": 16, "overlap": 8, "batch": 2 } })",
                 "unknown key 'tiling.batch'" );
  expectRefusal( R"({
      "name": "m7-u7", "task": "t", "framework": "onnx",
      "artifact": { "path": "w.onnx", "shasum": "abc" } })",
                 "unknown key 'artifact.shasum'" );
}

TEST_CASE( "7.0 refused manifests stay indexed in a directory scan", "[models][manifest7]" )
{
  // The catalog never drops refused manifests: the entry is inspectable with
  // InvalidManifest readiness and the full reason (typed refusal, not silence).
  QTemporaryDir dir;
  const QDir modelDir( dir.path() );
  REQUIRE( modelDir.mkpath( QStringLiteral( "m7-scan" ) ) );
  QFile manifest( dir.filePath( QStringLiteral( "m7-scan/model.json" ) ) );
  REQUIRE( manifest.open( QIODevice::WriteOnly ) );
  manifest.write( R"({ "name": "m7-scan", "task": "t", "framework": "onnx",
                       "preprocess": { "unknown_knob": 1 } })" );
  manifest.close();

  auto &catalog = ModelCatalog::instance();
  catalog.setDirectory( dir.path().toStdString() );
  const auto model = catalog.find( "m7-scan" );
  REQUIRE( model.has_value() );
  CHECK( model->readiness == ModelReadiness::InvalidManifest );
  CHECK( model->readinessReason.find( "unknown key 'preprocess.unknown_knob'" )
         != std::string::npos );
  // A refused manifest is not executable, ever.
  const auto ranked = catalog.rankModels( {} );
  for ( const auto &candidate : ranked )
    if ( candidate.model.name == "m7-scan" )
      CHECK_FALSE( candidate.compatible );
}

TEST_CASE( "7.0 annotation keys (note / reference / x-*) stay legal", "[models][manifest7]" )
{
  const auto model = parseOk( R"({
      "name": "m7-annotated",
      "note": "documentation only",
      "reference": "https://example.invalid/model",
      "x-vendor-field": { "anything": true },
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "note": "download instructions", "path": "" },
      "inputs": [ { "name": "x", "note": "sentinel-2 L1C" } ],
      "runtime": { "x-hint": "edge" }
  })" );
  CHECK( model.manifestVersion == 0 );
  // Not refused: registration succeeded and the entry carries no contract error.
  CHECK( model.readiness != ModelReadiness::InvalidManifest );
}

TEST_CASE( "manifest_version 5 keeps the shape cross-check", "[models][manifest7]" )
{
  expectRefusal( R"({
      "name": "m7-v5flat", "task": "t", "framework": "onnx", "manifest_version": 5,
      "input": "raster" })",
                 "but the manifest shape is version 1" );
  expectRefusal( R"({
      "name": "m7-v9", "task": "t", "framework": "onnx", "manifest_version": 9 })",
                 "manifest_version 9 is unsupported (1..5)" );
}

TEST_CASE( "7.0 fields survive the toJson projection", "[models][manifest7]" )
{
  const auto model = parseOk( R"({
      "name": "m7-roundtrip", "task": "segmentation", "framework": "http",
      "manifest_version": 5,
      "inputs": [ { "name": "s1", "modality": "sar", "temporal_length": 4,
                     "missing_timestep": "zero" } ],
      "output": { "heads": [ { "name": "seg", "role": "segmentation",
                                "classes": ["bg","water"] } ] },
      "preprocess": { "clamp_min": -1.0, "clamp_max": 1.0, "pad": 4 },
      "tiling": { "min_valid_coverage": 0.5 },
      "runtime": { "provider": { "url": "http://worker:8080/infer" } }
  })" );
  const Json::Value projected = model.toJson();
  const Json::Value &inputs = projected["inputs"];
  REQUIRE( inputs.isArray() );
  CHECK( inputs[0]["modality"].asString() == "sar" );
  CHECK( inputs[0]["missing_timestep"].asString() == "zero" );
  const Json::Value &heads = projected["output_contract"]["heads"];
  REQUIRE( heads.isArray() );
  CHECK( heads[0]["role"].asString() == "segmentation" );
  CHECK( projected["preprocess"]["clamp_min"].asDouble() == Catch::Approx( -1.0 ) );
  CHECK( projected["preprocess"]["pad"].asInt() == 4 );
  CHECK( projected["tiling"]["min_valid_coverage"].asDouble() == Catch::Approx( 0.5 ) );
  CHECK( projected["runtime"]["provider"]["url"].asString() == "http://worker:8080/infer" );
}

TEST_CASE( "legacy manifests keep parsing cleanly (no 7.0 keys)", "[models][manifest7]" )
{
  const auto model = parseOk( R"({
      "name": "m7-legacy",
      "task": "segmentation",
      "input": { "dtype": "float32", "band_roles": ["R","G","B"] },
      "output": { "type": "raster", "classes": ["a","b","c"] },
      "framework": "onnx"
  })" );
  CHECK( model.manifestVersion == 0 );
  CHECK( model.readiness != ModelReadiness::InvalidManifest );
  CHECK_FALSE( model.output.headsDeclared );
  CHECK( model.output.heads.empty() );
  CHECK( std::isnan( model.preprocess.clampMin ) );
  CHECK( model.preprocess.pad == 0 );
  CHECK( model.tiling.minValidCoverage == 0.0 );
}

TEST_CASE( "validateManifestJson reports 7.0 refusals without registering", "[models][manifest7]" )
{
  auto &catalog = ModelCatalog::instance();
  const auto issues = catalog.validateManifestJson( R"({
      "name": "m7-val", "task": "t", "framework": "onnx",
      "inputs": [ { "name": "x", "modality": "pointcloud" } ] })" );
  REQUIRE_FALSE( issues.empty() );
  bool found = false;
  for ( const auto &issue : issues )
    found = found || issue.find( "modality 'pointcloud' is unsupported" ) != std::string::npos;
  CHECK( found );
  CHECK( !catalog.find( "m7-val" ).has_value() );
}
