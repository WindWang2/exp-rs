/***************************************************************************
 * src/plugins/host/plugin_host_protocol.h — method names & payload shapes
 * of the host-process plugin protocol v1 (shared by launcher and worker).
 *
 * Wire contract (see docs/plugins/host-process.md):
 *   - transport: length-prefixed JSON frames over two inherited OS handles
 *     (never stdio: plugin printf noise lands on the worker's real stdout,
 *     which is a sink, and can never corrupt framing)
 *   - handshake: the worker sends a "worker.hello" event first thing; the
 *     launcher validates the protocol axis (E6001) BEFORE plugin.load
 *   - services are materialized at plugin.load (no reverse request/response
 *     in v1); the worker reports progress/log as fire-and-forget frames
 *   - large artifacts stay workspace-contained file references; tensor
 *     exchanges are bounded JSON (frame cap is the enforcement point)
 ***************************************************************************/
#pragma once

namespace sicnu::plugins::hostprotocol {

/// Request methods (launcher -> worker).
constexpr const char *kLoadPlugin = "plugin.load";
constexpr const char *kShutdownPlugin = "plugin.shutdown";
constexpr const char *kExecuteOperator = "operator.execute";
constexpr const char *kExecuteAgentTool = "agentTool.execute";
constexpr const char *kDiscoverData = "dataProvider.discover";
constexpr const char *kInspectData = "dataProvider.inspect";
constexpr const char *kOpenData = "dataProvider.open";
constexpr const char *kLoadModel = "modelRuntime.load";
constexpr const char *kInferModel = "modelRuntime.infer";

/// Handshake event (worker -> launcher).
constexpr const char *kWorkerHello = "worker.hello";

/// Command-line switches carrying the inherited protocol handles.
constexpr const char *kIpcReadSwitch = "--exprs-ipc-read=";
constexpr const char *kIpcWriteSwitch = "--exprs-ipc-write=";

/// Worker exit codes for the launcher's recovery logic.
constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;
constexpr int kExitLoadFailed = 3;

/// plugin.load params/registration-report shape helpers.
///   params: { "entrypoint": <path>, "manifest": <manifest json>,
///             "services": { "tempDirectory", "workspaceRoot",
///                           "dataDirectory", "pluginDirectory" } }
///   response result: { "registered": { "operators": [id...],
///       "dataProviders": [id...], "modelRuntimes": [framework...],
///       "agentTools": [id...] } }

/// operator.execute params: { "operatorId", "params", "workDir" }
///   ok result:  { "success": true, "result": <run() json> }
///   error:      code = decimal sicnu::operators::ErrorCode, message,
///               data = RSOperatorError details
/// agentTool.execute params: { "toolId", "params" }
///   ok result:  SpatialTool envelope json
/// dataProvider.* params: { "providerId", ... method-specific fields }
/// modelRuntime.load params: { "framework", "request": PluginModelRequestV1 }
/// modelRuntime.infer params: { "framework", "input": PluginTensorV1,
///                              "outputTensorName" }
/// plugin.shutdown params: {} — the worker runs PluginV1::shutdown and
/// exits with kExitOk after replying.

} // namespace sicnu::plugins::hostprotocol
