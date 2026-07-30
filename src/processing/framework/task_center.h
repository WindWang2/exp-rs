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

namespace sicnu::workflow {
struct WorkflowDefinition;
}

namespace sicnu {

using JobExecutor = std::function<Json::Value(const sicnu::jobs::JobRequest&,
                                               sicnu::operators::RSOperatorContext&)>;

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
    JobExecutor jobExecutor;
    bool autoLoadLayer = true;
    /// When true, processNextQueuedTasks submits the task to JobEngine once parents complete.
    bool autoDispatch = false;
    /// Scheduling class used by resource-profile throttling (ADR Algorithm Provider Adapter).
    /// Resolved from AlgorithmEngine when the algorithm is registered; otherwise
    /// defaults to InProcessThread.
    ProviderResourceProfile resourceProfile = ProviderResourceProfile::InProcessThread;
    QString outputLayerPath;
    QString stepId;
    long pipelineId = -1;
};

struct PipelineExecutionInfo {
    long pipelineId = -1;
    QString definitionId;
    std::vector<std::string> orderedStepIds;
    QMap<std::string, long> stepToTaskId;
    QMap<long, std::string> taskToStepId;
    QMap<std::string, TaskStatus> stepStatuses;
    bool isCompleted = false;
    bool isFailed = false;
    QString errorMessage;
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
                     const QList<long>& parentTaskIds = QList<long>(),
                     bool autoDispatch = false);

    long enqueueToolCall( const std::string &jsonToolCall,
                          bool autoLoad = true,
                          TaskPriority priority = TaskPriority::Normal );

    /// Submit a DAG Task Pipeline for execution (auto-dispatched via JobEngine).
    long submitPipeline( const sicnu::workflow::WorkflowDefinition &def, bool autoLoad = true );
    long submitPipelineJson( const std::string &jsonPipeline, bool autoLoad = true );

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
    PipelineExecutionInfo getPipelineInfo(long pipelineId) const;
    void clearCompletedTasks();

    /// Cap concurrent Running tasks for @a profile (minimum 1). Used by processNextQueuedTasks.
    void setResourceProfileLimit( ProviderResourceProfile profile, unsigned int maxConcurrent );
    unsigned int resourceProfileLimit( ProviderResourceProfile profile ) const;
    /// Restore built-in per-profile defaults (InProcess ≈ hardware_concurrency-1, CLI/Python lower).
    void resetResourceProfileLimits();
    /// Global concurrent Running cap across all profiles (minimum 1).
    void setGlobalConcurrencyLimit( unsigned int maxConcurrent );
    unsigned int globalConcurrencyLimit() const;

signals:
    /// Lifecycle notifications are always emitted **outside** m_mutex so slots may
    /// safely re-enter TaskCenter (getTaskInfo, enqueue, cancel, …) without deadlock.
    void taskAdded(const AlgorithmTaskInfo& info);
    void taskUpdated(const AlgorithmTaskInfo& info);
    void taskLogAdded(long taskId, const QString& message);
    void layerAutoLoadRequested(const QString& filePath);

private:
    TaskCenter();
    ~TaskCenter() override = default;
    TaskCenter(const TaskCenter&) = delete;
    TaskCenter& operator=(const TaskCenter&) = delete;

    /// Must be called with m_mutex held. Stages auto-dispatch work into m_pendingLaunches.
    void processNextQueuedTasks();
    /// Must be called without m_mutex held.
    void flushPendingLaunches();
    /// Queue signal payloads while holding m_mutex (copies task snapshot).
    void queueTaskAddedLocked( long taskId );
    void queueTaskUpdatedLocked( long taskId );
    void queueTaskLogLocked( long taskId, const QString &message );
    /// Drain queued signals; never holds m_mutex across emit. Safe if slots re-enter.
    void flushPendingSignals();
    void applyPlaceholdersForTask(long taskId);
    void updatePipelineForTaskLocked(long taskId);
    long submitJobImpl(const sicnu::jobs::JobRequest& request,
                       JobExecutor executor,
                       CancelHook onCancel,
                       bool autoLoad);
    void watchSubmittedJob(long taskId, std::string jobId);
    static Json::Value variantMapToJsonParams(const QVariantMap& params);
    ProviderResourceProfile resolveResourceProfile( const QString &algorithmId ) const;
    unsigned int defaultLimitForProfile( ProviderResourceProfile profile ) const;
    unsigned int limitForProfileLocked( ProviderResourceProfile profile ) const;

    struct PendingLaunch {
        long taskId = -1;
        sicnu::jobs::JobRequest request;
        JobExecutor executor;
        bool hasExecutor = false;
    };

    struct PendingLog {
        long taskId = -1;
        QString message;
    };

    mutable QMutex m_mutex;
    QMap<long, AlgorithmTaskInfo> m_tasks;
    QMap<long, PipelineExecutionInfo> m_pipelines;
    QList<PendingLaunch> m_pendingLaunches;
    QList<AlgorithmTaskInfo> m_pendingTaskAdded;
    QList<AlgorithmTaskInfo> m_pendingTaskUpdated;
    QList<PendingLog> m_pendingLogs;
    QMap<ProviderResourceProfile, unsigned int> m_profileLimits; ///< empty entry → use defaultLimitForProfile
    unsigned int m_globalConcurrencyLimit = 0; ///< 0 → hardware_concurrency()-1 (min 1)
    long m_nextTaskId = 1;
    long m_nextPipelineId = 1;
};

} // namespace sicnu

Q_DECLARE_METATYPE(sicnu::AlgorithmTaskInfo)
