// test_provider_guard_r4.cpp — Track 15 WP-E: the provider switch-matrix
// guard for the DEFAULT build (TensorRT off, OpenVINO off — the shipped
// configuration; #1334 once had to route around a link issue here, so the
// default cell is pinned as a hard test instead of an assumption).
//
//   Cell matrix (evidence in PROVIDER_OOM_MATRIX.md):
//     off/off        — THIS test (default build; the hard guard)
//     TensorRT on, no SDK  — configure-level cell: graceful "requested but
//                            the SDK was not found", provider still absent
//     OpenVINO on, no SDK  — same shape (matrix runs in the track scripts)
//     *-on with SDK        — N/A on this host, recorded with evidence
//
// The contract: absent optional providers are TYPED absences — readiness
// refuses with UnsupportedRuntime naming the framework, the registry simply
// does not have them, and the built-in provider surface is untouched.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_runtime.h"

#include <string>

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;

namespace
{
ModelInfo modelWithFramework( const std::string &framework )
{
    ModelInfo model;
    model.name = "r4-provider-guard";
    model.task = "segmentation";
    model.framework = framework;
    model.readiness = ModelReadiness::Ready; // catalog-static state: providers decide here
    return model;
}
} // namespace

TEST_CASE( "Default build cell: optional deployment providers are typed absences",
           "[provider][guard][r4]" )
{
    auto &registry = ModelRuntimeRegistry::instance();

    // TensorRT / OpenVINO are OFF in the default build: the registry has no
    // such members (typed absence, not a ghost entry).
    CHECK_FALSE( registry.hasProvider( "tensorrt" ) );
    CHECK_FALSE( registry.hasProvider( "openvino" ) );
    // ONNX Runtime is SDK-gated and its SDK is absent in the default build.
    CHECK_FALSE( registry.hasProvider( "onnxruntime" ) );

    // The default-build provider surface is otherwise intact: the built-in
    // OpenCV-DNN-backed "onnx" provider is present.
    REQUIRE( registry.hasProvider( "onnx" ) );
}

TEST_CASE( "Default build cell: a TensorRT-framework model refuses with UnsupportedRuntime",
           "[provider][guard][r4]" )
{
    const ModelHardwareCapabilities hw; // default detection (no CUDA on this lane)

    for ( const std::string &framework : { std::string( "tensorrt" ), std::string( "openvino" ) } )
    {
        INFO( "framework: " << framework );
        const ModelInfo model = modelWithFramework( framework );
        std::string reason;
        const ModelReadiness readiness = evaluateRuntimeReadiness( model, hw, &reason );
        REQUIRE( readiness == ModelReadiness::UnsupportedRuntime );
        // The refusal names the framework and the BUILD (an actionable, typed
        // absence — never a silent demotion or a crash).
        REQUIRE( reason.find( "no runtime provider available for framework '" + framework + "'" )
                 != std::string::npos );
        REQUIRE( reason.find( "in this build" ) != std::string::npos );
    }

    // The same evaluation for the BUILT-IN provider is not refused by the
    // provider gate (the guard's control arm: readiness uses one truth).
    const ModelInfo onnxModel = modelWithFramework( "onnx" );
    std::string onnxReason;
    const ModelReadiness onnxReadiness =
        evaluateRuntimeReadiness( onnxModel, hw, &onnxReason );
    REQUIRE( onnxReadiness != ModelReadiness::UnsupportedRuntime );
}
