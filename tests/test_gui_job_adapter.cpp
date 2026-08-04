// tests/test_gui_job_adapter.cpp
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include "shell/gui_job_adapter.h"
#include "processing/framework/task_center.h"

using namespace sicnu;
using namespace sicnu::app;

TEST_CASE("GuiJobHandle - Lifecycle, Busy-Gating, and Callbacks", "[app][shell][gui_job_adapter]") {
    int argc = 1;
    char arg0[] = "test_gui_job_adapter";
    char *argv[] = { arg0, nullptr };
    if (!QCoreApplication::instance()) {
        new QCoreApplication(argc, argv);
    }

    GuiJobHandle handle;
    CHECK_FALSE(handle.isRunning());
    CHECK(handle.taskId() == -1);

    SECTION("Busy-gating: cannot submit second job while one is running") {
        jobs::JobRequest req;
        req.algorithmId = "gdal:contrast_stretch";
        req.title = "Test Job";

        long id1 = handle.submitJob(req, nullptr, nullptr);
        REQUIRE(id1 > 0);
        CHECK(handle.isRunning());
        CHECK(handle.taskId() == id1);

        long id2 = handle.submitJob(req, nullptr, nullptr);
        CHECK(id2 == -1); // Busy-gated

        handle.cancel();
        CHECK_FALSE(handle.isRunning());
    }

    SECTION("Successful task completion callback and signal emission") {
        jobs::JobRequest req;
        req.algorithmId = "gdal:contrast_stretch";
        req.params["OUTPUT"] = "/tmp/test_out.tif";

        bool successCalled = false;
        QString receivedPath;

        long taskId = handle.submitJob(req, [&](const QString &outPath, const Json::Value &) {
            successCalled = true;
            receivedPath = outPath;
        }, nullptr);

        REQUIRE(taskId > 0);

        // Simulate TaskCenter completion
        TaskCenter::instance().markTaskRunning(taskId);
        Json::Value payload(Json::objectValue);
        payload["output"] = "/tmp/test_out.tif";
        TaskCenter::instance().markTaskCompleted(taskId, QVariantMap(), payload);

        // Process pending Qt events to deliver QueuedConnection signal
        for ( int i = 0; i < 50 && !successCalled; ++i )
        {
          QCoreApplication::processEvents();
          std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        CHECK(successCalled);
        CHECK(receivedPath == "/tmp/test_out.tif");
        CHECK_FALSE(handle.isRunning());
    }

    SECTION("Task failure callback and cancellation") {
        jobs::JobRequest req;
        req.algorithmId = "gdal:contrast_stretch";

        bool failureCalled = false;
        bool reportedCanceled = false;
        QString errorReceived;

        long taskId = handle.submitJob(req, nullptr, [&](const QString &err, bool wasCanceled) {
            failureCalled = true;
            errorReceived = err;
            reportedCanceled = wasCanceled;
        });

        REQUIRE(taskId > 0);

        handle.cancel();
        for ( int i = 0; i < 50 && !failureCalled; ++i )
        {
          QCoreApplication::processEvents();
          std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        CHECK(failureCalled);
        CHECK(reportedCanceled);
        CHECK_FALSE(handle.isRunning());
    }
}
