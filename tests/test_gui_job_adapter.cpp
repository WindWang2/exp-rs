// tests/test_gui_job_adapter.cpp
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <chrono>
#include <thread>
#include "shell/gui_job_adapter.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
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
        // Inject a synchronous executor: the test process never registers the
        // app's fallback executor, so a bare "gdal:contrast_stretch" request
        // fails asynchronously in the engine and races any manual state
        // transitions — the source of this test's historical flakiness.
        jobs::JobRequest req;
        req.algorithmId = "test:fast_success";
        req.params["OUTPUT"] = "/tmp/test_out.tif";

        bool successCalled = false;
        QString receivedPath;

        long taskId = handle.submitJob(
            req,
            [](const jobs::JobRequest &, sicnu::operators::RSOperatorContext &) {
                Json::Value result(Json::objectValue);
                result["output"] = "/tmp/test_out.tif";
                return result;
            },
            nullptr, true,
            [&](const QString &outPath, const Json::Value &) {
                successCalled = true;
                receivedPath = outPath;
            },
            nullptr);

        // #453: the id of a submission that already reached a terminal state
        // during catch-up must still be reported.
        REQUIRE(taskId > 0);

        // The engine worker resolves the job asynchronously; pump queued
        // signals until the completion callback lands.
        for ( int i = 0; i < 2000 && !successCalled; ++i )
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
        req.algorithmId = "test:fast_fail";

        bool failureCalled = false;
        bool reportedCanceled = false;
        QString errorReceived;

        long taskId = handle.submitJob(
            req,
            [](const jobs::JobRequest &, sicnu::operators::RSOperatorContext &) -> Json::Value {
                // Block until cancellation lands, mirroring a long job.
                for ( int i = 0; i < 2000; ++i )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
                return Json::Value();
            },
            nullptr, true,
            nullptr,
            [&](const QString &err, bool wasCanceled) {
                failureCalled = true;
                errorReceived = err;
                reportedCanceled = wasCanceled;
            });

        REQUIRE(taskId > 0);
        REQUIRE(handle.isRunning());

        handle.cancel();
        for ( int i = 0; i < 2000 && !failureCalled; ++i )
        {
          QCoreApplication::processEvents();
          std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        CHECK(failureCalled);
        CHECK(reportedCanceled);
        CHECK_FALSE(handle.isRunning());
    }
}

TEST_CASE("GuiJobHandle returns the submitted id even when the job is terminal on catch-up (#453)", "[jobs][guijob][453]") {
    if (!QCoreApplication::instance()) {
        int argc = 1;
        static char arg0[] = "test_gui_job_adapter";
        char *argv[] = { arg0, nullptr };
        new QCoreApplication(argc, argv);
    }

    GuiJobHandle handle;
    jobs::JobRequest req;
    // Unknown algorithm: the engine fails the job immediately — the exact
    // terminal-state catch-up window that used to swallow the returned id.
    req.algorithmId = "no:such_algorithm";
    req.title = "Fast fail";

    bool failureCalled = false;
    const long taskId = handle.submitJob(
        req, nullptr,
        [&](const QString &, bool) { failureCalled = true; });

    // #453: the submission itself succeeded, so the id must be reported even
    // though the engine fails the unknown algorithm asynchronously (and the
    // catch-up window may already have resolved the task).
    REQUIRE(taskId > 0);
    for (int i = 0; i < 600 && !failureCalled; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(failureCalled);
    CHECK_FALSE(handle.isRunning());
}
