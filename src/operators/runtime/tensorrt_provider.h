// src/operators/runtime/tensorrt_provider.h — optional TensorRT provider
// (Platform 10.0 provider matrix). Compiled ONLY when SICNU_WITH_TENSORRT is
// defined and the TensorRT headers/libs were found at configure time; the
// absent branch registers a graceful-degradation stub exactly like the ONNX
// Runtime provider, so the default build never changes behavior.
#pragma once

#include "operators/runtime/model_runtime.h"

namespace sicnu::operators::runtime {

bool tensorRTProviderAvailable();
void registerTensorRTProvider( ModelRuntimeRegistry &registry );
std::string tensorRTUnavailableReason();

} // namespace sicnu::operators::runtime
