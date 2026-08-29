#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QMutex>
#include <QWaitCondition>
#include <QPointer>
#include <memory>
#include <string>
#include <functional>
#include <utility>
#include <vector>

#include "qgstaskmanager.h"
#include "algorithm_engine.h"
#include "jobs/job_types.h"
#include "resource_monitor.h"
#include "task_resource_budget.h"

namespace sicnu::operators {
class RSOperatorContext;
}

namespace sicnu::workflow {
struct WorkflowDefinition;
struct PlaceholderRef;
class WorkflowRun;
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
    Canceled,
    /// Launch-eligible (parents satisfied, autoDispatch) but held back by
    /// resource admission: RAM budget, RSS watermark, profile/global worker
    /// slots. Re-evaluated on every terminal transition; never drops work.
    WaitingResource,
    /// Cancel was requested on a dispatched task; the worker has not yet
    /// reported a terminal record. Transitions to Canceled via the listener.
    Cancelling
};

inline bool isTerminalStatus( TaskStatus status )
{
    return status == TaskStatus::Completed || status == TaskStatus::Failed || status == TaskStatus::Canceled;
}

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
    /// Per-task RAM estimate override (MiB) from the execution plane (e.g. a
    /// preflight- or request-supplied estimate). 0 → use the registry-backed
    /// resolver with its conservative per-class fallback.
    unsigned int resourceEstimateOverrideMb = 0;
    /// Entry tag propagated from the submitting surface ("agent", "mcp",
    /// "gui", "cli", "workflow") into the JobEngine request for provenance.
    QString source;
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

/// Point-in-time resource admission snapshot for a candidate task: the data
/// the ExecutionPlane consults (and exposes to preflight consumers) when
/// deciding whether a submission would launch now or be held in
/// WaitingResource. Pure information — no side effects.
struct TaskAdmissionSnapshot
{
    bool wouldAdmit = false;
    /// True when the RSS watermark alone holds the launch (global hold).
    bool rssHold = false;
    unsigned int candidateMb = 0;  ///< resolved estimate for the candidate
    unsigned int runningMb = 0;    ///< summed estimates of Running tasks
    unsigned int budgetMb = 0;     ///< configured RAM budget cap (0 = disabled)
    unsigned int runningCount = 0; ///< Running + Cancelling tasks
    unsigned int globalLimit = 0;
    QString reason;                ///< human-readable hold reason when !wouldAdmit
};

class TaskCenter : public QObject {
    Q_OBJECT
public:
    using JobExecutor = std::function<Json::Value(const sicnu::jobs::JobRequest&,
                                                   sicnu::operators::RSOperatorContext&)>;
    using CancelHook = std::function<void()>;
    /// Thread-safe terminal notification: invoked EXACTLY ONCE per task, from
    /// the thread that performs the terminal transition (a JobEngine worker
    /// thread, or the thread that called cancel/mark*), always outside
    /// m_mutex. Callbacks must be cheap and lock-free; affinity marshaling is
    /// the callback's responsibility (see ExecutionPlane::watch). This is the
    /// event-loop-independent completion channel the ExecutionPlane uses —
    /// unlike Qt queued delivery it cannot deadlock a sync waiter.
    using TaskCompletionCallback = std::function<void(const AlgorithmTaskInfo&)>;

    static TaskCenter& instance();

    long enqueueTask(const QString& algorithmId,
                     const QVariantMap& params,
                     bool autoLoad = true,
                     TaskPriority priority = TaskPriority::Normal,
                     const QList<long>& parentTaskIds = QList<long>(),
                     bool autoDispatch = false,
                     unsigned int resourceEstimateOverrideMb = 0,
                     const QString& source = QString());

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
    bool cancelPipeline(long pipelineId);
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
    void shutdown();

    /// True once shutdown() has been called (await loops poll this so a
    /// shutdown never leaves a sync waiter blocked until its timeout).
    bool isShuttingDown() const { return m_isShuttingDown.load(); }

    /// Register a terminal-transition callback for @a taskId. Returns a
    /// removal token (> 0), or 0 when the task is unknown. When the task is
    /// ALREADY terminal the callback fires inline immediately on the calling
    /// thread and 0 is returned (nothing left to remove). Exactly-once per
    /// registration: terminal transitions are deduplicated, and fired
    /// callbacks are removed automatically.
    long addTaskCompletionCallback(long taskId, TaskCompletionCallback callback);
    /// Remove a not-yet-fired callback registration (e.g. handle destroyed).
    void removeTaskCompletionCallback(long taskId, long token);

    /// Point-in-time admission snapshot for a candidate (see struct). Pure
    /// query shared by the ExecutionPlane and preflight consumers so
    /// "Preflight → Resource Admission → TaskCenter" consults one logic.
    TaskAdmissionSnapshot admissionSnapshot(const QString& algorithmId,
                                            unsigned int resourceEstimateOverrideMb = 0) const;

    /// Wait for task to reach a terminal status or timeout.
    AlgorithmTaskInfo waitForTask( long taskId,
                                    std::chrono::milliseconds timeout = std::chrono::minutes( 30 ),
                                    std::chrono::milliseconds pollInterval = std::chrono::milliseconds( 10 ) ) const;

    /// Wait for pipeline execution to complete or timeout.
    PipelineExecutionInfo waitForPipeline( long pipelineId,
                                            std::chrono::milliseconds timeout = std::chrono::minutes( 30 ),
                                            std::chrono::milliseconds pollInterval = std::chrono::milliseconds( 10 ) ) const;

    /// Workflow Engine 2.0 run aggregate for a pipeline (ADR 0123, #662):
    /// lifecycle state, per-step plans with fingerprints, progress. Null when
    /// the pipeline predates the wiring or its run could not be created.
    std::shared_ptr<const sicnu::workflow::WorkflowRun>
    workflowRunForPipeline( long pipelineId ) const;

    /// Cap concurrent Running tasks for @a profile (minimum 1). Used by processNextQueuedTasks.
    void setResourceProfileLimit( ProviderResourceProfile profile, unsigned int maxConcurrent );
    unsigned int resourceProfileLimit( ProviderResourceProfile profile ) const;
    /// Restore built-in per-profile defaults (InProcess ≈ hardware_concurrency-1, CLI/Python lower).
    void resetResourceProfileLimits();
    /// Global concurrent Running cap across all profiles (minimum 1).
    void setGlobalConcurrencyLimit( unsigned int maxConcurrent );
    unsigned int globalConcurrencyLimit() const;

    /// Memory watermark (MB) above which new task launches are held until RSS
    /// drops (ADR 0063). 0 disables the gate. Defaults to 75% of system RAM.
    void setMemoryLimitMb( unsigned int mb );
    unsigned int memoryLimitMb() const;
    /// Inject a custom RSS sampler (MB) for tests; {} restores the default.
    void setRssSampler( std::function<unsigned int()> sampler );

    /// Resource-aware scheduling (perf/architecture goal 2026-08-08): the RAM
    /// budget (MiB) gates launch so a FullRaster high-memory operator is not
    /// started alongside others when the budget can't safely hold them. 0
    /// disables the gate (legacy behavior). Defaults to the memory watermark.
    void setResourceBudgetMb( unsigned int mb );
    unsigned int resourceBudgetMb() const;
    /// Inject a custom estimate resolver for tests; {} restores the
    /// registry-backed default resolver.
    void setEstimateResolver( TaskEstimateResolver resolver );
    /// Resolve the RAM estimate (MiB) for an algorithm, applying the
    /// conservative per-class fallback when the operator declares none.
    unsigned int resolveEstimateMb( const std::string &algorithmId ) const;

signals:
    /// Lifecycle notifications are always emitted **outside** m_mutex so slots may
    /// safely re-enter TaskCenter (getTaskInfo, enqueue, cancel, …) without deadlock.
    void taskAdded(const AlgorithmTaskInfo& info);
    void taskUpdated(const AlgorithmTaskInfo& info);
    void taskLogAdded(long taskId, const QString& message);
    void layerAutoLoadRequested(const QString& filePath);

private:
    TaskCenter();
    ~TaskCenter() override;
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
    /// Pop + invoke the completion callbacks registered for a task that just
    /// reached a terminal status. Called WITHOUT m_mutex held; each callback
    /// sees the terminal task snapshot. Exactly-once: registrations are
    /// removed before invocation and terminal transitions are deduplicated.
    void fireTaskCompletionCallbacks( long taskId );
    QList<long> collectTransitiveDescendantsLocked( long rootTaskId ) const;
    static QVariant substituteVariantRecursive( const QVariant &value,
                                                const std::function<std::string( const sicnu::workflow::PlaceholderRef & )> &resolver,
                                                bool *changed = nullptr );
    void applyPlaceholdersForTask(long taskId);
    /// Shared descendant-cascade body for markTaskFailed / markTaskCanceled /
    /// cancelTask. Must be called with m_mutex held. @a userRootId is the
    /// caller-facing root task (gets the "by user" messages) or -1 when the
    /// root was already marked by the caller. @a upstreamCause is "failure" or
    /// "cancellation". @a cleanupScratchOutputs enables cancelTask's
    /// scratch-file removal. Cancelling targets are appended to
    /// @a jobCancelTargets as (jobId, taskId) pairs; attached QgsTask handles
    /// are collected in @a handlesToCancel for thread-marshaled cancellation.
    void cascadeCancelTargetsLocked( const QList<long> &targets, long userRootId,
                                     const QString &upstreamCause, bool cleanupScratchOutputs,
                                     QList<long> &cascadeCanceledIds,
                                     std::vector<std::pair<std::string, long>> &jobCancelTargets,
                                     QList<QPointer<QgsTask>> &handlesToCancel );
    /// Post-lock half of the cascade: marshals attached QgsTask cancellation
    /// to the handle's own thread, asks JobEngine to cancel dispatched jobs,
    /// and finalizes tasks whose job id the engine no longer knows (they
    /// would otherwise strand in Cancelling forever). Must be called without
    /// m_mutex held.
    void dispatchPendingCancels( const QList<QPointer<QgsTask>> &handlesToCancel,
                                 const std::vector<std::pair<std::string, long>> &jobCancelTargets,
                                 const QString &strandedReason );
    void updatePipelineForTaskLocked(long taskId);
    long submitJobImpl(const sicnu::jobs::JobRequest& request,
                       JobExecutor executor,
                       CancelHook onCancel,
                       bool autoLoad);
    /// Owns JobEngine's single listener slot (ADR 0051). Re-installed on every
    /// submit so a test-side reset (shutdownForTests / EngineGuard) cannot
    /// silently detach task bookkeeping; tests driving TaskCenter and
    /// JobEngine in the same process must not install their own listener
    /// while TaskCenter jobs are in flight (the slot replaces, not stacks).
    void ensureJobListener();
    /// JobEngine listener entry: dispatch by jobId, ignore foreign jobs.
    void onJobRecord( const sicnu::jobs::JobRecord &record );
    /// Forward progress/log deltas; on terminal records mark the task.
    void processJobRecord( long taskId, const sicnu::jobs::JobRecord &record );
public:
    static Json::Value variantMapToJsonParams(const QVariantMap& params);
private:
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
    mutable QWaitCondition m_waitCondition;
    std::atomic<bool> m_isShuttingDown{false};
    QMap<long, AlgorithmTaskInfo> m_tasks;
    QMap<long, PipelineExecutionInfo> m_pipelines;
    /// Workflow Engine 2.0 run aggregate per pipeline (ADR 0123 wiring, #662).
    /// Mirrors step statuses and run lifecycle; the legacy PipelineExecutionInfo
    /// above remains the dispatch source of truth, so a v2 hiccup can never
    /// regress production behavior. Exposed read-only via workflowRunForPipeline().
    QMap<long, std::shared_ptr<sicnu::workflow::WorkflowRun>> m_pipelineRuns;
    QList<PendingLaunch> m_pendingLaunches;
    QList<AlgorithmTaskInfo> m_pendingTaskAdded;
    QList<AlgorithmTaskInfo> m_pendingTaskUpdated;
    QList<PendingLog> m_pendingLogs;
    QMap<std::string, long> m_taskByJobId; ///< jobId → taskId, listener dispatch (ADR 0051)
    QMap<long, std::size_t> m_forwardedLogCounts; ///< per-task log dedup key (logLines.size())
    QMap<long, double> m_lastForwardedProgress; ///< per-task progress dedup
    QMap<long, QMap<long, TaskCompletionCallback>> m_completionCallbacks; ///< per-task terminal callbacks
    long m_nextCompletionToken = 1;
    QMap<ProviderResourceProfile, unsigned int> m_profileLimits; ///< empty entry → use defaultLimitForProfile
    unsigned int m_globalConcurrencyLimit = 0; ///< 0 → hardware_concurrency()-1 (min 1)
    long m_nextTaskId = 1;
    long m_nextPipelineId = 1;
    ResourceMonitor m_resourceMonitor;
    TaskResourceBudget m_resourceBudget;
    /// Installs the registry-backed estimate resolver (idempotent). Called once
    /// from the constructor; re-installed if a test clears it via {}.
    void installDefaultEstimateResolver();
    /// Per-task resolved estimate: the task's override when set, else the
    /// budget resolver (registry estimate + conservative class fallback).
    unsigned int taskEstimateMbLocked( const AlgorithmTaskInfo &task ) const;

    // --- Workflow Engine 2.0 wiring (ADR 0123, #662) -------------------------
    /// Creates the WorkflowRun aggregate for a pipeline and builds its step
    /// plans (topological-order fingerprints: operator id + canonical params
    /// + parent-step derivation revisions). Defensive: failures leave the
    /// pipeline running on the legacy path only.
    void attachWorkflowRunLocked( long pipelineId,
                                  const sicnu::workflow::WorkflowDefinition &def,
                                  const std::vector<std::string> &orderedStepIds );
    /// Mirrors a task status transition into the pipeline's run aggregate
    /// (step plan status, checkpoint save). No-op when the pipeline has no run.
    void mirrorStepToRunLocked( long pipelineId, const std::string &stepId,
                                TaskStatus status );
    /// Transitions the pipeline's run aggregate to its terminal state once
    /// every step is terminal (Running -> Completed/Failed + final checkpoint).
    void finalizeWorkflowRunLocked( long pipelineId, bool failed,
                                    const QString &errorMessage );
};

} // namespace sicnu

Q_DECLARE_METATYPE(sicnu::AlgorithmTaskInfo)
