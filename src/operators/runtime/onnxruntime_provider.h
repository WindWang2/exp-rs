// src/operators/runtime/onnxruntime_provider.h — optional ONNX Runtime
// provider (Platform 3.0, goal §9).
//
// The provider is compiled ONLY when CMake finds ONNX Runtime and
// SICNU_WITH_ONNX_RUNTIME is ON. On hosts without the dependency the stub
// registration is a no-op: `hasProvider("onnxruntime")` stays false and
// models declaring framework "onnxruntime" surface the honest readiness
// verdict `runtime_unavailable` — the rest of the platform is unaffected.
// This is the documented graceful-degradation path (no optional dependency
// may break the build or disable the software).
//
// Framework id contract: "onnx" remains OpenCV DNN (existing manifests keep
// working); "onnxruntime" selects this provider. Runtime selection prefers
// the manifest-declared framework; TensorRT/CUDA execution-provider options
// are surfaced through ORT's own environment (documented in models/README).
#pragma once

#include <string>

namespace sicnu::operators::runtime
{

/// True when this build embeds the ONNX Runtime provider (compile-time).
bool onnxRuntimeProviderAvailable();

/// Registers the "onnxruntime" provider factory with the registry when the
/// provider is compiled in; no-op otherwise. Called once from the registry
/// constructor.
void registerOnnxRuntimeProvider();

/// Formats the compile-time provider state for readiness reasons.
std::string onnxRuntimeUnavailableReason();

} // namespace sicnu::operators::runtime
