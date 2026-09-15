/***************************************************************************
  agent/tools/temporal_tool.h
  Temporal Phenology Timeline Studio (D16) — catalog-facing temporal tool.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Thin registration-shape binding over TemporalSpatialTool: the name,
  schema and execution surface an Agent Tool Catalog entry exposes. State
  and logic live in temporal_spatial_tools.h (ADR 0161).
 ***************************************************************************/

#ifndef SICNU_AGENT_TOOLS_TEMPORAL_TOOL_H
#define SICNU_AGENT_TOOLS_TEMPORAL_TOOL_H

#include "agent/spatial_tools/temporal_spatial_tools.h"

#include <json/json.h>

#include <string>

namespace sicnu::agent
{

class TemporalTool
{
  public:
    static const char *name() { return "temporal"; }

    /// Full registration schema (all three temporal:* functions).
    static Json::Value schema() { return TemporalSpatialTool::toolSchema(); }

    /// Delegates execution to the spatial-tool kernel.
    static Json::Value execute( const std::string &toolName, const Json::Value &arguments )
    {
        return TemporalSpatialTool::executeTool( toolName, arguments );
    }
};

} // namespace sicnu::agent

#endif // SICNU_AGENT_TOOLS_TEMPORAL_TOOL_H
