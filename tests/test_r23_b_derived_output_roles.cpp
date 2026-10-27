// tests/test_r23_b_derived_output_roles.cpp — R23-B: derived output.format
// must respect the typed head contract (risk B5 residual, recorded by the
// r21-B track as "output.format 与 head role 无关联校验").
//
// The derived collapse (labels=argmax, mask=threshold, confidence=top-1)
// assumes head 0 produces [0,1] class-probability planes. A typed head
// declaring role embedding/detection/uncertainty/auxiliary has no class
// planes to collapse, and "distance"/"logit" confidence semantics are not
// probabilities — collapsing them fabricates a product the manifest never
// claimed. The gate refuses BEFORE any compute (before the input raster is
// even opened); legacy flat manifests (no output.heads[]) stay permissive.
//
// Oracle trick: the refusals fire in preflight, before the input raster is
// opened — so feeding a NONEXISTENT input path discriminates exactly:
// gated manifest  → RSOperatorError "declares role/confidence …"
// ungated manifest→ RSOperatorError "failed to open input raster"
// On master (no gate) every case below lands in the second branch.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "support/onnx_fixture_builder.h"

#include <QTemporaryDir>

#include <filesystem>
#include <string>

namespace
{

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;

using namespace onnxfixture;

// Single Identity head "seg" — the minimal model every case acquires.
std::filesystem::path writeSegModel( const QTemporaryDir &dir )
{
  const std::filesystem::path artifact =
    std::filesystem::path( dir.path().toStdString() ) / "seg.onnx";
  writeModel( artifact,
              buildModel( { node( "Identity", { "x" }, { "seg" } ) },
                          {},
                          { valueInfo( "x", Float, { fixed( 1 ), fixed( 1 ), fixed( 8 ), fixed( 8 ) } ) },
                          { valueInfo( "seg", Float, { fixed( 1 ), fixed( 1 ), fixed( 8 ), fixed( 8 ) } ) },
                          "seg_fixture" ) );
  return artifact;
}

sicnu::operators::ModelHeadContract head( const std::string &role,
                                          const std::string &confidence )
{
  sicnu::operators::ModelHeadContract h;
  h.name = "seg";
  h.role = role;
  h.confidence = confidence;
  return h;
}

// Runs the engine against a deliberately nonexistent input. Returns the
// RSOperatorError message (preflight refusals name the head contract; the
// un-gated path surfaces the raster-open failure instead).
std::string runExpectingRefusal( const ModelInfo &info )
{
  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  std::string acquireError;
  const auto session = registry.acquire( info, RequestedDevice::cpu(), &acquireError );
  REQUIRE( session );
  REQUIRE( acquireError.empty() );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  try
  {
    (void )engine.run( "/nonexistent/r23b/missing-input.tif", {}, "/nonexistent/r23b/out.tif",
                       context );
  }
  catch ( const RSOperatorError &e )
  {
    registry.releaseAll();
    return e.message();
  }
  registry.releaseAll();
  FAIL( "engine.run did not refuse the nonexistent-input request" );
  return {};
}

ModelInfo baseInfo( const std::filesystem::path &artifact )
{
  ModelInfo info;
  info.name = "seg-only";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = artifact.string();
  info.tiling.tileSize = 8;
  info.tiling.overlap = 0;
  info.output.tensorNames = { "seg" };
  info.output.format = "labels";
  return info;
}

} // namespace

TEST_CASE( "derived output.format refuses non-class head roles",
           "[models][runtime][r23b]" )
{
  QTemporaryDir dir;
  const auto artifact = writeSegModel( dir );

  // embedding head collapsed by argmax would fabricate class labels.
  ModelInfo embedding = baseInfo( artifact );
  embedding.output.heads = { head( "embedding", "probability" ) };
  CHECK_THAT( runExpectingRefusal( embedding ),
              Catch::Matchers::ContainsSubstring( "declares role 'embedding'" ) );

  // detection heads are decoded by the detection lane, not the collapse.
  ModelInfo detection = baseInfo( artifact );
  detection.output.heads = { head( "detection", "probability" ) };
  detection.output.format = "mask";
  CHECK_THAT( runExpectingRefusal( detection ),
              Catch::Matchers::ContainsSubstring( "declares role 'detection'" ) );

  // uncertainty / auxiliary likewise have no class planes.
  ModelInfo uncertainty = baseInfo( artifact );
  uncertainty.output.heads = { head( "uncertainty", "probability" ) };
  CHECK_THAT( runExpectingRefusal( uncertainty ),
              Catch::Matchers::ContainsSubstring( "declares role 'uncertainty'" ) );
}

TEST_CASE( "derived output.format refuses non-probability confidence semantics",
           "[models][runtime][r23b]" )
{
  QTemporaryDir dir;
  const auto artifact = writeSegModel( dir );

  // distance (smaller = closer) inverts argmax outright.
  ModelInfo distance = baseInfo( artifact );
  distance.output.heads = { head( "segmentation", "distance" ) };
  CHECK_THAT( runExpectingRefusal( distance ),
              Catch::Matchers::ContainsSubstring( "declares confidence 'distance'" ) );

  // logit + mask: thresholding a logit at a probability threshold is
  // meaningless.
  ModelInfo logitMask = baseInfo( artifact );
  logitMask.output.heads = { head( "segmentation", "logit" ) };
  logitMask.output.format = "mask";
  CHECK_THAT( runExpectingRefusal( logitMask ),
              Catch::Matchers::ContainsSubstring( "declares confidence 'logit'" ) );

  // logit + confidence (top-1 band): the band claims a probability value.
  ModelInfo logitConf = baseInfo( artifact );
  logitConf.output.heads = { head( "segmentation", "logit" ) };
  logitConf.output.format = "confidence";
  CHECK_THAT( runExpectingRefusal( logitConf ),
              Catch::Matchers::ContainsSubstring( "declares confidence 'logit'" ) );
}

TEST_CASE( "derived output.format stays permissive for legacy and logit-labels",
           "[models][runtime][r23b]" )
{
  QTemporaryDir dir;
  const auto artifact = writeSegModel( dir );

  // Legacy flat manifest (no output.heads[]): historical behavior — the
  // gate must not fire; the run proceeds to raster open and fails there.
  ModelInfo legacy = baseInfo( artifact );
  CHECK_THAT( runExpectingRefusal( legacy ),
              Catch::Matchers::ContainsSubstring( "failed to open input raster" ) );

  // logit + labels: argmax is order-preserving on logits — allowed; the
  // refusal that surfaces is the raster-open failure, not the gate.
  ModelInfo logitLabels = baseInfo( artifact );
  logitLabels.output.heads = { head( "segmentation", "logit" ) };
  CHECK_THAT( runExpectingRefusal( logitLabels ),
              Catch::Matchers::ContainsSubstring( "failed to open input raster" ) );

  // segmentation + probability + labels: the fully legal combination.
  ModelInfo legal = baseInfo( artifact );
  legal.output.heads = { head( "segmentation", "probability" ) };
  CHECK_THAT( runExpectingRefusal( legal ),
              Catch::Matchers::ContainsSubstring( "failed to open input raster" ) );
}
