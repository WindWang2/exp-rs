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

/// Interpreter policy for manifest-declared workers (review P1-6). A model
/// manifest is data, so it must not pick an arbitrary program to execute:
///   * empty -> the default "python3";
///   * a bare command name must be a Python launcher
///     (python, python3, python3.N, pythonw, py — optional ".exe");
///   * a path is accepted only when it canonicalizes to an interpreter the
///     USER configured: SICNU_PYTHON_EXECUTABLE or an entry of
///     SICNU_MODEL_INTERPRETERS (QDir::listSeparator()-separated).
/// Returns false with @p reason on rejection.
bool modelInterpreterAllowed( const std::string &interpreter, std::string *reason = nullptr );

/// Resolves a manifest worker_script against the manifest directory and
/// requires the result to stay inside it (no absolute paths elsewhere, no
/// "../" escapes, symlinks resolved). An empty @p manifestPath (models built
/// programmatically, not loaded from disk) skips the containment rule.
bool resolveModelWorkerScript( const std::string &workerScript, const std::string &manifestPath,
                               std::string *resolved, std::string *reason = nullptr );

} // namespace sicnu::operators::runtime
