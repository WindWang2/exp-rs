// src/agent/workspace_state.h
#pragma once

//
// Workspace Understanding 3.0 (Phase B) — the WorkspaceState contract.
//
// Extends the free-text WorkspaceSnapshot with a structured, bounded JSON
// document in which every entity carries a stable short id ("asset-3",
// "layer-2", "chart-1", …). User utterances like "把刚才那个 NDVI 图的颜色调浅"
// resolve through these ids instead of fuzzy name matching.
//
// Layering: this module must not link the workflow engine (workflow sits above
// the agent library). Workflow run summaries arrive through a provider seam
// registered by the app/MCP layer (setWorkflowRunsProvider).
//

#include <json/json.h>

#include <functional>
#include <QMap>
#include <QMutex>
#include <QString>

class QgsMapCanvas;
namespace sicnu::workspace { class WorkspaceService; }

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::agent {

/**
 * Process-wide service locator handing workspace-attached objects to stateless
 * SpatialTools (which are constructed before a DataManager/canvas exists).
 * The app shell, copilot dock, and `--mcp` main() wire these at startup; tools
 * must tolerate a null pointer (return a structured "workspace_unavailable").
 */
class AgentServices
{
  public:
    static AgentServices &instance();

    void setDataManager( data::DataManager *manager ) { mDataManager = manager; }
    data::DataManager *dataManager() const { return mDataManager; }

    void setMapCanvas( QgsMapCanvas *canvas ) { mMapCanvas = canvas; }
    QgsMapCanvas *mapCanvas() const { return mMapCanvas; }

    /// Workspace Governance 3.0 seam (Platform 3.0). The host (GUI shell or
    /// the --mcp main()) wires the project-scoped WorkspaceService here at
    /// startup; governance tools tolerate a null pointer.
    void setWorkspaceService( sicnu::workspace::WorkspaceService *service ) { mWorkspaceService = service; }
    sicnu::workspace::WorkspaceService *workspaceService() const { return mWorkspaceService; }

  private:
    data::DataManager *mDataManager = nullptr;
    QgsMapCanvas *mMapCanvas = nullptr;
    sicnu::workspace::WorkspaceService *mWorkspaceService = nullptr;
};

/**
 * Stable entity-id registry. Maps natural keys (asset canonical path, layer
 * uuid, collection id, layout name, chart id) to short ids per kind.
 * Mappings persist through QgsProject custom properties (scope "sicnu",
 * key "entityIds/<kind>") when a project instance exists, so referents
 * survive save/load; otherwise they live in-process.
 */
class WorkspaceEntityRegistry
{
  public:
    static WorkspaceEntityRegistry &instance();

    /// Stable id ("asset-1") for (kind, naturalKey); assigns a new one on
    /// first sight. kind must not contain '/'.
    QString idFor( const QString &kind, const QString &naturalKey );

    /// Reverse lookup; empty string when the id is unknown.
    QString naturalKeyFor( const QString &id ) const;

    /// Drops in-process mappings (project-persisted entries remain until
    /// overwritten lazily on next assignment).
    void clearInProcess();

  private:
    WorkspaceEntityRegistry() = default;

    void loadPersisted( const QString &kind );
    void persist( const QString &kind );

    mutable QMutex mMutex;
    QMap<QString, QString> mIdByKey;   // "kind\u0001key" -> id
    QMap<QString, QString> mKeyById;   // id -> "kind\u0001key"
    QMap<QString, int> mNextCounter;   // kind -> next ordinal
};

/// Provider seam for workflow-run summaries (avoids an agent → workflow link
/// dependency). The app/MCP layer registers a bounded (≤ 20 runs) producer.
using WorkflowRunsProvider = std::function<Json::Value()>;
void setWorkflowRunsProvider( WorkflowRunsProvider provider );

/**
 * Builds the WorkspaceState contract document:
 *   { schema_version, kind: "workspace_state", project, view, active,
 *     assets[], layers[], temporal_collections[], layouts[], charts[],
 *     models[], running_tasks[], recent_outputs[], workflow_runs[] }
 *
 * Bounded by construction: assets capped at 200, layers 200, models 25,
 * running tasks 25, recent outputs `recentOutputsLimit` (default 8, max 50).
 */
Json::Value buildWorkspaceState( data::DataManager *dataManager,
                                 QgsMapCanvas *canvas,
                                 const QString &activeLayerName = {},
                                 int recentOutputsLimit = 8 );

} // namespace sicnu::agent
