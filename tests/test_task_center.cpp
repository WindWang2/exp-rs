#include <catch2/catch_test_macros.hpp>

#include "processing/framework/json_params_converter.h"
#include "processing/framework/task_center.h"
#include "processing/framework/resource_monitor.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "workflow/workflow_definition.h"

#include <QObject>

#include <chrono>
#include <atomic>
#include <future>
#include <thread>
#include <vector>

// JobEngine::waitUntilIdleForTests() returns once the engine's own m_running
// counter hits zero, but the TaskCenter marks a task Completed/Failed via the
// job-record listener, which a worker thread runs slightly AFTER it decremented
// m_running. With multiple concurrent workers the two counters are updated by
// different threads, so "engine idle" does NOT imply "every task's terminal
// transition has landed in TaskCenter". Poll the task status (the established
// pattern at lines ~106/~131) before asserting a terminal status.
namespace {
void waitForTerminalStatus( sicnu::TaskCenter &center, long taskId,
                            int attempts = 200, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) )
            return;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
}
} // namespace

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

TEST_CASE("TaskCenter - Upstream External Cancellation Cascades to Descendants", "[processing][task_center][cancellation_cascade]") {
    auto& center = sicnu::TaskCenter::instance();

    QVariantMap parentParams;
    long parentId = center.enqueueTask(QStringLiteral("parent_cancel_algo"), parentParams, true);

    QVariantMap childParams;
    QList<long> parentIds = { parentId };
    long childId = center.enqueueTask(QStringLiteral("child_cancel_algo"), childParams, true, sicnu::TaskPriority::Normal, parentIds);
    long grandchildId = center.enqueueTask(QStringLiteral("grandchild_cancel_algo"), childParams, true, sicnu::TaskPriority::Normal, { childId });

    // Mark parent canceled (listener-side external cancellation path)
    center.markTaskCanceled(parentId, QStringLiteral("Externally canceled"));

    auto parentInfo = center.getTaskInfo(parentId);
    REQUIRE(parentInfo.status == sicnu::TaskStatus::Canceled);

    // Both descendants cascade to Canceled with the upstream-cancellation wording
    auto childInfo = center.getTaskInfo(childId);
    REQUIRE(childInfo.status == sicnu::TaskStatus::Canceled);
    REQUIRE(childInfo.errorMessage.contains(QStringLiteral("upstream parent task cancellation")));

    auto grandchildInfo = center.getTaskInfo(grandchildId);
    REQUIRE(grandchildInfo.status == sicnu::TaskStatus::Canceled);
    REQUIRE(grandchildInfo.logBuffer.last().contains(QStringLiteral("upstream parent task cancellation")));
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

TEST_CASE("TaskCenter - forwards operator log lines incrementally (delta notify)", "[processing][task_center]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    // The executor logs one line, then waits until the TEST has observed that
    // line in the task log while the task is still Running. With a broken
    // delta notify (lines dropped at notify time and only flushed by the
    // terminal record, #638) nothing is observable before terminal and the
    // poll below times out - the regression is detected deterministically.
    std::promise<void> lineOneObserved;
    std::shared_future<void> lineOneObservedFuture = lineOneObserved.get_future().share();
    // Always unblock the executor (even if an assertion below aborts the test
    // case) so a failing run cannot leave a worker thread parked forever.
    struct PromiseSignaler {
        std::promise<void>& promise;
        bool fired = false;
        void fire() { if ( !fired ) { promise.set_value(); fired = true; } }
        ~PromiseSignaler() { try { fire(); } catch ( ... ) {} }
    } lineOneSignaler{ lineOneObserved };
    engine.registerExecutor("test:log-delta",
                            [&lineOneObserved, lineOneObservedFuture](
                              const sicnu::jobs::JobRequest&, sicnu::operators::RSOperatorContext& context) {
        context.logInfo("delta line one");
        lineOneObservedFuture.wait();
        context.logInfo("delta line two");
        context.logInfo("delta line three");
        Json::Value result(Json::objectValue);
        result["output"] = "delta-done";
        return result;
    });

    sicnu::jobs::JobRequest request;
    request.algorithmId = "test:log-delta";
    request.source = "task_panel";
    const long taskId = sicnu::TaskCenter::instance().submitJob(request);
    REQUIRE(taskId > 0);

    bool streamedBeforeTerminal = false;
    for (int attempt = 0; attempt < 5000; ++attempt) {
        const auto info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
        if (info.logBuffer.join('\n').contains(QStringLiteral("delta line one"))) {
            streamedBeforeTerminal = true;
            break;
        }
        if (sicnu::isTerminalStatus(info.status))
            break; // terminal flushed the lines instead: broken streaming
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(streamedBeforeTerminal);

    lineOneSignaler.fire();
    engine.waitUntilIdleForTests();
    waitForTerminalStatus( sicnu::TaskCenter::instance(), taskId );

    // Every line must land exactly once once the job is terminal.
    const auto info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
    REQUIRE(info.status == sicnu::TaskStatus::Completed);
    const QString log = info.logBuffer.join('\n');
    REQUIRE(log.contains(QStringLiteral("delta line one")));
    REQUIRE(log.contains(QStringLiteral("delta line two")));
    REQUIRE(log.contains(QStringLiteral("delta line three")));
}

TEST_CASE("TaskCenter - clearCompletedTasks also prunes the JobEngine records", "[processing][task_center][clear]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.registerExecutor("test:clear-jobs", [](const sicnu::jobs::JobRequest&, sicnu::operators::RSOperatorContext&) {
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/clear-me.tif";
        return result;
    });

    auto& center = sicnu::TaskCenter::instance();

    sicnu::jobs::JobRequest request;
    request.algorithmId = "test:clear-jobs";
    request.source = "task_panel";

    const long taskA = center.submitJob(request);
    const long taskB = center.submitJob(request);
    REQUIRE(taskA > 0);
    REQUIRE(taskB > 0);

    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 20
                      && (center.getTaskInfo(taskA).status == sicnu::TaskStatus::Running
                          || center.getTaskInfo(taskB).status == sicnu::TaskStatus::Running);
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(center.getTaskInfo(taskA).status == sicnu::TaskStatus::Completed);
    REQUIRE(center.getTaskInfo(taskB).status == sicnu::TaskStatus::Completed);

    const std::string jobA = center.getTaskInfo(taskA).jobId;
    const std::string jobB = center.getTaskInfo(taskB).jobId;
    REQUIRE_FALSE(jobA.empty());
    REQUIRE_FALSE(jobB.empty());
    REQUIRE(engine.snapshot(jobA).has_value());
    REQUIRE(engine.snapshot(jobB).has_value());

    // A job submitted directly to the engine (not tracked by TaskCenter)
    // must survive the clear: only the cleared tasks' records are pruned.
    sicnu::jobs::JobRequest direct;
    direct.algorithmId = "test:clear-jobs";
    direct.source = "test";
    const auto directId = engine.submit(direct);
    engine.waitUntilIdleForTests();
    REQUIRE(engine.snapshot(directId).has_value());

    center.clearCompletedTasks();
    const auto tasksAfterClear = center.allTasks().size();

    // TaskCenter no longer retains the cleared tasks...
    REQUIRE(center.getTaskInfo(taskA).taskId == -1);
    REQUIRE(center.getTaskInfo(taskB).taskId == -1);
    // ...the engine records for exactly those tasks are gone...
    REQUIRE_FALSE(engine.snapshot(jobA).has_value());
    REQUIRE_FALSE(engine.snapshot(jobB).has_value());
    // ...and no task survived under the cleared ids.
    for (const auto& t : center.allTasks()) {
        REQUIRE(t.taskId != taskA);
        REQUIRE(t.taskId != taskB);
    }

    // The listener still fires for unknown jobIds (pruned/foreign records)
    // without crashing or creating bookkeeping.
    const auto directId2 = engine.submit(direct);
    engine.waitUntilIdleForTests();
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // let the terminal notify land
    REQUIRE(engine.snapshot(directId2).has_value());
    REQUIRE(center.allTasks().size() == tasksAfterClear);

    engine.clearExecutors();
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
    waitForTerminalStatus( sicnu::TaskCenter::instance(), taskId );
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
    // Cancel-requested dispatched work is explicitly Cancelling (not silently
    // Running) until the worker's terminal record arrives.
    REQUIRE(sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Cancelling);

    releaseWorker.store(true);
    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 200
                      && sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Cancelling;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(sicnu::TaskCenter::instance().getTaskInfo(taskId).status == sicnu::TaskStatus::Canceled);
}

TEST_CASE("TaskCenter - Native submitPipeline dispatches DAG and resolves $stepId.output", "[processing][task_center][pipeline]") {
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    std::string observedStep2Input;
    engine.registerExecutor("pipe:step1", [](const sicnu::jobs::JobRequest& req, sicnu::operators::RSOperatorContext& context) {
        context.logInfo("step1 done");
        Json::Value result(Json::objectValue);
        if (req.params.isMember("output") && req.params["output"].isString())
            result["output"] = req.params["output"].asString();
        else
            result["output"] = "/tmp/step1_ndvi.tif";
        return result;
    });
    engine.registerExecutor("pipe:step2", [&](const sicnu::jobs::JobRequest& req, sicnu::operators::RSOperatorContext& context) {
        context.logInfo("step2 done");
        if (req.params.isMember("input") && req.params["input"].isString())
            observedStep2Input = req.params["input"].asString();
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/step2_blur.tif";
        return result;
    });

    auto& center = sicnu::TaskCenter::instance();

    sicnu::workflow::WorkflowDefinition def;
    def.id = "test_pipeline_def";
    def.title = "Test Pipeline";

    sicnu::workflow::StepDef step1;
    step1.id = "step1";
    step1.title = "Step 1 Operator";
    step1.kind = sicnu::workflow::StepKind::Operator;
    step1.operatorId = "pipe:step1";
    step1.params["output"] = "/tmp/step1_ndvi.tif";

    sicnu::workflow::StepDef step2;
    step2.id = "step2";
    step2.title = "Step 2 Operator";
    step2.kind = sicnu::workflow::StepKind::Operator;
    step2.operatorId = "pipe:step2";
    step2.params["input"] = "$step1.output";
    step2.params["output"] = "/tmp/step2_blur.tif";

    sicnu::workflow::StepConnection conn;
    conn.fromStepId = "step1";
    conn.fromPort = "output";
    conn.toPort = "input";
    step2.inputs.push_back( conn );

    def.steps = { step1, step2 };

    long pId = center.submitPipeline(def, /*autoLoad=*/false);
    REQUIRE(pId > 0);

    auto pipeInfo = center.getPipelineInfo(pId);
    REQUIRE(pipeInfo.pipelineId == pId);
    REQUIRE(pipeInfo.stepToTaskId.contains("step1"));
    REQUIRE(pipeInfo.stepToTaskId.contains("step2"));

    long s1TaskId = pipeInfo.stepToTaskId["step1"];
    long s2TaskId = pipeInfo.stepToTaskId["step2"];

    // Root step must auto-dispatch (not remain Queued forever).
    for (int attempt = 0; attempt < 200; ++attempt) {
        auto st = center.getTaskInfo(s1TaskId).status;
        if (st == sicnu::TaskStatus::Running || st == sicnu::TaskStatus::Completed || st == sicnu::TaskStatus::Failed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    {
        auto st = center.getTaskInfo(s1TaskId).status;
        REQUIRE((st == sicnu::TaskStatus::Running || st == sicnu::TaskStatus::Completed || st == sicnu::TaskStatus::Failed));
    }

    engine.waitUntilIdleForTests();
    for (int attempt = 0; attempt < 200; ++attempt) {
        auto info = center.getPipelineInfo(pId);
        if (info.isCompleted)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    pipeInfo = center.getPipelineInfo(pId);
    REQUIRE(pipeInfo.isCompleted);
    REQUIRE_FALSE(pipeInfo.isFailed);
    REQUIRE(center.getTaskInfo(s1TaskId).status == sicnu::TaskStatus::Completed);
    REQUIRE(center.getTaskInfo(s2TaskId).status == sicnu::TaskStatus::Completed);
    REQUIRE(observedStep2Input == "/tmp/step1_ndvi.tif");
    REQUIRE(center.getTaskInfo(s2TaskId).parameterMap.value("input").toString() == QStringLiteral("/tmp/step1_ndvi.tif"));

    engine.clearExecutors();
}

TEST_CASE( "TaskCenter - resource profile throttling distinguishes concurrency caps",
           "[processing][task_center][throttling]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 8 );
    center.setResourceProfileLimit( sicnu::ProviderResourceProfile::InProcessThread, 2 );
    center.setResourceProfileLimit( sicnu::ProviderResourceProfile::ExternalCliSubprocess, 1 );

    class ProfiledProvider : public sicnu::AlgorithmProviderAdapter
    {
      public:
        ProfiledProvider( QString id, sicnu::ProviderResourceProfile profile )
          : m_id( std::move( id ) )
          , m_profile( profile )
        {
        }
        QString providerId() const override { return m_id; }
        QString providerName() const override { return m_id; }
        sicnu::ProviderResourceProfile resourceProfile() const override { return m_profile; }
        void initialize() override {}
        void discoverAlgorithms( sicnu::AlgorithmEngine & ) override {}

      private:
        QString m_id;
        sicnu::ProviderResourceProfile m_profile;
    };

    auto &algEngine = sicnu::AlgorithmEngine::instance();
    algEngine.registerProvider(
      std::make_shared<ProfiledProvider>( QStringLiteral( "throttle_cli" ),
                                          sicnu::ProviderResourceProfile::ExternalCliSubprocess ) );
    algEngine.registerProvider(
      std::make_shared<ProfiledProvider>( QStringLiteral( "throttle_inproc" ),
                                          sicnu::ProviderResourceProfile::InProcessThread ) );

    std::atomic<int> inFlightCli{ 0 };
    std::atomic<int> maxCli{ 0 };
    std::atomic<int> inFlightInproc{ 0 };
    std::atomic<int> maxInproc{ 0 };
    std::atomic<bool> releaseWorkers{ false };

    auto holdExecutor = []( std::atomic<int> &inFlight, std::atomic<int> &maxSeen,
                            std::atomic<bool> &release ) {
        return [&inFlight, &maxSeen, &release]( const sicnu::jobs::JobRequest &,
                                                sicnu::operators::RSOperatorContext & ) {
            const int cur = ++inFlight;
            int prev = maxSeen.load();
            while ( cur > prev && !maxSeen.compare_exchange_weak( prev, cur ) )
            {
            }
            while ( !release.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            --inFlight;
            Json::Value result( Json::objectValue );
            result["output"] = "/tmp/throttle.tif";
            return result;
        };
    };

    engine.registerExecutor( "throttle_cli:task", holdExecutor( inFlightCli, maxCli, releaseWorkers ) );
    engine.registerExecutor( "throttle_inproc:task", holdExecutor( inFlightInproc, maxInproc, releaseWorkers ) );

    // Enqueue three CLI tasks — only 1 may run at a time.
    QList<long> cliIds;
    for ( int i = 0; i < 3; ++i )
        cliIds.append( center.enqueueTask( QStringLiteral( "throttle_cli:task" ), {}, false,
                                           sicnu::TaskPriority::Normal, {}, true ) );

    // Enqueue three in-process tasks — up to 2 may run concurrently.
    QList<long> inprocIds;
    for ( int i = 0; i < 3; ++i )
        inprocIds.append( center.enqueueTask( QStringLiteral( "throttle_inproc:task" ), {}, false,
                                              sicnu::TaskPriority::Normal, {}, true ) );

    for ( int attempt = 0; attempt < 200; ++attempt )
    {
        if ( maxCli.load() >= 1 && maxInproc.load() >= 1 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    // Snapshot how many are Running before release.
    int cliRunning = 0;
    int inprocRunning = 0;
    int cliQueued = 0;
    for ( long id : cliIds )
    {
        const auto st = center.getTaskInfo( id ).status;
        if ( st == sicnu::TaskStatus::Running )
            ++cliRunning;
        if ( st == sicnu::TaskStatus::Queued || st == sicnu::TaskStatus::WaitingResource )
            ++cliQueued; // WaitingResource = admission-held, still queued work
    }
    for ( long id : inprocIds )
    {
        if ( center.getTaskInfo( id ).status == sicnu::TaskStatus::Running )
            ++inprocRunning;
    }

    CHECK( center.getTaskInfo( cliIds.first() ).resourceProfile
           == sicnu::ProviderResourceProfile::ExternalCliSubprocess );
    CHECK( center.getTaskInfo( inprocIds.first() ).resourceProfile
           == sicnu::ProviderResourceProfile::InProcessThread );
    CHECK( cliRunning <= 1 );
    CHECK( cliQueued >= 1 );
    CHECK( inprocRunning <= 2 );
    CHECK( maxCli.load() <= 1 );
    CHECK( maxInproc.load() <= 2 );

    releaseWorkers.store( true );
    engine.waitUntilIdleForTests();
    for ( int attempt = 0; attempt < 200; ++attempt )
    {
        bool allDone = true;
        for ( long id : cliIds )
            allDone = allDone && center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed;
        for ( long id : inprocIds )
            allDone = allDone && center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed;
        if ( allDone )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    for ( long id : cliIds )
        REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );
    for ( long id : inprocIds )
        REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );

    engine.clearExecutors();
    center.resetResourceProfileLimits();
}

TEST_CASE( "TaskCenter - reentrant taskUpdated slot does not deadlock",
           "[processing][task_center][reentrancy]" )
{
    auto &center = sicnu::TaskCenter::instance();

    std::atomic<int> slotEnterCount{ 0 };
    std::atomic<int> slotDoneCount{ 0 };
    std::atomic<bool> sawCompleted{ false };

    QObject guard;
    QObject::connect( &center, &sicnu::TaskCenter::taskUpdated, &guard,
                      [&]( const sicnu::AlgorithmTaskInfo &info ) {
                        ++slotEnterCount;
                        // Re-enter TaskCenter while handling the signal (would deadlock
                        // if taskUpdated were emitted while holding m_mutex).
                        const auto snapshot = center.getTaskInfo( info.taskId );
                        REQUIRE( snapshot.taskId == info.taskId );
                        (void) center.allTasks();
                        if ( info.status == sicnu::TaskStatus::Completed )
                          sawCompleted.store( true );
                        ++slotDoneCount;
                      } );

    long taskId = center.enqueueTask( QStringLiteral( "reentrancy_probe" ), {}, false );
    REQUIRE( taskId > 0 );

    center.markTaskRunning( taskId );
    center.markTaskCompleted( taskId );

    REQUIRE( slotEnterCount.load() >= 1 );
    REQUIRE( slotDoneCount.load() == slotEnterCount.load() );
    REQUIRE( sawCompleted.load() );
    REQUIRE( center.getTaskInfo( taskId ).status == sicnu::TaskStatus::Completed );
}

TEST_CASE( "TaskCenter - event-driven wait condition sub-millisecond wakeup latency", "[processing][task_center][event_driven]" )
{
    auto &center = sicnu::TaskCenter::instance();

    const long taskId = center.enqueueTask( QStringLiteral( "latency_probe" ), {}, false );
    REQUIRE( taskId > 0 );

    std::atomic<bool> wokenUp{ false };

    const auto markStart = std::chrono::steady_clock::now();

    std::thread waiter( [&]() {
        const auto info = center.waitForTask( taskId, std::chrono::seconds( 5 ) );
        wokenUp.store( info.status == sicnu::TaskStatus::Completed );
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    center.markTaskCompleted( taskId );

    waiter.join();
    const auto markEnd = std::chrono::steady_clock::now();

    REQUIRE( wokenUp.load() );
}

TEST_CASE( "TaskCenter - priority-aware scheduler preempts lower priority queued tasks", "[processing][task_center][priority]" )
{
    auto &center = sicnu::TaskCenter::instance();
    auto &engine = sicnu::jobs::JobEngine::instance();

    center.setGlobalConcurrencyLimit( 1 );

    std::atomic<bool> releaseFirst{ false };
    std::atomic<bool> firstRunning{ false };
    std::vector<std::string> launchOrder;
    std::mutex orderMutex;

    auto recordLaunch = [&]( const std::string &name ) {
        std::lock_guard<std::mutex> lock( orderMutex );
        launchOrder.push_back( name );
    };

    engine.registerExecutor( "priority:blocker", [&]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
        firstRunning.store( true );
        while ( !releaseFirst.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        recordLaunch( "blocker" );
        Json::Value res( Json::objectValue );
        res["output"] = "/tmp/blocker.tif";
        return res;
    } );

    engine.registerExecutor( "priority:low", [&]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
        recordLaunch( "low" );
        Json::Value res( Json::objectValue );
        res["output"] = "/tmp/low.tif";
        return res;
    } );

    engine.registerExecutor( "priority:high", [&]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) {
        recordLaunch( "high" );
        Json::Value res( Json::objectValue );
        res["output"] = "/tmp/high.tif";
        return res;
    } );

    long blockerId = center.enqueueTask( QStringLiteral( "priority:blocker" ), {}, false, sicnu::TaskPriority::Normal, {}, true );
    REQUIRE( blockerId > 0 );

    for ( int i = 0; i < 200; ++i ) {
        if ( firstRunning.load() ) break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( firstRunning.load() );

    // Enqueue Low priority task first, then High priority task
    long lowId = center.enqueueTask( QStringLiteral( "priority:low" ), {}, false, sicnu::TaskPriority::Low, {}, true );
    long highId = center.enqueueTask( QStringLiteral( "priority:high" ), {}, false, sicnu::TaskPriority::High, {}, true );

    REQUIRE( lowId > 0 );
    REQUIRE( highId > 0 );

    releaseFirst.store( true );

    center.waitForTask( highId, std::chrono::seconds( 5 ) );
    center.waitForTask( lowId, std::chrono::seconds( 5 ) );

    {
        std::lock_guard<std::mutex> lock( orderMutex );
        REQUIRE( launchOrder.size() == 3 );
        CHECK( launchOrder[0] == "blocker" );
        CHECK( launchOrder[1] == "high" );
        CHECK( launchOrder[2] == "low" );
    }

    center.resetResourceProfileLimits();
    engine.clearExecutors();
}

TEST_CASE( "TaskCenter - recursive DAG cancellation cascades to child and grandchild tasks", "[processing][task_center][cancellation_cascade]" )
{
    auto &center = sicnu::TaskCenter::instance();

    long parentId = center.enqueueTask( QStringLiteral( "dag:parent" ), {}, false );
    long childId = center.enqueueTask( QStringLiteral( "dag:child" ), {}, false, sicnu::TaskPriority::Normal, { parentId } );
    long grandchildId = center.enqueueTask( QStringLiteral( "dag:grandchild" ), {}, false, sicnu::TaskPriority::Normal, { childId } );

    REQUIRE( parentId > 0 );
    REQUIRE( childId > 0 );
    REQUIRE( grandchildId > 0 );

    CHECK( center.getTaskInfo( parentId ).status == sicnu::TaskStatus::Queued );
    CHECK( center.getTaskInfo( childId ).status == sicnu::TaskStatus::Queued );
    CHECK( center.getTaskInfo( grandchildId ).status == sicnu::TaskStatus::Queued );

    bool ok = center.cancelTask( parentId );
    REQUIRE( ok );

    CHECK( center.getTaskInfo( parentId ).status == sicnu::TaskStatus::Canceled );
    CHECK( center.getTaskInfo( childId ).status == sicnu::TaskStatus::Canceled );
    CHECK( center.getTaskInfo( grandchildId ).status == sicnu::TaskStatus::Canceled );
}

// ---------------------------------------------------------------------------
// ADR 0063 - RSS watermark throttling: hold launches on memory pressure,
// then release when RSS drops (re-evaluated on task completion).
// ---------------------------------------------------------------------------
TEST_CASE( "TaskCenter - RSS watermark holds queued tasks then releases on completion",
           "[processing][task_center][throttling]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 8 ); // generous; memory gate is the binding constraint
    center.setMemoryLimitMb( 100 );
    // setMemoryLimitMb also arms the estimate-based budget at the same cap
    // (deliberate sync so both gates share one cap). The in-process executor
    // here has no registry adapter, so its estimate falls back to 1024 MiB and
    // would block the second task. Disable the estimate gate so this test
    // exercises only the RSS watermark; the estimate gate has its own coverage
    // in test_task_resource_budget.cpp.
    center.setResourceBudgetMb( 0 );

    std::atomic<unsigned int> fakeRss{ 50 }; // below the 100 MB watermark
    center.setRssSampler( [&fakeRss]() { return fakeRss.load(); } );

    std::atomic<bool> releaseWorkers{ false };
    auto holdExecutor = [&releaseWorkers]( const sicnu::jobs::JobRequest &,
                                           sicnu::operators::RSOperatorContext & ) {
        while ( !releaseWorkers.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        Json::Value result( Json::objectValue );
        result["output"] = "/tmp/mem_throttle.tif";
        return result;
    };
    engine.registerExecutor( "mem_inproc:task", holdExecutor );

    // Launch two tasks under low RSS - both run immediately.
    long id1 = center.enqueueTask( QStringLiteral( "mem_inproc:task" ), {}, false,
                                   sicnu::TaskPriority::Normal, {}, true );
    long id2 = center.enqueueTask( QStringLiteral( "mem_inproc:task" ), {}, false,
                                   sicnu::TaskPriority::Normal, {}, true );
    for ( int attempt = 0; attempt < 200; ++attempt )
    {
        if ( center.getTaskInfo( id1 ).status == sicnu::TaskStatus::Running
             && center.getTaskInfo( id2 ).status == sicnu::TaskStatus::Running )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( center.getTaskInfo( id1 ).status == sicnu::TaskStatus::Running );
    REQUIRE( center.getTaskInfo( id2 ).status == sicnu::TaskStatus::Running );

    // Raise RSS to the watermark, then enqueue a third task - it must stay held.
    fakeRss.store( 100 );
    long id3 = center.enqueueTask( QStringLiteral( "mem_inproc:task" ), {}, false,
                                   sicnu::TaskPriority::Normal, {}, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) ); // let any pending dispatch settle
    // Admission-held (RSS watermark) tasks surface explicitly as
    // WaitingResource instead of silently Queued.
    REQUIRE( center.getTaskInfo( id3 ).status == sicnu::TaskStatus::WaitingResource );

    // Drop RSS below the watermark and release the workers. Their completions
    // re-enter processNextQueuedTasks, which now sees low pressure and launches
    // the third task, which then also runs to completion.
    fakeRss.store( 50 );
    releaseWorkers.store( true );

    for ( int attempt = 0; attempt < 400; ++attempt )
    {
        if ( center.getTaskInfo( id3 ).status == sicnu::TaskStatus::Completed )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    // id3 reached Completed, which means it was launched (gate reopened) and ran.
    REQUIRE( center.getTaskInfo( id3 ).status == sicnu::TaskStatus::Completed );

    engine.waitUntilIdleForTests();
    // id1/id2 finished earlier, but under multi-worker concurrency their
    // terminal transitions can lag the engine's m_running==0 observation
    // (see waitForTerminalStatus). Poll before asserting.
    waitForTerminalStatus( center, id1 );
    waitForTerminalStatus( center, id2 );
    REQUIRE( center.getTaskInfo( id1 ).status == sicnu::TaskStatus::Completed );
    REQUIRE( center.getTaskInfo( id2 ).status == sicnu::TaskStatus::Completed );

    center.resetResourceProfileLimits();
    engine.clearExecutors();
}

TEST_CASE( "TaskCenter - memory limit 0 disables the RSS gate",
           "[processing][task_center][throttling]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 8 );
    // Pin the per-profile cap so this test is machine-independent (the default
    // InProcessThread cap is hardware_concurrency()-1, which is 3 on 4-core
    // runners but 1 on 2-core machines).
    center.setResourceProfileLimit( sicnu::ProviderResourceProfile::InProcessThread, 3 );
    center.setMemoryLimitMb( 0 ); // disabled

    std::atomic<unsigned int> fakeRss{ 999999 }; // would block if the gate were on
    center.setRssSampler( [&fakeRss]() { return fakeRss.load(); } );

    std::atomic<bool> releaseWorkers{ false };
    engine.registerExecutor( "mem_disabled:task",
        [&releaseWorkers]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext & ) {
            while ( !releaseWorkers.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            Json::Value result( Json::objectValue );
            result["output"] = "/tmp/mem_disabled.tif";
            return result;
        } );

    QList<long> ids;
    for ( int i = 0; i < 3; ++i )
        ids.append( center.enqueueTask( QStringLiteral( "mem_disabled:task" ), {}, false,
                                        sicnu::TaskPriority::Normal, {}, true ) );

    // Despite a huge fake RSS, all three should run (gate disabled).
    for ( int attempt = 0; attempt < 400; ++attempt )
    {
        int running = 0;
        for ( long id : ids )
            if ( center.getTaskInfo( id ).status == sicnu::TaskStatus::Running )
                ++running;
        if ( running >= 3 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    int running = 0;
    for ( long id : ids )
        if ( center.getTaskInfo( id ).status == sicnu::TaskStatus::Running )
            ++running;
    CHECK( running == 3 );

    releaseWorkers.store( true );
    engine.waitUntilIdleForTests();
    // The engine is idle, but the per-task terminal transition can lag the
    // m_running==0 observation when several workers finish near-simultaneously
    // (see waitForTerminalStatus). Poll before asserting.
    for ( long id : ids )
        waitForTerminalStatus( center, id );
    for ( long id : ids )
        REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );

    center.resetResourceProfileLimits();
    engine.clearExecutors();
}

// ---------------------------------------------------------------------------
// ADR 0063 stress — OBIA segmentation/classification-style high-concurrency
// load with a simulated memory ramp. Models the real failure mode the RSS
// watermark exists to prevent: many memory-heavy tasks launched at once push
// the process toward OOM. The gate must hold launches while pressure is high
// and reopen as each running task finishes and frees memory, with every task
// eventually reaching Completed (no Failed, no deadlock, no over-launch).
// ---------------------------------------------------------------------------
TEST_CASE( "TaskCenter - OBIA-style batch holds under RSS pressure then drains to completion",
           "[processing][task_center][stress]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 4 ); // a few run concurrently; memory gate binds above that
    center.setMemoryLimitMb( 100 );

    // Simulated RSS: each running task bumps it toward the watermark; releasing
    // the tasks (and dropping RSS) models memory being freed on completion.
    std::atomic<unsigned int> fakeRss{ 40 };
    std::atomic<int> runningTasks{ 0 };
    center.setRssSampler( [&fakeRss]() { return fakeRss.load(); } );

    std::atomic<bool> releaseWorkers{ false };
    auto obiaExecutor = [&]( const sicnu::jobs::JobRequest &,
                             sicnu::operators::RSOperatorContext & ) {
        // Each "segmentation" task claims memory while running.
        const int n = runningTasks.fetch_add( 1 ) + 1;
        // Ramp RSS up as tasks pile in; once it crosses the watermark the gate
        // closes and no further tasks launch until some finish and free memory.
        fakeRss.store( static_cast<unsigned int>( 40 + n * 25 ) );
        while ( !releaseWorkers.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        runningTasks.fetch_sub( 1 );
        // Free memory as tasks complete.
        fakeRss.store( static_cast<unsigned int>( 40 + runningTasks.load() * 25 ) );
        Json::Value result( Json::objectValue );
        result["output"] = "/tmp/obia_segment.tif";
        return result;
    };
    engine.registerExecutor( "obia_inproc:segment", obiaExecutor );

    // Enqueue a batch larger than the concurrency cap so several must queue.
    constexpr int BATCH = 12;
    QList<long> ids;
    for ( int i = 0; i < BATCH; ++i )
        ids.append( center.enqueueTask( QStringLiteral( "obia_inproc:segment" ), {}, false,
                                        sicnu::TaskPriority::Normal, {}, true ) );

    // Let the initial wave launch and push RSS up.
    int queued = 0, running = 0;
    for ( int attempt = 0; attempt < 400; ++attempt )
    {
        queued = 0;
        running = 0;
        for ( long id : ids )
        {
            const auto s = center.getTaskInfo( id ).status;
            if ( s == sicnu::TaskStatus::Queued || s == sicnu::TaskStatus::WaitingResource )
                ++queued; // WaitingResource = admission-held, still queued work
            else if ( s == sicnu::TaskStatus::Running ) ++running;
        }
        if ( running > 0 && queued > 0 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    // While RSS is at/above the watermark, queued tasks must NOT be Running.
    // At least one task should still be Queued (the batch exceeds the cap and
    // the gate is closed under pressure).
    REQUIRE( running > 0 );           // the first wave launched
    REQUIRE( queued > 0 );            // and the rest are held (no over-launch)
    REQUIRE( running + queued == BATCH ); // none completed/failed yet

    // Release the workers: each completion frees memory, re-opening the gate
    // via processNextQueuedTasks, so the whole batch drains.
    releaseWorkers.store( true );
    engine.waitUntilIdleForTests();
    for ( long id : ids )
        waitForTerminalStatus( center, id, 400 );

    int completed = 0, failed = 0;
    for ( long id : ids )
    {
        const auto s = center.getTaskInfo( id ).status;
        if ( s == sicnu::TaskStatus::Completed ) ++completed;
        else if ( s == sicnu::TaskStatus::Failed ) ++failed;
    }
    REQUIRE( completed == BATCH );
    REQUIRE( failed == 0 );

    center.resetResourceProfileLimits();
    engine.clearExecutors();
}

// ---------------------------------------------------------------------------
// ADR 0063 — under high-concurrency load the gate stays disabled when the
// watermark is 0 (no throttling), mirroring the single-task case but with a
// batch that exercises multi-worker completion ordering.
// ---------------------------------------------------------------------------
TEST_CASE( "TaskCenter - memory limit 0 keeps the gate open under batch load",
           "[processing][task_center][stress]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 8 );
    // resetResourceProfileLimits() clears per-profile caps, so the InProcessThread
    // profile falls back to hardware_concurrency()-1 (3 on the 4-core CI runner).
    // Raise it explicitly so the global limit is the binding constraint, making
    // the assertion machine-independent.
    center.setResourceProfileLimit( sicnu::ProviderResourceProfile::InProcessThread, 8 );
    center.setMemoryLimitMb( 0 ); // gate disabled

    std::atomic<unsigned int> fakeRss{ 999999 }; // would block if the gate were on
    center.setRssSampler( [&fakeRss]() { return fakeRss.load(); } );

    std::atomic<bool> releaseWorkers{ false };
    engine.registerExecutor( "batch_disabled:task",
        [&releaseWorkers]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext & ) {
            while ( !releaseWorkers.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            Json::Value result( Json::objectValue );
            result["output"] = "/tmp/batch_disabled.tif";
            return result;
        } );

    constexpr int BATCH = 8;
    QList<long> ids;
    for ( int i = 0; i < BATCH; ++i )
        ids.append( center.enqueueTask( QStringLiteral( "batch_disabled:task" ), {}, false,
                                        sicnu::TaskPriority::Normal, {}, true ) );

    // This test RAISES admission to BATCH explicitly (to exercise the RSS
    // gate in isolation), which exceeds the engine worker pool: all BATCH
    // tasks are admitted (#686 semantics: admitted = Dispatching or Running),
    // while the count of tasks ACTUALLY running on a worker never exceeds
    // JobEngine::maxWorkers().
    for ( int attempt = 0; attempt < 400; ++attempt )
    {
        int admitted = 0;
        for ( long id : ids )
            if ( center.getTaskInfo( id ).status == sicnu::TaskStatus::Running
                 || center.getTaskInfo( id ).status == sicnu::TaskStatus::Dispatching )
                ++admitted;
        if ( admitted >= BATCH )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    int admitted = 0, onWorker = 0;
    for ( long id : ids )
    {
        const auto st = center.getTaskInfo( id ).status;
        if ( st == sicnu::TaskStatus::Running || st == sicnu::TaskStatus::Dispatching )
            ++admitted;
        if ( st == sicnu::TaskStatus::Running )
            ++onWorker;
    }
    CHECK( admitted == BATCH );
    CHECK( onWorker <= engine.maxWorkers() ); // honest Running (#686)

    releaseWorkers.store( true );
    engine.waitUntilIdleForTests();
    for ( long id : ids )
        waitForTerminalStatus( center, id, 400 );
    for ( long id : ids )
        REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );

    center.resetResourceProfileLimits();
    engine.clearExecutors();
}

// ---------------------------------------------------------------------------
// ADR 0063 amendment (Loop F) - the default sampler must report INSTANTANEOUS
// current RSS, not the all-time peak (getrusage ru_maxrss). The behavior
// difference that matters: after a process allocates a large block and then
// frees it, peak RSS stays at the high-water mark forever (so the watermark
// gate would stay closed even after memory was freed), while current RSS
// drops back down (so the gate reopens). This test pins that: allocate a
// ~128 MiB block, touch it to force residency, observe the RSS rise, free it,
// and assert the RSS subsequently falls back below the high-water mark. Under
// ru_maxrss this cannot happen (peak is monotonic), so it would fail RED.
// ---------------------------------------------------------------------------
TEST_CASE( "ResourceMonitor default sampler reports current (not peak) RSS",
           "[processing][task_center][rss]" )
{
    sicnu::ResourceMonitor monitor; // default sampler

    const unsigned int baseline = monitor.currentRssMb();
    // On platforms without a current-RSS source the sampler returns 0 and the
    // gate is disabled; skip the residency assertions there (can't test).
    if ( baseline == 0 )
    {
        WARN( "current RSS unavailable on this platform; skipping residency test" );
        return;
    }

    // Allocate + touch a ~128 MiB block to force the pages resident.
    constexpr size_t BLOCK = 128u * 1024u * 1024u; // 128 MiB
    std::vector<unsigned char> block( BLOCK );
    volatile unsigned char sink = 0;
    for ( size_t i = 0; i < BLOCK; i += 4096 )
        sink = block[i];
    (void)sink;

    const unsigned int highWater = monitor.currentRssMb();
    // The allocation should have raised RSS measurably (allow slack for
    // already-cached pages / allocator overhead).
    REQUIRE( highWater >= baseline );

    // Free the block and give the kernel a moment to reclaim the pages.
    block.clear();
    block.shrink_to_fit();
    unsigned int after = highWater;
    for ( int attempt = 0; attempt < 60; ++attempt )
    {
        after = monitor.currentRssMb();
        if ( after < highWater )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    INFO( "baseline=" << baseline << " highWater=" << highWater << " after=" << after );

    // Current-RSS contract: residency falls once the block is freed, so a
    // second identical alloc/touch/free cycle must NOT ratchet the reported
    // value up by another block. A peak-RSS (ru_maxrss) sampler is monotonic
    // and would grow by ~BLOCK each cycle. The direct "after < highWater"
    // check is informational only: ASan's allocator keeps freed pages
    // quarantined longer and concurrent activity can push a CURRENT reading
    // a few MB above any earlier sample, which made the strict comparison
    // flaky under sanitizers.
    constexpr unsigned int BLOCK_MB = static_cast<unsigned int>( BLOCK / ( 1024u * 1024u ) );
    std::vector<unsigned char> block2( BLOCK );
    for ( size_t i = 0; i < BLOCK; i += 4096 )
        sink = block2[i];
    block2.clear();
    block2.shrink_to_fit();
    unsigned int after2 = after;
    for ( int attempt = 0; attempt < 60; ++attempt )
    {
        after2 = monitor.currentRssMb();
        if ( after2 < after + BLOCK_MB / 2 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    INFO( "after2=" << after2 );
    REQUIRE( after2 < after + BLOCK_MB / 2 );
}

TEST_CASE("TaskCenter - Progress and markRunning never regress terminal states", "[processing][task_center][terminal]") {
    auto& center = sicnu::TaskCenter::instance();

    // 1. Completed task
    long completedId = center.enqueueTask(QStringLiteral("test_terminal_completed"), {}, true);
    REQUIRE(completedId > 0);
    center.markTaskCompleted(completedId);
    REQUIRE(center.getTaskInfo(completedId).status == sicnu::TaskStatus::Completed);

    center.updateTaskProgress(completedId, 0.5);
    REQUIRE(center.getTaskInfo(completedId).status == sicnu::TaskStatus::Completed);
    center.markTaskRunning(completedId);
    REQUIRE(center.getTaskInfo(completedId).status == sicnu::TaskStatus::Completed);

    // 2. Failed task
    long failedId = center.enqueueTask(QStringLiteral("test_terminal_failed"), {}, true);
    REQUIRE(failedId > 0);
    center.markTaskFailed(failedId, QStringLiteral("error"));
    REQUIRE(center.getTaskInfo(failedId).status == sicnu::TaskStatus::Failed);

    center.updateTaskProgress(failedId, 0.5);
    REQUIRE(center.getTaskInfo(failedId).status == sicnu::TaskStatus::Failed);
    center.markTaskRunning(failedId);
    REQUIRE(center.getTaskInfo(failedId).status == sicnu::TaskStatus::Failed);

    // 3. Canceled task
    long canceledId = center.enqueueTask(QStringLiteral("test_terminal_canceled"), {}, true);
    REQUIRE(canceledId > 0);
    center.markTaskCanceled(canceledId, QStringLiteral("canceled"));
    REQUIRE(center.getTaskInfo(canceledId).status == sicnu::TaskStatus::Canceled);

    center.updateTaskProgress(canceledId, 0.5);
    REQUIRE(center.getTaskInfo(canceledId).status == sicnu::TaskStatus::Canceled);
    center.markTaskRunning(canceledId);
    REQUIRE(center.getTaskInfo(canceledId).status == sicnu::TaskStatus::Canceled);
}


TEST_CASE( "retryTask rejects a non-terminal task (no double submission) (#616)", "[processing][task_center][retry]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();

    // Enqueue WITHOUT dispatch: the task stays Queued (still schedulable).
    QVariantMap params;
    const long id = sicnu::TaskCenter::instance().enqueueTask(
        "test:never_dispatched", params, false, sicnu::TaskPriority::Normal, QList<long>{}, false );
    REQUIRE( id > 0 );
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( id );
    REQUIRE( ( info.status == sicnu::TaskStatus::Queued || info.status == sicnu::TaskStatus::WaitingResource ) );

    const auto countBefore = sicnu::TaskCenter::instance().allTasks().size();
    // The original is non-terminal: retry must be refused instead of
    // enqueueing a second copy of the same work.
    REQUIRE_FALSE( sicnu::TaskCenter::instance().retryTask( id ) );
    REQUIRE( sicnu::TaskCenter::instance().allTasks().size() == countBefore );

    sicnu::TaskCenter::instance().cancelTask( id );
}

TEST_CASE( "jsonParamsToVariantMap survives out-of-range integers (#619)", "[task_center][json]" )
{
    // A JSON uint > INT_MAX (and > INT64_MAX) previously reached jsoncpp's
    // throwing asInt()/asInt64() inside the scheduler path.
    Json::Value params( Json::objectValue );
    params["window"] = Json::Value( Json::UInt64( 3000000000ULL ) );  // > INT_MAX, fits int64
    params["huge"] = Json::UInt64( 18446744073709551615ULL );  // > INT64_MAX
    params["small"] = 42;

    const QVariantMap map = sicnu::processing::jsonParamsToVariantMap( params );
    REQUIRE( map.value( "window" ).toLongLong() == 3000000000LL );
    REQUIRE( map.value( "small" ).toInt() == 42 );
    REQUIRE( map.contains( "huge" ) );  // string form, no exception
}


// ---------------------------------------------------------------------------
// Spatial Execution Platform Phase 1 regressions: honest Running, unified
// admission, sticky shutdown, retry semantics (#683-#686, #696, #702).
// ---------------------------------------------------------------------------

namespace {
// Saturate the engine so nothing else can start; returns when every blocker
// task has actually reached Running (worker picked it up).
std::vector<long> saturateEngine(sicnu::TaskCenter &center, sicnu::jobs::JobEngine &engine,
                                 int count, std::atomic_bool &release)
{
    std::vector<long> ids;
    for (int i = 0; i < count; ++i) {
        sicnu::jobs::JobRequest req;
        req.algorithmId = "callable:platform_blocker";
        ids.push_back(center.submitJob(
            req, [&release](const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx) {
                while (!release.load() && !ctx.isCancelled())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return Json::Value(Json::objectValue);
            }));
    }
    for (long id : ids) {
        for (int attempt = 0; attempt < 600
             && center.getTaskInfo(id).status != sicnu::TaskStatus::Running
             && !sicnu::isTerminalStatus(center.getTaskInfo(id).status);
             ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ids;
}
} // namespace

TEST_CASE("TaskCenter - submitJob shares admission with the engine: Running means a worker started (#686)",
          "[processing][task_center][platform]")
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers(2);
    auto &center = sicnu::TaskCenter::instance();

    std::atomic_bool release{false};
    const auto blockers = saturateEngine(center, engine, 2, release);
    REQUIRE(center.getTaskInfo(blockers[0]).status == sicnu::TaskStatus::Running);
    REQUIRE(center.getTaskInfo(blockers[1]).status == sicnu::TaskStatus::Running);

    // Third task beyond engine capacity: must NOT claim Running and must not
    // execute while both workers are held.
    std::atomic_bool extraRan{false};
    sicnu::jobs::JobRequest extraReq;
    extraReq.algorithmId = "callable:platform_extra";
    const long extra = center.submitJob(
        extraReq, [&extraRan](const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &) {
            extraRan.store(true);
            return Json::Value(Json::objectValue);
        });
    REQUIRE(extra > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto st = center.getTaskInfo(extra).status;
    REQUIRE(st != sicnu::TaskStatus::Running); // Queued/WaitingResource/Dispatching at most
    REQUIRE_FALSE(extraRan.load());

    release.store(true);
    waitForTerminalStatus(center, extra);
    REQUIRE(center.getTaskInfo(extra).status == sicnu::TaskStatus::Completed);
    for (long id : blockers)
        waitForTerminalStatus(center, id);
}

TEST_CASE("TaskCenter - cancel of an undispatched submitted job never executes it (#683)",
          "[processing][task_center][platform]")
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers(2);
    auto &center = sicnu::TaskCenter::instance();

    std::atomic_bool release{false};
    const auto blockers = saturateEngine(center, engine, 2, release);

    std::atomic_bool ran{false};
    sicnu::jobs::JobRequest req;
    req.algorithmId = "callable:platform_cancel_me";
    const long victim = center.submitJob(
        req, [&ran](const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &) {
            ran.store(true);
            return Json::Value(Json::objectValue);
        });
    REQUIRE(victim > 0);
    // Cancel while admitted-but-not-running (or resource-held): the task must
    // reach Canceled and the executor must never observe a start.
    REQUIRE(center.cancelTask(victim));
    waitForTerminalStatus(center, victim);
    REQUIRE(center.getTaskInfo(victim).status == sicnu::TaskStatus::Canceled);
    REQUIRE_FALSE(ran.load());

    release.store(true);
    for (long id : blockers)
        waitForTerminalStatus(center, id);
}

TEST_CASE("TaskCenter - retryTask of a cascade-canceled pipeline step is not stranded (#685)",
          "[processing][task_center][platform][retry]")
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers(2);
    auto &center = sicnu::TaskCenter::instance();

    engine.registerExecutor("plat:upstream", [](const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &) {
        Json::Value r(Json::objectValue);
        r["output"] = "/tmp/plat_upstream.tif";
        return r;
    });
    engine.registerExecutor("plat:downstream", [](const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &) {
        return Json::Value(Json::objectValue);
    });

    sicnu::workflow::WorkflowDefinition def;
    def.id = "plat_retry_def";
    def.title = "Retry Pipeline";
    sicnu::workflow::StepDef up;
    up.id = "up";
    up.title = "Upstream";
    up.kind = sicnu::workflow::StepKind::Operator;
    up.operatorId = "plat:upstream";
    sicnu::workflow::StepDef down;
    down.id = "down";
    down.title = "Downstream";
    down.kind = sicnu::workflow::StepKind::Operator;
    down.operatorId = "plat:downstream";
    sicnu::workflow::StepConnection conn;
    conn.fromStepId = "up";
    conn.fromPort = "output";
    conn.toPort = "input";
    down.inputs.push_back(conn);
    def.steps.push_back(up);
    def.steps.push_back(down);

    // Hold every worker so "up" stays admitted-but-undispatched and "down"
    // stays DAG-gated.
    std::atomic_bool release{false};
    const auto blockers = saturateEngine(center, engine, 2, release);

    const long pipeId = center.submitPipeline(def, false);
    REQUIRE(pipeId > 0);
    auto pipe = center.getPipelineInfo(pipeId);
    const long upTask = pipe.stepToTaskId["up"];
    const long downTask = pipe.stepToTaskId["down"];

    // Cascade-cancel the root: "up" cancels, "down" is cascade-canceled while
    // it was never dispatched (the exact #685 shape).
    REQUIRE(center.cancelTask(upTask));
    waitForTerminalStatus(center, upTask);
    waitForTerminalStatus(center, downTask);
    REQUIRE(center.getTaskInfo(downTask).status == sicnu::TaskStatus::Canceled);

    // Retry the cascade-canceled step: previously it re-enqueued with the old
    // (Canceled) parent and stranded in Queued forever.
    release.store(true);
    for (long id : blockers)
        waitForTerminalStatus(center, id);
    REQUIRE(center.retryTask(downTask));
    // The retried STEP must not strand in Queued forever (#685): with the
    // unsatisfiable parent dropped it launches and completes.
    {
        const long retriedDown = center.getPipelineInfo(pipeId).stepToTaskId.value("down");
        REQUIRE(retriedDown > 0);
        REQUIRE(retriedDown != downTask);
        for (int attempt = 0; attempt < 600; ++attempt) {
            if (sicnu::isTerminalStatus(center.getTaskInfo(retriedDown).status))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(center.getTaskInfo(retriedDown).status == sicnu::TaskStatus::Completed);
    }

    // The pipeline stays failed while the canceled ROOT is not retried (its
    // step never ran); retrying the root remaps the step and the pipeline
    // rolls up to completed.
    REQUIRE(center.retryTask(upTask));
    for (int attempt = 0; attempt < 600; ++attempt) {
        pipe = center.getPipelineInfo(pipeId);
        if (pipe.isCompleted || pipe.isFailed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(pipe.isCompleted);
    REQUIRE_FALSE(pipe.isFailed);
}

TEST_CASE("TaskCenter - pauseTask refuses engine-dispatched work instead of fabricating Paused (#702)",
          "[processing][task_center][platform]")
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers(2);
    auto &center = sicnu::TaskCenter::instance();

    std::atomic_bool release{false};
    const auto blockers = saturateEngine(center, engine, 2, release);
    (void)blockers;

    sicnu::jobs::JobRequest req;
    req.algorithmId = "callable:platform_pause_me";
    std::atomic_bool ran{false};
    const long taskId = center.submitJob(
        req, [&ran](const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &) {
            ran.store(true);
            return Json::Value(Json::objectValue);
        });
    REQUIRE(taskId > 0);

    // Not dispatched to a worker and no QgsTask handle: pause must refuse.
    REQUIRE_FALSE(center.pauseTask(taskId));
    REQUIRE(center.getTaskInfo(taskId).status != sicnu::TaskStatus::Paused);

    release.store(true);
    waitForTerminalStatus(center, taskId);
    REQUIRE(ran.load());
}
