// test_async_algorithm_runner.cpp — Async algorithm runner tests
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QWidget>

#include "app/dialogs/async_algorithm_runner.h"

TEST_CASE("AsyncAlgorithmRunner construction and initial state", "[async][runner]")
{
    if (!QApplication::instance()) {
        static int argc = 1;
        static char arg0[] = "test";
        static char *argv[] = {arg0, nullptr};
        new QApplication(argc, argv);
    }

    SECTION("Can be created with null parent widget")
    {
        AsyncAlgorithmRunner runner(nullptr);
        REQUIRE_FALSE(runner.isRunning());
    }

    SECTION("Can be created with a parent widget")
    {
        QWidget parentWidget;
        AsyncAlgorithmRunner runner(&parentWidget);
        REQUIRE_FALSE(runner.isRunning());
    }
}

TEST_CASE("AsyncAlgorithmRunner signal connections and cancel", "[async][runner]")
{
    if (!QApplication::instance()) {
        static int argc = 1;
        static char arg0[] = "test";
        static char *argv[] = {arg0, nullptr};
        new QApplication(argc, argv);
    }

    AsyncAlgorithmRunner runner(nullptr);
    QSignalSpy completedSpy(&runner, &AsyncAlgorithmRunner::completed);
    QSignalSpy failedSpy(&runner, &AsyncAlgorithmRunner::failed);
    QSignalSpy progressSpy(&runner, &AsyncAlgorithmRunner::progressChanged);

    REQUIRE(completedSpy.isValid());
    REQUIRE(failedSpy.isValid());
    REQUIRE(progressSpy.isValid());

    // Calling cancel on idle runner is safe
    runner.cancel();
    REQUIRE_FALSE(runner.isRunning());
}
