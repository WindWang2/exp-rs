/// mcp_workspace_policy.h — the canonical implementation of the
/// SICNU_MCP_WORKSPACE containment policy shared by every path-consuming
/// MCP branch (#1033): McpServer::validateWorkspacePaths (rs:/gdal:/qgis:/
/// operator/spatial branches), the data-platform tool family, and
/// run_workflow's recording arguments. New MCP branches must consume these
/// functions rather than re-deriving a weaker check.
#pragma once

class QString;
class QVariant;

namespace sicnu::agent
{

/// The configured workspace root (SICNU_MCP_WORKSPACE); empty when no
/// sandbox is configured (every path is then allowed).
QString mcpWorkspaceRoot();

/// True when @p pathValue resolves outside @p workspaceRoot. Relative paths
/// resolve against the root; a non-existent path resolves through its
/// existing parent directory. Remote http(s)/vsicurl data references are
/// governed by SICNU_MCP_ALLOW_REMOTE; file:// maps to its local path.
/// *detail (when provided) receives a stable, caller-scoped reason that
/// never reveals filesystem state beyond the path the caller supplied.
bool mcpPathOutsideWorkspace( const QString &pathValue, const QString &workspaceRoot,
                              QString *detail = nullptr );

/// Recursive containment scan over a JSON-ish argument tree (strings,
/// lists, maps): true as soon as one string member lies outside the root.
bool mcpCollectOutsideWorkspace( const QVariant &value, const QString &workspaceRoot,
                                 QString *detail = nullptr );

/// Anchors a relative path at the workspace root while the sandbox is
/// active (and expands a leading ~), so the validated path and the path a
/// handler finally opens are the same file. Absolute paths and sandbox-less
/// runs come back unchanged.
QString mcpResolveWorkspacePath( const QString &path );

} // namespace sicnu::agent
