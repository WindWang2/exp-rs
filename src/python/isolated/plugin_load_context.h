// src/python/isolated/plugin_load_context.h — consolidated context for Python plugin loading (ADR 0044)
#pragma once

class QMenu;
class ActiveViewHost;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::python::isolated
{

/**
 * Consolidated context for Python plugin loading (ADR 0044).
 *
 * Replaces the 3-argument "unpack and repack" seam that threaded
 * DataManager, QMenu, and ActiveViewHost through PythonPluginHost::loadPlugin
 * and PythonPluginAdapter as individual raw pointers. All three are optional
 * (headless surfaces may pass nullptr for all).
 */
struct PluginLoadContext
{
  sicnu::data::DataManager *dataManager = nullptr;
  QMenu *pluginMenu = nullptr;
  ActiveViewHost *activeViewHost = nullptr;
};

} // namespace sicnu::python::isolated
