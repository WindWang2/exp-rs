/***************************************************************************
 * src/plugins/framework/plugin_model_runtime_bridge.h
 *
 * Adapts plugin model runtimes (exprs::IPluginModelRuntimeV1, OpenCV-free)
 * onto the in-repo ModelRuntimeRegistry contract (IModelRuntime, cv::Mat
 * based). Compiled only when the host build has OpenCV; plugin binaries
 * never need OpenCV headers.
 ***************************************************************************/
#pragma once

#include "exprs/plugin_interface.h"

#include <string>

namespace sicnu::plugins {

#if defined( SICNU_HAS_OPENCV )
/// Registers a bridge factory for @p framework into the in-repo
/// ModelRuntimeRegistry. The bridge resolves the plugin's own factory at
/// acquire time (after ensuring the plugin is loaded). Returns true when
/// the framework was registered.
bool registerPluginModelRuntime( const std::string &framework, const std::string &pluginId );
#endif

/// Executor-side storage shared with PluginRuntimeHost::registerModelRuntime.
void storePluginModelRuntimeFactory( const std::string &framework, const std::string &pluginId,
                                     exprs::PluginModelRuntimeFactoryV1 factory );
void clearPluginModelRuntimeFactory( const std::string &framework );

} // namespace sicnu::plugins
