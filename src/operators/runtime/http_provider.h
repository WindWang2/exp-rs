// src/operators/runtime/http_provider.h — Platform 7.0 HTTP inference
// provider contract. Compiled when Qt6::Network is available; otherwise the
// registration is a no-op stub and models declaring framework "http" surface
// the honest UnsupportedRuntime verdict (the documented graceful-degradation
// pattern shared with the ONNX Runtime provider).
//
// The provider posts the shared exp-rs-infer/1 wire document (provider_wire.h)
// to the manifest's runtime.provider.url and decodes the outputs document.
// It registers through ModelRuntimeRegistry::registerProvider like every
// other backend — no second catalog, no second runtime.
#pragma once

#include <string>

namespace sicnu::operators::runtime {

class ModelRuntimeRegistry;

/// True when this build embeds the HTTP provider.
bool httpProviderAvailable();

/// Registers the "http" framework factory on @p registry (no-op when
/// Qt6::Network is unavailable). Takes the registry BY REFERENCE: this runs
/// inside the registry constructor, where instance() would re-enter the
/// static initializer.
void registerHttpProvider( ModelRuntimeRegistry &registry );

/// Formats the compile-time provider state for readiness reasons.
std::string httpProviderUnavailableReason();

} // namespace sicnu::operators::runtime
