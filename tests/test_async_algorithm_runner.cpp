// test_async_algorithm_runner.cpp — Async algorithm runner tests
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include "app/dialogs/async_algorithm_runner.h"

static QApplication *ensureApp()
{
    if (!qApp) {
        static int argc = 1;
        static char appName[] = "test_runner";
        static char *argv[] = { appName, nullptr };
        new QApplication(argc, argv);
    }
    return static_cast<QApplication*>(qApp);
}

TEST_CASE("AsyncAlgorithmRunner construction and state", "[async][runner]")
{
    ensureApp();

    SECTION("Can be created with null parent widget")
    {
        AsyncAlgorithmRunner runner(nullptr);
        REQUIRE_FALSE(runner.isRunning());
    }
}

TEST_CASE("AsyncAlgorithmRunner signal connections", "[async][runner]")
{
    ensureApp();

    AsyncAlgorithmRunner runner(nullptr);

    SECTION("completed signal spy is valid")
    {
        QSignalSpy completedSpy(&runner, &AsyncAlgorithmRunner::completed);
        REQUIRE(completedSpy.isValid());
        REQUIRE(completedSpy.count() == 0);
    }

    SECTION("progressChanged signal spy is valid")
    {
        QSignalSpy progressSpy(&runner, &AsyncAlgorithmRunner::progressChanged);
        REQUIRE(progressSpy.isValid());
        REQUIRE(progressSpy.count() == 0);
    }

    SECTION("failed signal spy is valid")
    {
        QSignalSpy failedSpy(&runner, &AsyncAlgorithmRunner::failed);
        REQUIRE(failedSpy.isValid());
        REQUIRE(failedSpy.count() == 0);
    }

    SECTION("cancel on idle runner does not crash")
    {
        runner.cancel();
        REQUIRE_FALSE(runner.isRunning());
    }
}
