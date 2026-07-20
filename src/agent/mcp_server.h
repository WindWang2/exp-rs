#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <QObject>
#include <QVariantMap>
#include <QThread>
#include <QMutex>
#include <QMap>
#include <QPointer>
#include <atomic>
#include <memory>
#include <QCoreApplication>

class QgsProcessingAlgorithm;
class QgsProcessingContext;
class QgsProcessingFeedback;

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

struct AlgorithmExecution {
    QString id;
    QString algorithmId;
    mutable QMutex mutex;
    double progress = 0;
    QString progressText;
    QString error;
    bool completed = false;
    bool canceled = false;
    QVariantMap result;
};

class AlgorithmWorker : public QThread
{
    Q_OBJECT
public:
    AlgorithmWorker(const QString &execId, QgsProcessingAlgorithm *algo, const QVariantMap &parameters, std::shared_ptr<AlgorithmExecution> execState, QObject *parent = nullptr);
    ~AlgorithmWorker() override;

protected:
    void run() override;

private:
    QString mExecId;
    std::unique_ptr<QgsProcessingAlgorithm> mAlgo;
    QVariantMap mParameters;
    std::shared_ptr<AlgorithmExecution> mState;
};

class McpServer : public QObject
{
    Q_OBJECT
public:
    McpServer(QObject *parent = nullptr);
    ~McpServer();

    void start(QCoreApplication *app);

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

    // MCP Methods — RSOperator kernel (preferred Agent surface)
    QVariantMap handleListOperators();
    QVariantMap handleGetOperatorSchema(const QString &operatorId);
    QVariantMap handleExecuteOperator(const QString &operatorId, const QVariantMap &parameters);

private:
    /// Allow-list: rs:, gdal:, gdal_tools:, otb:; custom_tools: only with SICNU_MCP_TRUST_CUSTOM_TOOLS=1
    static bool isAlgorithmIdAllowed(const QString &algorithmId, QString *reason = nullptr);
    /// Same allow-list for operator ids (rs:, gdal:, otb:, opencv: prefix family).
    static bool isOperatorIdAllowed(const QString &operatorId, QString *reason = nullptr);
    /// When SICNU_MCP_WORKSPACE is set, reject absolute string params outside that root.
    static bool validateWorkspacePaths(const QVariantMap &parameters, QString *reason = nullptr);

    StdinReader *mReader = nullptr;
    QMap<QString, std::shared_ptr<AlgorithmExecution>> mExecutions;
    QMap<QString, std::shared_ptr<std::atomic<bool>>> mOperatorCancelFlags;
    QMutex mMutex;
    int mExecutionCounter = 0;
    QCoreApplication *mApp = nullptr;
};

#endif // MCP_SERVER_H
