/***************************************************************************
 * mission_tools.h — real `mission:*` agent tools (MCP / Pi surface)
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * Workbench 12.0 declared the `mission:context` / `mission:timeline` /
 * `mission:advance` identities in the surface registry and the MCP
 * allow-list, but no tool object existed: an allowed id fell through the
 * dispatch chain to "Algorithm not registered". These are the objects.
 *
 * Design rules:
 *   - QGIS-free. The tools read and write the mission runtime through
 *     MissionToolHost, whose default authority is the single-authority
 *     runtime store (works headless under `--mcp`). The desktop shell
 *     installs providers for the live project path, the execution run
 *     status and the layer liveness resolver; nothing here holds a raw
 *     QgsMapLayer* across an event-loop turn — references are ids.
 *   - Every mutation goes through MissionTimeline's fail-closed state
 *     machine (never around it) and is persisted through the single
 *     authority write. A rejected mutation commits nothing.
 *   - No second scheduler: run authority is *resolved* through the
 *     MissionRunStatusResolver the host installs (TaskCenter / workflow /
 *     pipeline run coordinators in the desktop build).
 *
 * The projections are the same functions the desktop model renders, so the
 * MCP/Pi payload and the GUI row cannot drift (surface parity by
 * construction — see tests/test_mission_surface_parity.cpp).
 ***************************************************************************/
#pragma once

#include "agent/spatial_tools/spatial_tool.h"
#include "app/workbench/mission_runtime_store.h"
#include "app/workbench/mission_run_authority.h"

#include <QDomDocument>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>
#include <mutex>

namespace sicnu::agent::spatial_tools
{

/// Strategy for the single-authority mission runtime read/write. The agent
/// surface depends only on this abstraction; the app side (which owns the
/// project file, QGIS and the live execution authorities) supplies the
/// implementation — `sicnu::app::MissionStoreAuthority` in
/// app/workbench/mission_tool_authority.h. Without an installed authority the
/// tools refuse rather than guess.
class MissionAuthority
{
public:
    virtual ~MissionAuthority() = default;

    virtual bool load( const QString &projectPath, const QDomDocument &document,
                       sicnu::app::MissionRuntimeState &state, QString &errorCode,
                       QString &errorMessage ) = 0;
    virtual bool commit( const QString &projectPath, QDomDocument &document,
                         sicnu::app::MissionRuntimeState &state, QString &errorCode,
                         QString &errorMessage ) = 0;
};

/// Process-wide host for the mission tools. The desktop shell (and the
/// headless `--mcp` branch of the same binary) installs its providers and
/// authority once at startup; the default is fail-closed (no project path →
/// the tools refuse instead of guessing).
class MissionToolHost
{
public:
    static MissionToolHost &instance();

    using ProjectPathProvider = std::function<QString()>;
    using RunStatusResolver = sicnu::app::MissionRunStatusResolver;
    using RefResolverProvider = std::function<sicnu::app::MissionRefResolver()>;
    using ProjectDocumentProvider = std::function<QDomDocument()>;

    void setAuthority( std::shared_ptr<MissionAuthority> authority );
    void setProjectPathProvider( ProjectPathProvider provider );
    void setRunStatusResolver( RunStatusResolver resolver );
    void setRefResolverProvider( RefResolverProvider provider );
    void setProjectDocumentProvider( ProjectDocumentProvider provider );

    /// Current project file name ("" when none — tools then refuse).
    QString currentProjectPath() const;
    /// Resolve one run reference; false when no resolver is installed.
    bool resolveRun( const sicnu::app::MissionRunRef &ref,
                     sicnu::app::MissionRunStatus &status ) const;
    /// Live reference resolver (layer/artifact liveness); false when none.
    bool refResolver( sicnu::app::MissionRefResolver &resolver ) const;

    /// Single-authority read under the runtime lock.
    bool readRuntime( sicnu::app::MissionRuntimeState &state, QString &errorCode,
                      QString &errorMessage ) const;
    /// Single-authority write under the runtime lock.
    bool commitRuntime( sicnu::app::MissionRuntimeState &state, QString &errorCode,
                        QString &errorMessage ) const;

    /// Serialized load → mutate → commit. `mutate` returns false for a domain
    /// rejection (nothing is committed, `reason` carries the machine code).
    /// The caller's `state` is refreshed from disk and left holding the
    /// committed value on success.
    using Mutation =
        std::function<bool( sicnu::app::MissionRuntimeState &state, QString &reason )>;
    bool mutateRuntime( sicnu::app::MissionRuntimeState &state, const Mutation &mutate,
                        QString &errorCode, QString &errorMessage, QString &reason );

private:
    mutable std::mutex mProviderMutex;
    mutable std::mutex mRuntimeMutex;
    std::shared_ptr<MissionAuthority> mAuthority;
    ProjectPathProvider mProjectPath;
    RunStatusResolver mRunResolver;
    RefResolverProvider mRefResolver;
    ProjectDocumentProvider mDocument;
};

/// `mission:context` — bounded mission context + task-space summary.
class MissionContextTool : public SpatialTool
{
public:
    std::string name() const override { return "mission:context"; }
    std::string displayName() const override { return "Read Mission Context"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// `mission:timeline` — stage summary, tasks and events after a cursor.
class MissionTimelineTool : public SpatialTool
{
public:
    std::string name() const override { return "mission:timeline"; }
    std::string displayName() const override { return "Read Mission Timeline"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// `mission:advance` — legal transitions / retry / run binding / reconcile.
class MissionAdvanceTool : public SpatialTool
{
public:
    std::string name() const override { return "mission:advance"; }
    std::string displayName() const override { return "Advance Mission Task"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// Structured outcome of one mission action. `transportFailure` separates
/// real errors (no project, corrupt authority, commit failure, unknown task)
/// from domain rejections, which are answers the caller may act on.
struct MissionActionResult
{
    bool applied = false;
    QString reason;           ///< "ok" or the machine rejection code
    QJsonObject task;         ///< updated task projection (empty when unknown)
    QJsonObject reconciliation; ///< only for action = "reconcile"
    quint64 revision = 0;
    quint64 lastEventSeq = 0;
    bool transportFailure = false;
    QString errorCode;
    QString errorMessage;
};

/// Apply one mission action through the host: load → state-machine mutation
/// → single-authority commit. Shared by the `mission:advance` tool and the
/// desktop shell commands, so the GUI and the agent surface cannot diverge.
/// Actions: start | succeed | fail | cancel | retry | bind_run | unbind_run |
/// reconcile. A rejected action persists nothing.
MissionActionResult applyMissionAction( const QString &taskId, const QString &action,
                                        const QString &runKind = {}, const QString &runId = {},
                                        const QString &errorCode = {},
                                        const QString &errorMessage = {},
                                        const QString &note = {} );

/// Register the three tools into the SpatialToolRegistry (called from
/// SpatialToolRegistry::registerBuiltinTools).
void registerMissionTools();

} // namespace sicnu::agent::spatial_tools
