// src/operators/runtime/python_worker_provider.h — Platform 7.0 Python
// worker inference provider contract. The worker is an OUT-OF-PROCESS
// interpreter (default "python3") running the manifest's
// runtime.provider.worker_script, speaking the shared exp-rs-infer/1 wire
// document (provider_wire.h) as newline-delimited JSON over stdin/stdout:
//
//   → on startup the worker prints one line: {"protocol":"exp-rs-infer/1","event":"ready"}
//   → per inference the runtime writes one request line
//   → the worker answers one response line (outputs document or {"error":...})
//
// The provider registers through ModelRuntimeRegistry::registerProvider like
// every other backend — no second catalog, no second runtime. Worker crashes
// map to the ProviderCrash failure kind.
#pragma once

#include <string>

namespace sicnu::operators::runtime {

class ModelRuntimeRegistry;

/// Registers the "python" framework factory on @p registry (Qt Core QProcess
/// based; always compiled — the dependency surface is Qt alone). Takes the
/// registry BY REFERENCE: this runs inside the registry constructor, where
/// instance() would re-enter the static initializer.
void registerPythonWorkerProvider( ModelRuntimeRegistry &registry );

} // namespace sicnu::operators::runtime
