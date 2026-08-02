#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_center.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "workflow/workflow_definition.h"

#include <QObject>

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

    class ProfiledAdapter : public sicnu::TaskAlgorithmAdapter
    {
      public:
        ProfiledAdapter( QString id, sicnu::ProviderResourceProfile profile )
          : m_id( std::move( id ) )
          , m_profile( profile )
        {
        }
        sicnu::AlgorithmDescriptor descriptor() const override
        {
            sicnu::AlgorithmDescriptor d;
            d.id = m_id;
            d.name = m_id;
            d.resourceProfile = m_profile;
            return d;
        }
        bool validateParameters( const QVariantMap &, QString & ) const override { return true; }
        bool execute( const QVariantMap &, std::function<void( double )>, QString & ) override { return true; }

      private:
        QString m_id;
        sicnu::ProviderResourceProfile m_profile;
    };

    auto &algEngine = sicnu::AlgorithmEngine::instance();
    algEngine.clear();
    algEngine.registerAlgorithm(
      std::make_shared<ProfiledAdapter>( QStringLiteral( "throttle:inproc" ),
                                         sicnu::ProviderResourceProfile::InProcessThread ) );
    algEngine.registerAlgorithm(
      std::make_shared<ProfiledAdapter>( QStringLiteral( "throttle:cli" ),
                                         sicnu::ProviderResourceProfile::ExternalCliSubprocess ) );

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

    engine.registerExecutor( "throttle:cli", holdExecutor( inFlightCli, maxCli, releaseWorkers ) );
    engine.registerExecutor( "throttle:inproc", holdExecutor( inFlightInproc, maxInproc, releaseWorkers ) );

    // Enqueue three CLI tasks — only 1 may run at a time.
    QList<long> cliIds;
    for ( int i = 0; i < 3; ++i )
        cliIds.append( center.enqueueTask( QStringLiteral( "throttle:cli" ), {}, false,
                                           sicnu::TaskPriority::Normal, {}, true ) );

    // Enqueue three in-process tasks — up to 2 may run concurrently.
    QList<long> inprocIds;
    for ( int i = 0; i < 3; ++i )
        inprocIds.append( center.enqueueTask( QStringLiteral( "throttle:inproc" ), {}, false,
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
        if ( st == sicnu::TaskStatus::Queued )
            ++cliQueued;
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
    algEngine.clear();
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
