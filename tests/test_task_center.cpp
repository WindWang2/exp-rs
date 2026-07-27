#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_center.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <chrono>
#include <atomic>
#include <thread>

TEST_CASE("TaskCenter - Enqueue, Info Query, and Lifecycle State Transitions", "[processing][task_center]") {
    auto& center = sicnu::TaskCenter::instance();

    QVariantMap params;
    params.insert(QStringLiteral("param1"), QStringLiteral("value1"));

    long taskId = center.enqueueTask(QStringLiteral("test_algo"), params, true);
    REQUIRE(taskId > 0);

    auto info = center.getTaskInfo(taskId);
    REQUIRE(info.taskId == taskId);
    REQUIRE(info.algorithmId == QStringLiteral("test_algo"));
    REQUIRE(info.status == sicnu::TaskStatus::Queued);
    REQUIRE(info.parameterMap.value(QStringLiteral("param1")).toString() == QStringLiteral("value1"));
    REQUIRE_FALSE(info.logBuffer.isEmpty());

    // Cancel task
    bool canceled = center.cancelTask(taskId);
    REQUIRE(canceled);

    auto updatedInfo = center.getTaskInfo(taskId);
    REQUIRE(updatedInfo.status == sicnu::TaskStatus::Canceled);

    // Retry task
    bool retried = center.retryTask(taskId);
    REQUIRE(retried);

    // Clear completed tasks
    center.clearCompletedTasks();
}

TEST_CASE("TaskCenter - Priority Queueing and DAG Parent Dependency Gating", "[processing][task_center]") {
    auto& center = sicnu::TaskCenter::instance();

    QVariantMap parentParams;
    parentParams.insert(QStringLiteral("OUTPUT"), QStringLiteral("/tmp/parent_output.tif"));

    long parentId = center.enqueueTask(QStringLiteral("parent_algo"), parentParams, true, sicnu::TaskPriority::Normal);
    REQUIRE(parentId > 0);

    QVariantMap childParams;
    childParams.insert(QStringLiteral("INPUT"), QStringLiteral("${task.") + QString::number(parentId) + QStringLiteral(".output}"));

    QList<long> parentIds = { parentId };
    long childId = center.enqueueTask(QStringLiteral("child_algo"), childParams, true, sicnu::TaskPriority::High, parentIds);
    REQUIRE(childId > 0);

    // Child task is initially queued and waiting for parent
    auto childInfo = center.getTaskInfo(childId);
    REQUIRE(childInfo.status == sicnu::TaskStatus::Queued);

    // Mark parent completed
    center.markTaskCompleted(parentId);

    // Verify parent is completed
    auto parentInfo = center.getTaskInfo(parentId);
    REQUIRE(parentInfo.status == sicnu::TaskStatus::Completed);
}

TEST_CASE("TaskCenter - Upstream Failure Cascade Cancellation", "[processing][task_center]") {
    auto& center = sicnu::TaskCenter::instance();

    QVariantMap parentParams;
    long parentId = center.enqueueTask(QStringLiteral("parent_fail_algo"), parentParams, true);

    QVariantMap childParams;
    QList<long> parentIds = { parentId };
    long childId = center.enqueueTask(QStringLiteral("child_fail_algo"), childParams, true, sicnu::TaskPriority::Normal, parentIds);

    // Mark parent failed
    center.markTaskFailed(parentId, QStringLiteral("Simulated failure"));

    // Child task should be automatically canceled due to upstream failure
    auto childInfo = center.getTaskInfo(childId);
    REQUIRE(childInfo.status == sicnu::TaskStatus::Canceled);
    REQUIRE(childInfo.logBuffer.last().contains(QStringLiteral("upstream parent task failure")));
}

TEST_CASE("TaskCenter - Executes an Algorithm Task through JobEngine", "[processing][task_center]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();

    sicnu::jobs::JobRequest request;
    request.algorithmId = "test:missing";
    request.title = "Task Center tracer";
    request.source = "task_panel";

    const long taskId = sicnu::TaskCenter::instance().submitJob(request);

    REQUIRE(taskId > 0);
    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 20
                      && sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Running;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Failed);
}

TEST_CASE("TaskCenter - Preserves a submitted job result", "[processing][task_center]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    const auto taskCountBefore = sicnu::TaskCenter::instance().allTasks().size();
    engine.registerExecutor("test:success", [](const sicnu::jobs::JobRequest&, sicnu::operators::RSOperatorContext& context) {
        context.logInfo("tracer completed");
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/task-center-result.tif";
        return result;
    });

    sicnu::jobs::JobRequest request;
    request.algorithmId = "test:success";
    request.source = "task_panel";
    const long taskId = sicnu::TaskCenter::instance().submitJob(request);

    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 20
                      && sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Running;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
    REQUIRE(sicnu::TaskCenter::instance().allTasks().size() == taskCountBefore + 1);
    REQUIRE(info.status == sicnu::TaskStatus::Completed);
    REQUIRE(info.resultPayload["output"].asString() == "/tmp/task-center-result.tif");
    REQUIRE(info.outputLayerPath == QStringLiteral("/tmp/task-center-result.tif"));
    REQUIRE(info.logBuffer.join('\n').contains(QStringLiteral("tracer completed")));
}

TEST_CASE("TaskCenter - Retry preserves a submitted job's auto-load preference", "[processing][task_center][retry]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.registerExecutor("module:classify:postprocess", [](const sicnu::jobs::JobRequest&,
                                                                 sicnu::operators::RSOperatorContext&) {
        return Json::Value(Json::objectValue);
    });

    sicnu::jobs::JobRequest request;
    request.algorithmId = "module:classify:postprocess";
    request.source = "classification";
    const long taskId = sicnu::TaskCenter::instance().submitJob(
        request, sicnu::TaskCenter::JobExecutor{}, {}, false);

    REQUIRE(taskId > 0);
    REQUIRE_FALSE(sicnu::TaskCenter::instance().getTaskInfo(taskId).autoLoadLayer);
    engine.waitUntilIdleForTests();
    REQUIRE(sicnu::TaskCenter::instance().retryTask(taskId));

    const auto tasks = sicnu::TaskCenter::instance().allTasks();
    REQUIRE(tasks.size() >= 2);
    const auto retriedInfo = tasks.last();
    REQUIRE(retriedInfo.taskId != taskId);
    REQUIRE(retriedInfo.algorithmId == QStringLiteral("module:classify:postprocess"));
    REQUIRE_FALSE(retriedInfo.autoLoadLayer);
}

TEST_CASE("TaskCenter - Executes a callable job through the same task seam", "[processing][task_center][callable]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    const auto taskCountBefore = sicnu::TaskCenter::instance().allTasks().size();

    sicnu::jobs::JobRequest request;
    request.algorithmId = "callable:dialog-test";
    request.source = "dialog";

    const long taskId = sicnu::TaskCenter::instance().submitJob(
        request,
        [](const sicnu::jobs::JobRequest&, sicnu::operators::RSOperatorContext&) {
            Json::Value result(Json::objectValue);
            result["output"] = "/tmp/callable-task-center-result.tif";
            return result;
        });

    REQUIRE(taskId > 0);
    REQUIRE(sicnu::TaskCenter::instance().allTasks().size() == taskCountBefore + 1);
    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 20
                      && sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Running;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
    REQUIRE(info.status == sicnu::TaskStatus::Completed);
    REQUIRE(info.resultPayload["output"].asString() == "/tmp/callable-task-center-result.tif");
}

TEST_CASE("TaskCenter - Cancels the underlying submitted job", "[processing][task_center]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.registerExecutor("test:cancel", [](const sicnu::jobs::JobRequest&, sicnu::operators::RSOperatorContext& context) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            context.throwIfCancelled();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return Json::Value(Json::objectValue);
    });

    sicnu::jobs::JobRequest request;
    request.algorithmId = "test:cancel";
    request.source = "task_panel";
    const long taskId = sicnu::TaskCenter::instance().submitJob(request);

    REQUIRE(sicnu::TaskCenter::instance().cancelTask(taskId));
    engine.waitUntilIdleForTests();

    sicnu::AlgorithmTaskInfo info;
    for (int attempt = 0; attempt < 20; ++attempt) {
        info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
        if (info.status == sicnu::TaskStatus::Canceled)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(info.status == sicnu::TaskStatus::Canceled);
    REQUIRE_FALSE(info.jobId.empty());
    const auto job = engine.snapshot(info.jobId);
    REQUIRE(job.has_value());
    REQUIRE(job->state == sicnu::jobs::JobState::Cancelled);
}

TEST_CASE("TaskCenter - Running cancellation waits for the worker terminal state", "[processing][task_center][cancellation]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    std::atomic_bool started = false;
    std::atomic_bool releaseWorker = false;

    sicnu::jobs::JobRequest request;
    request.algorithmId = "callable:cancel-lifecycle";
    request.source = "task_panel";
    const long taskId = sicnu::TaskCenter::instance().submitJob(
        request,
        [&started, &releaseWorker](const sicnu::jobs::JobRequest&, sicnu::operators::RSOperatorContext&) {
            started.store(true);
            while (!releaseWorker.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return Json::Value(Json::objectValue);
        });

    for (int attempt = 0; attempt < 100 && !started.load(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(started.load());
    REQUIRE(sicnu::TaskCenter::instance().cancelTask(taskId));
    REQUIRE(sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Running);

    releaseWorker.store(true);
    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 20
                      && sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Running;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Canceled);
}
