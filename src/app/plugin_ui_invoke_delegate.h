/***************************************************************************
 * plugin_ui_invoke_delegate.h — Workbench 9.0 M8
 *
 * The production UiInvokeDelegate for declarative out-of-process plugin UI:
 * it forwards rendered events to PluginRuntimeHost::invokePluginUi (which
 * wraps the host-process worker's ui.invoke). Created by the SHELL when it
 * attaches a plugin schema — the seam plugin-platform 8.0 left to this
 * track. Thread-safe: PluginUiSchemaRenderer calls invoke() on its single
 * serialized delivery thread, never on the GUI thread.
 ***************************************************************************/
#pragma once

#include "plugins/framework/plugin_runtime_host.h"
#include "plugins/framework/plugin_ui_schema_host.h"

#include <json/json.h>

#include <QString>

namespace sicnu::app
{

class PluginUiInvokeDelegate : public sicnu::plugins::UiInvokeDelegate
{
  public:
    explicit PluginUiInvokeDelegate( QString pluginId ) : mPluginId( std::move( pluginId ) ) {}

    Json::Value invoke( const Json::Value &event ) override
    {
      // PluginRuntimeHost::invokePluginUi locks its own mutex and blocks
      // until the worker answers or the timeout elapses — acceptable on the
      // renderer's delivery thread (a wedged plugin delays one event, never
      // the UI).
      return sicnu::plugins::PluginRuntimeHost::instance().invokePluginUi(
        mPluginId.toStdString(), event );
    }

  private:
    QString mPluginId;
};

} // namespace sicnu::app
