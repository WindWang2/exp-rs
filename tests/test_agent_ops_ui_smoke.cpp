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
