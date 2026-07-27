#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include "app/panels/task_center_dock.h"
#include "processing/framework/task_center.h"

static void ensureApp() {
    if (!QApplication::instance()) {
        static int argc = 1;
        static char appName[] = "test_task_center_dock";
        static char* argv[] = { appName, nullptr };
        new QApplication(argc, argv);
    }
}

TEST_CASE("TaskCenterDock - UI Workspace Creation and Task Signal Updates", "[app][task_center_dock]") {
    ensureApp();

    sicnu::TaskCenterDock dock;

    REQUIRE(dock.objectName() == QStringLiteral("TaskCenterDock"));
    REQUIRE(dock.autoLoadLayers() == true);

    QVariantMap params;
    params.insert(QStringLiteral("input"), QStringLiteral("/test/path.tif"));
    long taskId = sicnu::TaskCenter::instance().enqueueTask(QStringLiteral("dock_test_algo"), params, true);

    REQUIRE(taskId > 0);
    REQUIRE(sicnu::TaskCenter::instance().getTaskInfo(taskId).algorithmId == QStringLiteral("dock_test_algo"));
}
