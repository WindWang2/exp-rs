#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <QHash>
#include <QObject>
#include <QVariantMap>
#include <QThread>
#include <QCoreApplication>

#include <atomic>

#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

namespace sicnu::data
{
class DataManager;
class AssetId;
}

class QgsProcessingAlgorithm;
class QgsProcessingContext;
class QgsProcessingFeedback;

/// MCP status shape for a TaskCenter task (ADR 0022 status mapping):
/// - Queued / Running / Paused → "running"
/// - Completed → "completed" + `result` from resultPayload (omitted when null)
/// - Failed → "failed" + `errorMessage`
/// - Canceled → "canceled"
/// `progress` ← progressPercentage; `progressText` ← last logBuffer line
/// (omitted when empty).
QVariantMap mcpStatusForTask(const sicnu::AlgorithmTaskInfo &info);

class StdinReader : public QThread
{
    Q_OBJECT
public:
    StdinReader(QObject *parent = nullptr) : QThread(parent) {}
    void requestStop();
signals:
    void lineRead(const QString &line);
protected:
    void run() override;
private:
    std::atomic<bool> m_stopRequested{false};
};

/// Stateless JSON-RPC protocol adapter at the Task Center seam (ADR 0022):
/// owns stdio framing, the tool allow-list / workspace-path policy, and status
/// mapping — no execution machinery of its own. Single calls reach the Task
/// Center through ToolCallDispatcher (rs: operators) or TaskCenter::enqueueTask
/// (provider algorithms); execution ids are TaskCenter task ids ("task-<id>").
class McpServer : public QObject
{
    Q_OBJECT
public:
    McpServer(QObject *parent = nullptr);
    ~McpServer();

    void start(QCoreApplication *app);

    /// Injects DataManager asset authority so the dispatcher commits tool-call
    /// outputs transactionally (TICKET-23) and provenance queries resolve.
    /// Call before start().
    void setDataManager( sicnu::data::DataManager *dataManager )
    {
      m_dataManager = dataManager;
      mDispatcher.setDataManager( dataManager );
    }

private slots:
    void onLineRead(const QString &line);

protected:
    void handleRequest(const QVariantMap &request);
    /// Sends a tools/call execution failure as an MCP result object with
    /// isError:true (plus structured errorCode/errorCategory when known)
    /// instead of a JSON-RPC error response (#620).
    void sendToolErrorResult(const QVariant &id, const QString &message,
                             const QString &errorCode = QString(),
                             const QString &errorCategory = QString(),
                             bool retryable = false);

    virtual void sendResponse(const QVariant &id, const QVariantMap &result);
    virtual void sendError(const QVariant &id, int code, const QString &message);
    virtual void sendError(const QVariant &id, int code, const QString &message, const QVariantMap &data);
    virtual void sendNotification(const QString &method, const QVariantMap &params);

    // MCP Methods — canonical algorithm surface
    QVariantMap handleListAlgorithms(int limit = 50, int cursor = 0);
    QVariantMap handleSearchAlgorithms(const QString &query, const QString &group,
                                       const QString &inputType, const QString &outputType,
                                       bool largeRasterSafeOnly, int limit = 50, int cursor = 0);
    QVariantMap handleGetAlgorithmSchema(const QString &algorithmId);
    QVariantMap handlePreflightAlgorithm(const QString &algorithmId, const QVariantMap &parameters);
    QVariantMap handleExecuteAlgorithm(const QString &algorithmId, const QVariantMap &parameters);
    QVariantMap handleGetExecutionStatus(const QString &executionId);
    QVariantMap handleCancelExecution(const QString &executionId);
    QVariantMap handleListLayers();
    QVariantMap handleDescribeDataset(const QString &layerId);
    QVariantMap handleGetLineage(const QString &assetId);

    // MCP Methods — RSOperator kernel (preferred Agent surface)
    QVariantMap handleListOperators(int limit = 50, int cursor = 0);
    QVariantMap handleGetOperatorSchema(const QString &operatorId);
    QVariantMap handleExecuteOperator(const QString &operatorId, const QVariantMap &parameters);

    // MCP Methods — Unified Agent Tool Catalog (Algorithms + Interaction + Data)
    // Meta discovery tools default to compact (#643): agents list names +
    // descriptions and pull a candidate's schema via get_tool_schema. The
    // PROTOCOL tools/list (consumed by bridges, which need every
    // inputSchema to register tools) is a separate surface and stays
    // full-schema.
    QVariantMap handleListTools(const QString &category = QString(), bool compact = true,
                                int limit = 0, int cursor = 0);
    /// Capability facets for handleSearchTools (#701): string facets are
    /// substring/exact filters, bool facets apply only when their has* flag
    /// is set (tri-state, so an absent parameter never filters).
    struct SearchToolsFacets
    {
        QString task;
        QString modality;
        QString bandRoles;
        QString memoryPolicy;
        QString costClass;
        bool hasDeterministic = false;
        bool deterministic = false;
        bool hasGpu = false;
        bool gpu = false;
        bool hasTemporal = false;
        bool temporal = false;
        bool largeRasterSafe = false;
    };
    QVariantMap handleSearchTools(const QString &query, const QString &group = QString(),
                                  const QString &tag = QString(), const QString &inputType = QString(),
                                  const QString &outputType = QString(), bool compact = true,
                                  int limit = 0, int cursor = 0,
                                  const SearchToolsFacets &facets = SearchToolsFacets());
    QVariantMap handleGetToolSchema(const QString &toolId);

    // MCP Methods — Agent Interaction Layer
    QVariantMap handleListInteractionTools();
    QVariantMap handleGetInteractionSchema(const QString &toolName);

    // MCP Methods — Spatial workflow & spatial tool layer (ADR 0122)
    QVariantMap handleRunWorkflow(const QVariantMap &arguments);
    QVariantMap handleGetWorkflowStatus(long pipelineId);
    QVariantMap handleResumeWorkflow(const QString &runId);
    QVariantMap handleSpatialToolCall(const QString &toolId, const QVariantMap &parameters);

private:
    /// Set by the initialize handshake; gates other requests with -32002.
    bool m_initialized = false;
    /// Unified tool execution helper routing calls through ToolCallDispatcher
    QVariantMap dispatchToolCall(const QString &toolId, const QVariantMap &parameters, bool isOperatorCall);
    /// Allow-list: rs:, gdal:, gdal_tools:, otb:, qgis:, qgis_algorithms:, opencv:; custom_tools: only with SICNU_MCP_TRUST_CUSTOM_TOOLS=1
    static bool isToolIdAllowed(const QString &toolId, QString *reason = nullptr);
    /// When SICNU_MCP_WORKSPACE is set, reject absolute string params outside that root.
    static bool validateWorkspacePaths(const QVariantMap &parameters, QString *reason = nullptr);
    /// Parses "task-<id>" into @a taskId. Returns false for malformed ids.
    static bool parseExecutionId(const QString &executionId, long *taskId);
    /// Formats a TaskCenter task id as an MCP execution id ("task-<id>").
    static QString toExecutionId(long taskId);

    StdinReader *mReader = nullptr;
    /// TaskCenter sink for rs: operator calls (autoLoad=false; MCP has no canvas).
    sicnu::processing::ToolCallDispatcher mDispatcher;
    QCoreApplication *mApp = nullptr;
    /// Data Manager asset authority for lineage/provenance queries.
    sicnu::data::DataManager *m_dataManager = nullptr;
    /// rpc request id -> TaskCenter task id for in-flight tools/call
    /// executions, so a notifications/cancelled can cancel the mapped task
    /// (#634). Bound: entries are only added, never queried after terminal.
    /// rpc request id (numeric OR string, JSON-RPC 2.0 allows both) -> task
    /// id; entries are removed when consumed by notifications/cancelled and
    /// the map is bounded (#644).
    QHash<QString, long> m_cancelledRequestTasks;
};

#endif // MCP_SERVER_H
