#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QMutex>
#include <QPointer>
#include <memory>
#include <string>
#include <functional>

#include "qgstaskmanager.h"
#include "algorithm_engine.h"
#include "jobs/job_types.h"

namespace sicnu::operators {
class RSOperatorContext;
}

namespace sicnu {

enum class TaskStatus {
    Queued,
    Running,
    Paused,
    Completed,
    Failed,
    Canceled
};

enum class TaskPriority {
    High,
    Normal,
    Low
};

struct AlgorithmTaskInfo {
    long taskId = -1;
    long qgsTaskId = -1;
    QPointer<QgsTask> taskHandle;
    QString algorithmId;
    QString algorithmName;
    TaskStatus status = TaskStatus::Queued;
    TaskPriority priority = TaskPriority::Normal;
    QList<long> parentTaskIds;
    double progressPercentage = 0.0;
    QDateTime startTime;
    QDateTime endTime;
    QVariantMap parameterMap;
    QStringList logBuffer;
    QString errorMessage;
    std::string jobId;
    Json::Value resultPayload;
    sicnu::jobs::JobRequest jobRequest;
    bool hasJobRequest = false;
    bool autoLoadLayer = true;
    QString outputLayerPath;
};

class TaskCenter : public QObject {
    Q_OBJECT
public:
    using JobExecutor = std::function<Json::Value(const sicnu::jobs::JobRequest&,
                                                   sicnu::operators::RSOperatorContext&)>;
    using CancelHook = std::function<void()>;

    static TaskCenter& instance();

    long enqueueTask(const QString& algorithmId,
                     const QVariantMap& params,
                     bool autoLoad = true,
                     TaskPriority priority = TaskPriority::Normal,
                     const QList<long>& parentTaskIds = QList<long>());

    /// Submit a JobEngine request while keeping Task Center as the caller-facing seam.
    long submitJob(const sicnu::jobs::JobRequest& request);
    long submitJob(const sicnu::jobs::JobRequest& request,
                   JobExecutor executor,
                   CancelHook onCancel = {},
                   bool autoLoad = true);

    void attachQgsTask(long taskId, QgsTask* qgsTask);

    bool cancelTask(long taskId);
    bool pauseTask(long taskId);
    bool resumeTask(long taskId);
    bool retryTask(long taskId);

    void updateTaskProgress(long taskId, double progress);
    void appendTaskLog(long taskId, const QString& message);
    void markTaskRunning(long taskId);
    void markTaskCompleted(long taskId,
                           const QVariantMap& results = QVariantMap(),
                           const Json::Value& resultPayload = Json::Value());
    void markTaskFailed(long taskId, const QString& error);
    void markTaskCanceled(long taskId, const QString& reason = QStringLiteral("Task canceled"));

    QList<AlgorithmTaskInfo> allTasks() const;
    AlgorithmTaskInfo getTaskInfo(long taskId) const;
    void clearCompletedTasks();

signals:
    void taskAdded(const AlgorithmTaskInfo& info);
    void taskUpdated(const AlgorithmTaskInfo& info);
    void taskLogAdded(long taskId, const QString& message);
    void layerAutoLoadRequested(const QString& filePath);

private:
    TaskCenter();
    ~TaskCenter() override = default;
    TaskCenter(const TaskCenter&) = delete;
    TaskCenter& operator=(const TaskCenter&) = delete;

    void processNextQueuedTasks();
    QString substitutePlaceholders(const QString& val);
    long submitJobImpl(const sicnu::jobs::JobRequest& request,
                       JobExecutor executor,
                       CancelHook onCancel,
                       bool autoLoad);
    void watchSubmittedJob(long taskId, std::string jobId);

    mutable QMutex m_mutex;
    QMap<long, AlgorithmTaskInfo> m_tasks;
    long m_nextTaskId = 1;
};

} // namespace sicnu

Q_DECLARE_METATYPE(sicnu::AlgorithmTaskInfo)
