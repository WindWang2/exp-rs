/***************************************************************************
 * agent_context_tool.h — Workbench 10.0 UI→agent context projection
 *
 * A read-only SpatialTool (`workbench:context`, ADR 0122 surface) that lets
 * the agent read the CURRENT workbench selection/context: active workbench,
 * typed primary object, every selection list, ContextFacts and the registry
 * command ids it may run. The tool owns nothing: a provider callable is
 * injected by the shell at assembly time and produces the payload through
 * workbenchContextToJson(). Execution stays on the caller's thread (the MCP
 * request path is main-thread), so the provider may query the live
 * SelectionContext — it must be cheap and side-effect free.
 *
 * Write paths are deliberately NOT widened: the agent keeps acting through
 * the existing tools/command authority.
 ***************************************************************************/
#pragma once

#include "agent/spatial_tools/spatial_tool.h"

#include <functional>

namespace sicnu::app
{

class WorkbenchContextTool : public sicnu::agent::spatial_tools::SpatialTool
{
  public:
    /// Produces the payload JSON (workbenchContextToJson output). Empty JSON
    /// (null) means the workbench shell is not assembled (headless) — the
    /// tool answers with an honest "unavailable" result instead of an empty
    /// success.
    using PayloadProvider = std::function<Json::Value()>;

    explicit WorkbenchContextTool( PayloadProvider provider );

    std::string name() const override { return "workbench:context"; }
    std::string displayName() const override;
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    sicnu::agent::spatial_tools::SpatialToolResult execute( const Json::Value &input ) override;

  private:
    PayloadProvider m_provider;
};

} // namespace sicnu::app
