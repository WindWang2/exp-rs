// test_r29_t01_typed_readiness.cpp — R29-T01 WP06: the runtime readiness
// verdict carries a TYPED refusal kind (RuntimeRefusalKind) plus per-chain
// candidate verdicts, so consumers classify failures without matching
// English fragments. Pins: every refusal family maps to its kind, the
// historically MIXED "no fitting CUDA device" spelling now separates
// GpuUnavailable (no hardware) from ResourceBudget (no room), the primary
// candidate's kind wins when a fallback chain fails everywhere, and the
// legacy string API keeps its exact trail wording.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_runtime.h"

#include <string>

namespace {

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;

struct HardwareGuard
{
    ModelHardwareCapabilities saved;

    HardwareGuard()
      : saved( ModelRuntimeRegistry::instance().hardware() )
    {
    }

    ~HardwareGuard() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

ModelInfo modelWithFramework( const std::string &framework )
{
  ModelInfo model;
  model.name = "r29-readiness";
  model.framework = framework;
  model.task = "segmentation";
  return model;
}

ModelHardwareCapabilities gpuHost()
{
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 128;
  return hw;
}

/// A registered provider whose sessions never load (the factory's outcome is
/// irrelevant to readiness — registration alone makes the framework
/// provider-present).
void registerLoudFramework( const std::string &framework )
{
  ModelRuntimeRegistry::instance().registerProvider(
    framework,
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string *error ) -> ModelRuntimePtr {
      if ( error )
        *error = "r29 test provider never loads";
      return nullptr;
    },
    ProviderTraits{} );
}

} // namespace

TEST_CASE( "R29-T01: typed readiness classifies every refusal family",
           "[models][r29t01][readiness]" )
{
  HardwareGuard guard;

  SECTION( "ready model reports none" )
  {
    registerLoudFramework( "r29-ready-fw" );
    const RuntimeReadinessVerdict verdict =
      evaluateRuntimeReadinessTyped( modelWithFramework( "r29-ready-fw" ), gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::Ready );
    CHECK( verdict.refusal == RuntimeRefusalKind::None );
    CHECK( verdict.reason.empty() );
    REQUIRE( verdict.candidates.size() == 1 );
    CHECK( verdict.candidates.front().framework == "r29-ready-fw" );
    CHECK( verdict.candidates.front().readiness == ModelReadiness::Ready );
  }

  SECTION( "missing provider" )
  {
    const RuntimeReadinessVerdict verdict =
      evaluateRuntimeReadinessTyped( modelWithFramework( "r29-no-such-fw" ), gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::UnsupportedRuntime );
    CHECK( verdict.refusal == RuntimeRefusalKind::ProviderMissing );
    CHECK_THAT( verdict.reason, Catch::Matchers::ContainsSubstring( "no runtime provider" ) );
  }

  SECTION( "gpu demanded, none available (legacy tokenless path)" )
  {
    ModelInfo model = modelWithFramework( "r29-ready-fw" );
    model.runtime.gpu = true;
    model.runtime.cpuFallback = false;
    ModelHardwareCapabilities hw;
    hw.cudaAvailable = false;
    registerLoudFramework( "r29-ready-fw" );
    const RuntimeReadinessVerdict verdict = evaluateRuntimeReadinessTyped( model, hw );
    CHECK( verdict.readiness == ModelReadiness::IncompatibleHardware );
    CHECK( verdict.refusal == RuntimeRefusalKind::GpuUnavailable );
  }

  SECTION( "vram estimate exceeds the host budget (legacy tokenless path)" )
  {
    ModelInfo model = modelWithFramework( "r29-ready-fw" );
    model.runtime.gpu = true;
    model.runtime.cpuFallback = false;
    model.runtime.estimatedVramMb = 999999;
    registerLoudFramework( "r29-ready-fw" );
    const RuntimeReadinessVerdict verdict = evaluateRuntimeReadinessTyped( model, gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::IncompatibleHardware );
    CHECK( verdict.refusal == RuntimeRefusalKind::ResourceBudget );
    // The human-readable trail keeps the historical spelling.
    CHECK_THAT( verdict.reason, Catch::Matchers::ContainsSubstring( "VRAM but the host budget" ) );
  }

  SECTION( "unparsable device token" )
  {
    ModelInfo model = modelWithFramework( "r29-ready-fw" );
    model.runtime.device = "tensor-4";
    registerLoudFramework( "r29-ready-fw" );
    const RuntimeReadinessVerdict verdict =
      evaluateRuntimeReadinessTyped( model, gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::InvalidManifest );
    CHECK( verdict.refusal == RuntimeRefusalKind::DeviceRequestInvalid );
  }

  SECTION( "unaddressable explicit cuda index" )
  {
    ModelInfo model = modelWithFramework( "r29-ready-fw" );
    model.runtime.gpu = true;
    model.runtime.cpuFallback = false;
    model.runtime.device = "cuda:7";
    registerLoudFramework( "r29-ready-fw" );
    const RuntimeReadinessVerdict verdict =
      evaluateRuntimeReadinessTyped( model, gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::IncompatibleHardware );
    CHECK( verdict.refusal == RuntimeRefusalKind::DeviceNotAddressable );
    CHECK_THAT( verdict.reason, Catch::Matchers::ContainsSubstring( "not addressable" ) );
  }
}

TEST_CASE( "R29-T01: the mixed 'no fitting CUDA device' spelling now separates "
           "kinds at the source",
           "[models][r29t01][readiness]" )
{
  HardwareGuard guard;
  registerLoudFramework( "r29-ready-fw" );

  SECTION( "auto with no CUDA at all is a hardware-availability refusal" )
  {
    ModelInfo model = modelWithFramework( "r29-ready-fw" );
    model.runtime.gpu = true;
    model.runtime.cpuFallback = false;
    model.runtime.device = "auto";
    ModelHardwareCapabilities hw;
    hw.cudaAvailable = false;
    const RuntimeReadinessVerdict verdict = evaluateRuntimeReadinessTyped( model, hw );
    CHECK( verdict.readiness == ModelReadiness::IncompatibleHardware );
    CHECK( verdict.refusal == RuntimeRefusalKind::GpuUnavailable );
    // Same MESSAGE as the historical single spelling — only the kind differs.
    CHECK_THAT( verdict.reason,
                Catch::Matchers::ContainsSubstring( "no fitting CUDA device" ) );
  }

  SECTION( "auto with devices present but none fitting is a budget refusal" )
  {
    ModelInfo model = modelWithFramework( "r29-ready-fw" );
    model.runtime.gpu = true;
    model.runtime.cpuFallback = false;
    model.runtime.device = "auto";
    model.runtime.estimatedVramMb = 999999;
    const RuntimeReadinessVerdict verdict =
      evaluateRuntimeReadinessTyped( model, gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::IncompatibleHardware );
    CHECK( verdict.refusal == RuntimeRefusalKind::ResourceBudget );
    CHECK_THAT( verdict.reason,
                Catch::Matchers::ContainsSubstring( "no fitting CUDA device" ) );
  }
}

TEST_CASE( "R29-T01: fallback chains keep per-candidate verdicts; the primary "
           "classification wins",
           "[models][r29t01][readiness]" )
{
  HardwareGuard guard;
  registerLoudFramework( "r29-ready-fw" );

  SECTION( "a ready fallback rescues the chain" )
  {
    ModelInfo model = modelWithFramework( "r29-no-such-fw" );
    model.runtime.frameworkFallback = { "r29-ready-fw" };
    const RuntimeReadinessVerdict verdict = evaluateRuntimeReadinessTyped( model, gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::Ready );
    CHECK( verdict.refusal == RuntimeRefusalKind::None );
    CHECK( verdict.reason.empty() );
    // Both attempts stay visible for consumers explaining the chain.
    REQUIRE( verdict.candidates.size() == 2 );
    CHECK( verdict.candidates.front().framework == "r29-no-such-fw" );
    CHECK( verdict.candidates.front().refusal == RuntimeRefusalKind::ProviderMissing );
    CHECK( verdict.candidates.back().framework == "r29-ready-fw" );
    CHECK( verdict.candidates.back().readiness == ModelReadiness::Ready );
  }

  SECTION( "an all-failed chain reports the primary's kind, not the last's" )
  {
    ModelInfo model = modelWithFramework( "r29-no-such-fw" );
    model.runtime.gpu = true;
    model.runtime.cpuFallback = false;
    model.runtime.estimatedVramMb = 999999;
    model.runtime.frameworkFallback = { "r29-ready-fw" };
    const RuntimeReadinessVerdict verdict =
      evaluateRuntimeReadinessTyped( model, gpuHost() );
    CHECK( verdict.readiness == ModelReadiness::UnsupportedRuntime );
    // Policy: the chain only widens availability — the primary's typed
    // refusal classifies the failure even though the FALLBACK failed on
    // budget (the old string projection could read the fallback's
    // "VRAM but" and misclassify the whole chain as a budget error).
    CHECK( verdict.refusal == RuntimeRefusalKind::ProviderMissing );
    REQUIRE( verdict.candidates.size() == 2 );
    CHECK( verdict.candidates.back().refusal == RuntimeRefusalKind::ResourceBudget );
    // The trail keeps the full attempt history with the exact wording.
    CHECK_THAT( verdict.reason, Catch::Matchers::ContainsSubstring( "framework 'r29-no-such-fw'" ) );
    CHECK_THAT( verdict.reason, Catch::Matchers::ContainsSubstring( "fallback 'r29-ready-fw'" ) );
  }

  SECTION( "the legacy string API returns the same state and trail" )
  {
    ModelInfo model = modelWithFramework( "r29-no-such-fw" );
    model.runtime.frameworkFallback = { "r29-ready-fw" };
    std::string reason = "stale";
    const ModelReadiness state = evaluateRuntimeReadiness( model, gpuHost(), &reason );
    CHECK( state == ModelReadiness::Ready );
    // A successful verdict clears any stale caller buffer.
    CHECK( reason.empty() );
  }
}

TEST_CASE( "R29-T01: an all-nodata raster run reads as empty in the quality summary (WP09)",
           "[models][r29t01][quality]" )
{
  // quality.raster_empty mirrors the detection lane's detection_empty: a
  // grid-complete run whose every tile was skipped published an entirely
  // NoData product — the summary must say so instead of making consumers
  // re-derive it from counters.
  ModelExecutionResult result;
  result.rasterStats.tilesPlanned = 4;
  result.rasterStats.tilesProcessed = 0;
  result.rasterStats.tilesSkippedNoData = 4;
  result.rasterStats.outWidth = 32;
  result.rasterStats.outHeight = 32;
  result.rasterStats.outBands = 1;

  const InferenceQualitySummary summary = summarizeInferenceQuality( result );
  CHECK( summary.gridComplete );
  CHECK( summary.tileCoverageRatio == Catch::Approx( 0.0 ) );
  bool hasRasterEmpty = false;
  for ( const InferencePlanFinding &note : summary.notes )
    if ( note.code == "quality.raster_empty" )
      hasRasterEmpty = true;
  CHECK( hasRasterEmpty );

  // A healthy run (tiles processed, nothing skipped) carries no note.
  ModelExecutionResult healthy;
  healthy.rasterStats.tilesPlanned = 4;
  healthy.rasterStats.tilesProcessed = 4;
  healthy.rasterStats.outWidth = 32;
  healthy.rasterStats.outHeight = 32;
  healthy.rasterStats.outBands = 1;
  const InferenceQualitySummary healthySummary = summarizeInferenceQuality( healthy );
  for ( const InferencePlanFinding &note : healthySummary.notes )
    CHECK( note.code != "quality.raster_empty" );
}
