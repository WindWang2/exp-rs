#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <QObject>
#include <QVariantMap>
#include <QThread>
#include <QMutex>
#include <QMap>
#include <QPointer>
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
signals:
    void lineRead(const QString &line);
protected:
    void run() override;
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
    // MCP Methods
    QVariantMap handleListAlgorithms();
    QVariantMap handleGetAlgorithmSchema(const QString &algorithmId);
    QVariantMap handleExecuteAlgorithm(const QString &algorithmId, const QVariantMap &parameters);
    QVariantMap handleGetExecutionStatus(const QString &executionId);
    QVariantMap handleCancelExecution(const QString &executionId);
    QVariantMap handleListLayers();
    QVariantMap handleDescribeDataset(const QString &layerId);

private:
    StdinReader *mReader = nullptr;
    QMap<QString, std::shared_ptr<AlgorithmExecution>> mExecutions;
    QMutex mMutex;
    int mExecutionCounter = 0;
    QCoreApplication *mApp = nullptr;
};

#endif // MCP_SERVER_H
