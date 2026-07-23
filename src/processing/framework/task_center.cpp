#include "task_center.h"

#include <QMutexLocker>
#include <QRegularExpression>
#include <qgsapplication.h>
#include <thread>
#include <algorithm>
#include <chrono>
#include "jobs/job_engine.h"

namespace sicnu {

TaskCenter& TaskCenter::instance()
{
    static TaskCenter s_instance;
    return s_instance;
}

TaskCenter::TaskCenter()
{
    qRegisterMetaType<AlgorithmTaskInfo>("sicnu::AlgorithmTaskInfo");
}

long TaskCenter::enqueueTask(const QString& algorithmId,
                             const QVariantMap& params,
                             bool autoLoad,
                             TaskPriority priority,
                             const QList<long>& parentTaskIds)
{
    QMutexLocker locker(&m_mutex);
    long id = m_nextTaskId++;
    AlgorithmTaskInfo info;
    info.taskId = id;
    info.algorithmId = algorithmId;
    info.priority = priority;
    info.parentTaskIds = parentTaskIds;

    auto adapter = AlgorithmEngine::instance().findAlgorithm(algorithmId);
    if (adapter) {
        info.algorithmName = adapter->descriptor().name;
    } else {
        info.algorithmName = algorithmId;
    }

    info.status = TaskStatus::Queued;
    info.progressPercentage = 0.0;
    info.startTime = QDateTime::currentDateTime();
    info.parameterMap = params;
    info.autoLoadLayer = autoLoad;

    // Detect output path from parameters if present
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.key().contains(QStringLiteral("OUTPUT"), Qt::CaseInsensitive) ||
            it.key().contains(QStringLiteral("RESULT"), Qt::CaseInsensitive)) {
            info.outputLayerPath = it.value().toString();
            break;
        }
    }

    info.logBuffer.append(QString(QStringLiteral("[%1] Task queued with priority %2."))
                          .arg(info.startTime.toString(QStringLiteral("hh:mm:ss")))
                          .arg(static_cast<int>(priority)));

    m_tasks.insert(id, info);
    emit taskAdded(info);

    processNextQueuedTasks();
    return id;
}

long TaskCenter::submitJob(const sicnu::jobs::JobRequest& request)
{
    return submitJobImpl(request, {}, {});
}

long TaskCenter::submitJob(const sicnu::jobs::JobRequest& request,
                           JobExecutor executor,
                           CancelHook onCancel)
{
    return submitJobImpl(request, std::move(executor), std::move(onCancel));
}

long TaskCenter::submitJobImpl(const sicnu::jobs::JobRequest& request,
                               JobExecutor executor,
                               CancelHook onCancel)
{
    QVariantMap params;
    for (const auto& name : request.params.getMemberNames()) {
        const Json::Value& value = request.params[name];
        if (value.isString())
            params.insert(QString::fromStdString(name), QString::fromStdString(value.asString()));
        else if (value.isBool())
            params.insert(QString::fromStdString(name), value.asBool());
        else if (value.isNumeric())
            params.insert(QString::fromStdString(name), value.asDouble());
    }

    const long taskId = enqueueTask(QString::fromStdString(request.algorithmId), params);
    const std::string jobId = executor
        ? sicnu::jobs::JobEngine::instance().submit(request, std::move(executor), std::move(onCancel))
        : sicnu::jobs::JobEngine::instance().submit(request);
    if (jobId.empty()) {
        markTaskFailed(taskId, QStringLiteral("Task Center could not submit the job"));
        return taskId;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_tasks.contains(taskId)) {
            m_tasks[taskId].jobId = jobId;
            m_tasks[taskId].jobRequest = request;
            m_tasks[taskId].hasJobRequest = true;
        }
    }

    markTaskRunning(taskId);
    watchSubmittedJob(taskId, jobId);
    return taskId;
}

void TaskCenter::watchSubmittedJob(long taskId, std::string jobId)
{
    std::thread([taskId, jobId = std::move(jobId)]() {
        auto& engine = sicnu::jobs::JobEngine::instance();
        std::size_t forwardedLogCount = 0;
        double lastProgress = -2.0;
        for (;;) {
            const auto record = engine.snapshot(jobId);
            if (!record) {
                TaskCenter::instance().markTaskFailed(taskId, QStringLiteral("Task Center lost the job record"));
                return;
            }
            if (record->progress >= 0.0 && record->progress != lastProgress) {
                TaskCenter::instance().updateTaskProgress(taskId, record->progress);
                lastProgress = record->progress;
            }
            while (forwardedLogCount < record->logLines.size()) {
                TaskCenter::instance().appendTaskLog(
                    taskId, QString::fromStdString(record->logLines[forwardedLogCount].text));
                ++forwardedLogCount;
            }
            if (record && (record->state == sicnu::jobs::JobState::Succeeded
                           || record->state == sicnu::jobs::JobState::Failed
                           || record->state == sicnu::jobs::JobState::Cancelled)) {
                if (record->state == sicnu::jobs::JobState::Succeeded) {
                    QVariantMap results;
                    for (const auto& name : record->result.getMemberNames())
                        results.insert(QString::fromStdString(name), QString::fromStdString(record->result[name].asString()));
                    TaskCenter::instance().markTaskCompleted(taskId, results, record->result);
                } else if (record->state == sicnu::jobs::JobState::Cancelled) {
                    TaskCenter::instance().markTaskCanceled(taskId);
                } else {
                    TaskCenter::instance().markTaskFailed(taskId, QString::fromStdString(record->error));
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }).detach();
}

void TaskCenter::processNextQueuedTasks()
{
    // Max concurrent background tasks bounded to hardware_concurrency() - 1 (min 1)
    unsigned int maxThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    unsigned int runningCount = 0;

    for (const auto& t : m_tasks) {
        if (t.status == TaskStatus::Running) {
            runningCount++;
        }
    }

    if (runningCount >= maxThreads) {
        return;
    }

    // Find candidates in Queued status whose parentTaskIds are all Completed
    QList<long> eligibleIds;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().status != TaskStatus::Queued) continue;

        bool parentsSatisfied = true;
        for (long parentId : it.value().parentTaskIds) {
            if (m_tasks.contains(parentId)) {
                if (m_tasks[parentId].status != TaskStatus::Completed) {
                    parentsSatisfied = false;
                    break;
                }
            }
        }

        if (parentsSatisfied) {
            eligibleIds.append(it.key());
        }
    }

    // Sort eligible tasks by priority (High < Normal < Low in enum order: 0, 1, 2)
    std::sort(eligibleIds.begin(), eligibleIds.end(), [this](long a, long b) {
        if (m_tasks[a].priority != m_tasks[b].priority) {
            return static_cast<int>(m_tasks[a].priority) < static_cast<int>(m_tasks[b].priority);
        }
        return m_tasks[a].taskId < m_tasks[b].taskId;
    });

    // Substitute placeholders for the top eligible tasks
    for (long id : eligibleIds) {
        if (runningCount >= maxThreads) break;

        // Perform placeholder substitution: ${task.<parent_id>.output}
        QVariantMap& pMap = m_tasks[id].parameterMap;
        for (auto pIt = pMap.begin(); pIt != pMap.end(); ++pIt) {
            if (pIt.value().canConvert<QString>()) {
                QString valStr = pIt.value().toString();
                for (long parentId : m_tasks[id].parentTaskIds) {
                    if (m_tasks.contains(parentId)) {
                        QString pattern = QString(QStringLiteral("${task.%1.output}")).arg(parentId);
                        QString defaultPattern = QStringLiteral("${task.parent.output}");
                        if (valStr.contains(pattern)) {
                            valStr.replace(pattern, m_tasks[parentId].outputLayerPath);
                        } else if (valStr.contains(defaultPattern)) {
                            valStr.replace(defaultPattern, m_tasks[parentId].outputLayerPath);
                        }
                    }
                }
                *pIt = valStr;
            }
        }
    }
}

void TaskCenter::attachQgsTask(long taskId, QgsTask* qgsTask)
{
    QMutexLocker locker(&m_mutex);
    if (m_tasks.contains(taskId) && qgsTask) {
        m_tasks[taskId].taskHandle = qgsTask;
    }
}

void TaskCenter::updateTaskProgress(long taskId, double progress)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId) || m_tasks[taskId].status == TaskStatus::Canceled) return;
    m_tasks[taskId].progressPercentage = progress;
    m_tasks[taskId].status = TaskStatus::Running;
    emit taskUpdated(m_tasks[taskId]);
}

void TaskCenter::appendTaskLog(long taskId, const QString& message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId)) return;
    m_tasks[taskId].logBuffer.append(message);
    emit taskLogAdded(taskId, message);
}

void TaskCenter::markTaskRunning(long taskId)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId) || m_tasks[taskId].status == TaskStatus::Canceled) return;
    m_tasks[taskId].status = TaskStatus::Running;
    emit taskUpdated(m_tasks[taskId]);
}

void TaskCenter::markTaskCompleted(long taskId,
                                   const QVariantMap& results,
                                   const Json::Value& resultPayload)
{
    QString autoLoadPath;
    bool shouldAutoLoad = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_tasks.contains(taskId) || m_tasks[taskId].status == TaskStatus::Canceled) return;
        m_tasks[taskId].status = TaskStatus::Completed;
        m_tasks[taskId].resultPayload = resultPayload;
        m_tasks[taskId].progressPercentage = 1.0;
        m_tasks[taskId].endTime = QDateTime::currentDateTime();
        m_tasks[taskId].logBuffer.append(QString(QStringLiteral("[%1] Task completed successfully."))
                                          .arg(m_tasks[taskId].endTime.toString(QStringLiteral("hh:mm:ss"))));

        if (m_tasks[taskId].outputLayerPath.isEmpty() && !results.isEmpty()) {
            for (auto it = results.begin(); it != results.end(); ++it) {
                if (it.value().canConvert<QString>()) {
                    m_tasks[taskId].outputLayerPath = it.value().toString();
                    break;
                }
            }
        }

        shouldAutoLoad = m_tasks[taskId].autoLoadLayer && !m_tasks[taskId].outputLayerPath.isEmpty();
        autoLoadPath = m_tasks[taskId].outputLayerPath;

        emit taskUpdated(m_tasks[taskId]);
        processNextQueuedTasks();
    }

    if (shouldAutoLoad && !autoLoadPath.isEmpty()) {
        emit layerAutoLoadRequested(autoLoadPath);
    }
}

void TaskCenter::markTaskFailed(long taskId, const QString& error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId) || m_tasks[taskId].status == TaskStatus::Canceled) return;
    m_tasks[taskId].status = TaskStatus::Failed;
    m_tasks[taskId].errorMessage = error;
    m_tasks[taskId].endTime = QDateTime::currentDateTime();
    m_tasks[taskId].logBuffer.append(QString(QStringLiteral("[%1] Task failed: %2"))
                                      .arg(m_tasks[taskId].endTime.toString(QStringLiteral("hh:mm:ss")), error));
    emit taskUpdated(m_tasks[taskId]);

    // Cascade failure to downstream child tasks
    QList<long> keys = m_tasks.keys();
    for (long id : keys) {
        if (m_tasks[id].parentTaskIds.contains(taskId) && m_tasks[id].status == TaskStatus::Queued) {
            m_tasks[id].status = TaskStatus::Canceled;
            m_tasks[id].endTime = QDateTime::currentDateTime();
            m_tasks[id].logBuffer.append(QStringLiteral("Canceled due to upstream parent task failure."));
            emit taskUpdated(m_tasks[id]);
        }
    }

    processNextQueuedTasks();
}

void TaskCenter::markTaskCanceled(long taskId, const QString& reason)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId) || m_tasks[taskId].status == TaskStatus::Canceled) return;
    m_tasks[taskId].status = TaskStatus::Canceled;
    m_tasks[taskId].errorMessage = reason;
    m_tasks[taskId].endTime = QDateTime::currentDateTime();
    m_tasks[taskId].logBuffer.append(QString(QStringLiteral("[%1] %2"))
                                      .arg(m_tasks[taskId].endTime.toString(QStringLiteral("hh:mm:ss")), reason));
    emit taskUpdated(m_tasks[taskId]);
    processNextQueuedTasks();
}

bool TaskCenter::cancelTask(long taskId)
{
    std::string jobId;
    bool cancelImmediately = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_tasks.contains(taskId)) {
            return false;
        }
        if (m_tasks[taskId].status == TaskStatus::Completed
            || m_tasks[taskId].status == TaskStatus::Failed
            || m_tasks[taskId].status == TaskStatus::Canceled) {
            return false;
        }
        if (m_tasks[taskId].taskHandle) {
            m_tasks[taskId].taskHandle->cancel();
        }
        jobId = m_tasks[taskId].jobId;
        cancelImmediately = jobId.empty();
        if (cancelImmediately) {
            m_tasks[taskId].status = TaskStatus::Canceled;
            m_tasks[taskId].errorMessage = QStringLiteral("Task canceled");
            m_tasks[taskId].endTime = QDateTime::currentDateTime();
            m_tasks[taskId].logBuffer.append(QStringLiteral("Task canceled by user."));
        } else {
            // A callable worker may still be reading its task-owned state.
            // Publish the terminal Canceled state only once JobEngine reports
            // that the worker has actually observed cancellation and exited.
            m_tasks[taskId].logBuffer.append(QStringLiteral("Cancellation requested by user."));
        }
        emit taskUpdated(m_tasks[taskId]);

        if (cancelImmediately) {
            // Cascade only after this task has a terminal cancellation state.
            QList<long> keys = m_tasks.keys();
            for (long id : keys) {
                if (m_tasks[id].parentTaskIds.contains(taskId) && m_tasks[id].status == TaskStatus::Queued) {
                    m_tasks[id].status = TaskStatus::Canceled;
                    m_tasks[id].endTime = QDateTime::currentDateTime();
                    m_tasks[id].logBuffer.append(QStringLiteral("Canceled due to upstream parent task failure."));
                    emit taskUpdated(m_tasks[id]);
                }
            }
        }

        processNextQueuedTasks();
    }
    if (!jobId.empty())
        sicnu::jobs::JobEngine::instance().cancel(jobId);
    return true;
}

bool TaskCenter::pauseTask(long taskId)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId)) {
        return false;
    }
    if (m_tasks[taskId].status == TaskStatus::Running) {
        if (m_tasks[taskId].taskHandle) {
            m_tasks[taskId].taskHandle->hold();
        }
        m_tasks[taskId].status = TaskStatus::Paused;
        m_tasks[taskId].logBuffer.append(QStringLiteral("Task paused."));
        emit taskUpdated(m_tasks[taskId]);
        return true;
    }
    return false;
}

bool TaskCenter::resumeTask(long taskId)
{
    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(taskId)) {
        return false;
    }
    if (m_tasks[taskId].status == TaskStatus::Paused) {
        if (m_tasks[taskId].taskHandle) {
            m_tasks[taskId].taskHandle->unhold();
        }
        m_tasks[taskId].status = TaskStatus::Running;
        m_tasks[taskId].logBuffer.append(QStringLiteral("Task resumed."));
        emit taskUpdated(m_tasks[taskId]);
        return true;
    }
    return false;
}

bool TaskCenter::retryTask(long taskId)
{
    AlgorithmTaskInfo oldInfo;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_tasks.contains(taskId)) {
            return false;
        }
        oldInfo = m_tasks[taskId];
    }
    if (oldInfo.hasJobRequest)
        return submitJob(oldInfo.jobRequest) > 0;
    return enqueueTask(oldInfo.algorithmId, oldInfo.parameterMap, oldInfo.autoLoadLayer, oldInfo.priority, oldInfo.parentTaskIds) > 0;
}

QList<AlgorithmTaskInfo> TaskCenter::allTasks() const
{
    QMutexLocker locker(&m_mutex);
    return m_tasks.values();
}

AlgorithmTaskInfo TaskCenter::getTaskInfo(long taskId) const
{
    QMutexLocker locker(&m_mutex);
    return m_tasks.value(taskId);
}

void TaskCenter::clearCompletedTasks()
{
    QMutexLocker locker(&m_mutex);
    QList<long> keys = m_tasks.keys();
    for (long id : keys) {
        if (m_tasks[id].status == TaskStatus::Completed ||
            m_tasks[id].status == TaskStatus::Failed ||
            m_tasks[id].status == TaskStatus::Canceled) {
            m_tasks.remove(id);
        }
    }
}

} // namespace sicnu
