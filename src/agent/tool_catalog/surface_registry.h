// src/agent/tool_catalog/surface_registry.h
//
// The authoritative UNION projection of every agent-callable tool:
// meta protocol tools + data-platform tools + AgentToolCatalog tools.
//
// MCP tools/list, the CLI `tools` command, and get_tool_schema fallback all
// render this one projection, so cross-surface discovery parity holds by
// construction; test_surface_parity gates the projection against dispatch
// reality (a table row without a dispatch branch, or the reverse, fails).
#pragma once

#include "agent_tool.h"

#include <QString>

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent::tool_catalog {

enum class SurfaceToolSource
{
    MetaProtocol,  ///< protocol-level tools (list_algorithms … resume_workflow)
    DataPlatform,  ///< dataset:/experiment:/reproducibility:/benchmark: tools
    Catalog        ///< AgentToolCatalog (processing/interaction/data/spatial providers)
};

struct SurfaceTool
{
    std::string name;
    std::string description;
    std::string family;  ///< namespace before ':'; "meta" for unprefixed protocol tools
    SurfaceToolSource source = SurfaceToolSource::MetaProtocol;
    Json::Value inputSchema;  ///< always {"type":"object", …}
};

struct SurfaceQuery
{
    /// false (default, headless): apply the same GUI-only interaction filter
    /// MCP tools/list applies — when the process has no interaction registry
    /// (or no view:get_state probe tool), GUI-bound catalog entries stay
    /// hidden. true exposes them (desktop/GUI contexts).
    bool includeGuiOnly = false;
};

/// The union projection in deterministic order: meta table order, data
/// platform table order, then catalog provider order — matching the wire
/// order MCP tools/list has always used. The MCP allow-prefix policy
/// (surfaceIdAllowed) and the headless GUI filter are applied to catalog
/// entries exactly as tools/list did before the extraction.
std::vector<SurfaceTool> collectSurfaceTools( const SurfaceQuery &query = {} );

/// Lookup by exact name; nullopt when the projection does not contain it.
std::optional<SurfaceTool> findSurfaceTool( const std::string &name,
                                            const SurfaceQuery &query = {} );

/// Namespace family of a tool id: the text before the first ':' (whole id
/// when unprefixed); meta protocol tools report "meta".
std::string surfaceToolFamily( const std::string &name );

/// Distinct families present in the projection, sorted ascending — the
/// static oracle for Pi's EXP_RS_TOOL_CATEGORIES parity gate.
std::vector<std::string> surfaceFamilies( const SurfaceQuery &query = {} );

/// MCP allow-prefix policy (moved verbatim from mcp_server.cpp): which
/// catalog ids tools/list may expose and tools/call accepts. A "processing:"
/// prefix is stripped before matching. custom_tools: is not allowed here —
/// @p isCustomTools reports it so the trust-gate caller can handle it.
bool surfaceIdAllowed( const QString &id, bool *isCustomTools = nullptr );

} // namespace sicnu::agent::tool_catalog
