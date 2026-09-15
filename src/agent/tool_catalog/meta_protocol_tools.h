// src/agent/tool_catalog/meta_protocol_tools.h
//
// Single source of the MCP protocol-level meta tools (list_algorithms …
// resume_workflow). Moved verbatim from mcp_server.cpp (#ADR 0022 table) so
// the surface projection (surface_registry.h), the MCP server, and the CLI
// `tools` command all render the SAME table — the table is the schema
// authority for these tools, and there is exactly one table.
#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::tool_catalog::meta_protocol {

struct MetaToolInput
{
    const char *name;
    const char *type;
    const char *description;
    bool required;
};

struct MetaToolDef
{
    const char *name;
    const char *description;
    QList<MetaToolInput> inputs;
};

/// Protocol tool table in wire order (tools/list renders it in this order).
const std::vector<MetaToolDef> &defs();

/// JSON Schema for one table row: {"type":"object","properties":{…},
/// "required":[…]} — the exact shape mcp_server.cpp projected before the
/// extraction (same types/descriptions/required sets).
Json::Value schema( const MetaToolDef &def );

/// Schema for a meta tool by name; a null Json::Value when @p name is not a
/// meta protocol tool.
Json::Value schemaFor( const std::string &name );

/// True when @p name is a meta protocol tool (dispatchable by tools/call).
bool contains( const std::string &name );

} // namespace sicnu::agent::tool_catalog::meta_protocol
