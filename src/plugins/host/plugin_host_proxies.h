/***************************************************************************
 * src/plugins/host/plugin_host_proxies.h — launcher-side contribution
 * proxies. Each proxy marshals one contribution call over the IPC contract;
 * on worker death it applies the restart policy ONCE (atomic arm so a
 * crash-looping plugin can never stampede or spin the host) and otherwise
 * fails typed.
 ***************************************************************************/
#pragma once

#include "exprs/plugin_interface.h"
#include "exprs/plugin_quotas.h"

#include "plugin_host_process_runtime.h"

#include <string>

namespace sicnu::plugins {

/// RSOperator whose run() executes in the worker process.
std::unique_ptr<sicnu::operators::RSOperator> makeHostProcessOperatorProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string operatorId );

/// IPluginAgentToolV1 whose execute() runs in the worker process.
std::shared_ptr<exprs::IPluginAgentToolV1> makeHostProcessAgentToolProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string toolId );

/// IPluginDataProviderV1 whose discover/inspect/open run in the worker.
std::shared_ptr<exprs::IPluginDataProviderV1> makeHostProcessDataProviderProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string providerId );

/// IPluginModelRuntimeV1 whose load/infer run in the worker process.
exprs::PluginModelRuntimePtrV1 makeHostProcessModelRuntimeProxy(
    PluginHostSessionEntryPtr entry, std::string pluginId, std::string framework,
    const exprs::PluginModelRequestV1 &request, std::string &error );

} // namespace sicnu::plugins
