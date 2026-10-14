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

    // Start a first worker that sleeps briefly
    dock.startWorker([&](const std::atomic<bool> &cancel) {
        ++runs;
        for (int i = 0; i < 5 && !cancel.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Start a second worker immediately: must reap prior worker without blocking GUI thread
    auto t0 = std::chrono::steady_clock::now();
    dock.startWorker([&](const std::atomic<bool> &) {
        ++runs;
    });
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    // startWorker must return near-instantly (< 40ms), not wait for the worker
    CHECK(elapsed < 40);
    dock.joinWorker();
    CHECK(runs.load() >= 1);
}
