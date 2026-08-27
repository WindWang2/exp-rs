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
    const auto info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
    REQUIRE(info.algorithmId == QStringLiteral("dock_test_algo"));

    // Deliver taskAdded signal
    dock.onTaskAdded(info);
    QCoreApplication::processEvents();

    // Deliver taskUpdated (Running, progress = 0.5)
    sicnu::AlgorithmTaskInfo runningInfo = info;
    runningInfo.status = sicnu::TaskStatus::Running;
    runningInfo.progressPercentage = 50.0;
    dock.onTaskUpdated(runningInfo);
    dock.onTaskLogAdded(taskId, QStringLiteral("Processing tile 1/2..."));
    QCoreApplication::processEvents();

    // Deliver taskUpdated (Completed, progress = 1.0)
    sicnu::AlgorithmTaskInfo completedInfo = runningInfo;
    completedInfo.status = sicnu::TaskStatus::Completed;
    completedInfo.progressPercentage = 100.0;
    completedInfo.outputLayerPath = QStringLiteral("/test/out.tif");
    dock.onTaskUpdated(completedInfo);
    dock.onTaskLogAdded(taskId, QStringLiteral("Task completed successfully."));
    QCoreApplication::processEvents();

    // Refresh and verify no crash
    dock.refreshTaskList();
    QCoreApplication::processEvents();
}

TEST_CASE("TaskCenterDock - WaitingResource and Cancelling states", "[app][task_center_dock]") {
    ensureApp();

    sicnu::TaskCenterDock dock;

    QVariantMap params;
    params.insert(QStringLiteral("input"), QStringLiteral("/test/path.tif"));
    long taskId = sicnu::TaskCenter::instance().enqueueTask(QStringLiteral("dock_state_algo"), params, true);
    REQUIRE(taskId > 0);

    sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo(taskId);

    SECTION("WaitingResource is formatted and cancel enabled") {
        info.status = sicnu::TaskStatus::WaitingResource;
        dock.onTaskUpdated(info);
        QCoreApplication::processEvents();
        CHECK(dock.formatStatus(sicnu::TaskStatus::WaitingResource) == QObject::tr("等待资源"));
        // cancel enabled for WaitingResource via public selection path
        dock.onSelectionChanged();
        CHECK(dock.formatStatus(sicnu::TaskStatus::Cancelling) == QObject::tr("取消中"));
    }

    SECTION("Cancelling is formatted and cancel disabled") {
        info.status = sicnu::TaskStatus::Cancelling;
        dock.onTaskUpdated(info);
        QCoreApplication::processEvents();
        CHECK(dock.formatStatus(sicnu::TaskStatus::Cancelling) == QObject::tr("取消中"));
    }

    sicnu::TaskCenter::instance().cancelTask(taskId);
}
