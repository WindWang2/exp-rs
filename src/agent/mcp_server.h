#ifndef MCP_SERVER_H
#define MCP_SERVER_H

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

private:
    void handleRequest(const QVariantMap &request);
    void sendResponse(const QVariant &id, const QVariantMap &result);
    void sendError(const QVariant &id, int code, const QString &message);
    void sendNotification(const QString &method, const QVariantMap &params);

protected:
    // MCP Methods — QgsProcessing algorithms (legacy Agent surface)
    QVariantMap handleListAlgorithms();
    QVariantMap handleGetAlgorithmSchema(const QString &algorithmId);
    QVariantMap handleExecuteAlgorithm(const QString &algorithmId, const QVariantMap &parameters);
    QVariantMap handleGetExecutionStatus(const QString &executionId);
    QVariantMap handleCancelExecution(const QString &executionId);
    QVariantMap handleListLayers();
    QVariantMap handleDescribeDataset(const QString &layerId);
    QVariantMap handleGetLineage(const QString &assetId);

    // MCP Methods — RSOperator kernel (preferred Agent surface)
    QVariantMap handleListOperators();
    QVariantMap handleGetOperatorSchema(const QString &operatorId);
    QVariantMap handleExecuteOperator(const QString &operatorId, const QVariantMap &parameters);

private:
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
};

#endif // MCP_SERVER_H
