// src/operators/runtime/openvino_provider.h — optional OpenVINO provider
// (Platform 10.0 provider matrix). Compiled ONLY when SICNU_WITH_OPENVINO is
// defined and OpenVINO was found at configure time; the absent branch keeps
// the typed-unavailability contract (default build unchanged).
#pragma once

#include "operators/runtime/model_runtime.h"

namespace sicnu::operators::runtime {

bool openVinoProviderAvailable();
void registerOpenVinoProvider( ModelRuntimeRegistry &registry );
std::string openVinoUnavailableReason();

} // namespace sicnu::operators::runtime
