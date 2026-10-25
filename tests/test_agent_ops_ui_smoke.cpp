// tests/test_agent_ops_ui_smoke.cpp — offscreen Control Center smoke
#include <catch2/catch_test_macros.hpp>

#include "app/agent_ops/agent_ops_control_center_panel.h"
#include "agent_ops/ops_projection.h"

#include <QApplication>

TEST_CASE("agent_ops UI smoke: panel accepts projection", "[agent_ops][ui]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, nullptr);

    sicnu::app::agent_ops::AgentOpsControlCenterPanel panel;
    sicnu::agent_ops::OpsProjection proj;
    proj.sessionId = "sess-ui";
    proj.currentStage = "verify";
    Json::Value row(Json::objectValue);
    row["seq"] = 1;
    row["stage"] = "verify";
    row["event"] = "stage_enter";
    row["summary"] = "enter";
    proj.timeline.append(row);
    proj.controls["export"] = true;
    proj.controls["bypasses_loop"] = false;
    panel.setProjection(proj);
    REQUIRE(panel.isVisible() == false); // not shown; constructed ok
    SUCCEED("panel constructed and projection applied");
}

#include "app/agent_ops/agent_ops_control_center_dock.h"
#include <chrono>

TEST_CASE("agent_ops UI dock: startWorker does not block on prior worker", "[agent_ops][dock][join_lag][issue1493]")
{
    if (!qApp)
    {
        int argc = 0;
        qputenv("QT_QPA_PLATFORM", "offscreen");
        new QApplication(argc, nullptr);
    }

    sicnu::app::agent_ops::AgentOpsControlCenterDock dock;
    std::atomic<int> runs{0};

    // Start a first worker with a deterministic, generous runtime: 25 x 20ms
    // cancel-checked quanta = up to 500ms. #1699: the old shape (5 x 10ms
    // sleeps, 40ms ceiling) put the ceiling INSIDE the blocking range, so a
    // loaded runner or ASan flaked it. The split below is structural:
    // a startWorker that reaps asynchronously returns in ~thread-spawn time,
    // while one that joins the prior worker without cancelling it first
    // cannot return before the full 500ms runtime — 200ms separates both
    // regimes with >=2.5x margin on each side, independent of machine load.
    dock.startWorker([&](const std::atomic<bool> &cancel) {
        ++runs;
        for (int i = 0; i < 25 && !cancel.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // Start a second worker immediately: must reap prior worker without blocking GUI thread
    auto t0 = std::chrono::steady_clock::now();
    dock.startWorker([&](const std::atomic<bool> &) {
        ++runs;
    });
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    // Async reap is ~0ms; a blocking join of the uncanceled worker is
    // >=500ms. 200ms rejects the second regime with margin (see above).
    CHECK(elapsed < 200);
    dock.joinWorker();
    CHECK(runs.load() >= 1);
}

TEST_CASE("agent_ops UI dock: rapid restart cancels prior worker and preserves running flag", "[agent_ops][dock][cancellation_stress]")
{
    if (!qApp)
    {
        int argc = 0;
        qputenv("QT_QPA_PLATFORM", "offscreen");
        new QApplication(argc, nullptr);
    }

    auto dock = std::make_unique<sicnu::app::agent_ops::AgentOpsControlCenterDock>();
    std::atomic<bool> worker1Cancelled{false};
    std::atomic<bool> worker1Finished{false};
    std::atomic<int> worker1Ticks{0};

    // Worker 1 loops for up to 500ms unless cancelled
    dock->startWorker([&](const std::atomic<bool> &cancel) {
        for (int i = 0; i < 50; ++i)
        {
            if (cancel.load())
            {
                worker1Cancelled.store(true);
                break;
            }
            worker1Ticks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker1Finished.store(true);
    });

    // Give worker 1 just enough time to enter the loop
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    REQUIRE(dock->isWorkerRunning());

    // Worker 2 starts while worker 1 is running
    std::atomic<bool> worker2Running{true};
    dock->startWorker([&](const std::atomic<bool> &cancel) {
        while (worker2Running.load() && !cancel.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Worker 1 MUST have been cancelled
    // Wait up to 100ms for worker 1 to observe cancel and finish
    for (int i = 0; i < 15 && !worker1Finished.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(worker1Cancelled.load() == true);
    CHECK(worker1Ticks.load() < 10);

    // Worker 2 is still running: isWorkerRunning() must still be true!
    CHECK(dock->isWorkerRunning() == true);

    worker2Running.store(false);
    dock->joinWorker();
    CHECK(dock->isWorkerRunning() == false);
}

TEST_CASE("agent_ops UI dock: worker running flag corruption when prior worker completes", "[agent_ops][dock][running_flag_corruption]")
{
    if (!qApp)
    {
        int argc = 0;
        qputenv("QT_QPA_PLATFORM", "offscreen");
        new QApplication(argc, nullptr);
    }

    auto dock = std::make_unique<sicnu::app::agent_ops::AgentOpsControlCenterDock>();
    std::atomic<bool> worker1Finished{false};

    // Worker 1 runs for 50ms
    dock->startWorker([&](const std::atomic<bool> &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        worker1Finished.store(true);
    });

    // Wait 10ms so worker 1 has started
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(dock->isWorkerRunning());

    // Worker 2 starts: it runs for 200ms
    std::atomic<bool> worker2Running{true};
    dock->startWorker([&](const std::atomic<bool> &) {
        while (worker2Running.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Wait for worker 1 to finish (sleep 100ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(worker1Finished.load());

    // Worker 2 is STILL RUNNING!
    // Buggy behavior: Worker 1 executed `mWorkerRunning.store(false)` upon its exit!
    CHECK(dock->isWorkerRunning() == true);

    worker2Running.store(false);
    dock->joinWorker();
}

TEST_CASE("agent_ops UI dock: rapid destruction during active worker and reaper retirement", "[agent_ops][dock][reaper_lifetime][issue1493]")
{
    if (!qApp)
    {
        int argc = 0;
        qputenv("QT_QPA_PLATFORM", "offscreen");
        new QApplication(argc, nullptr);
    }

    for (int iter = 0; iter < 10; ++iter)
    {
        auto dock = std::make_unique<sicnu::app::agent_ops::AgentOpsControlCenterDock>();
        std::atomic<bool> workerAlive{true};

        // Start 3 workers in rapid succession so two are reaped in background
        dock->startWorker([&](const std::atomic<bool> &cancel) {
            while (!cancel.load() && workerAlive.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        dock->startWorker([&](const std::atomic<bool> &cancel) {
            while (!cancel.load() && workerAlive.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        dock->startWorker([&](const std::atomic<bool> &cancel) {
            while (!cancel.load() && workerAlive.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        // Immediately destroy dock: destructor MUST cancel and join active + all retired workers
        // without crashing, hanging, or leaking detached threads
        dock.reset();
        workerAlive.store(false);
    }
}
