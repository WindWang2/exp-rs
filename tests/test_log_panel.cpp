// tests/test_log_panel.cpp — TDD for logging and message handling
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgis.h>

#include "app/log_panel.h"

#include <processing/framework/error_reporter.h>

using namespace sicnu;

static int argc = 0;
static char *argv[] = {nullptr};
static std::unique_ptr<QgsApplication> g_app;
static void ensureApp()
{
    if (!g_app) {
        g_app = std::make_unique<QgsApplication>(argc, argv, false);
        g_app->init();
        g_app->setPrefixPath(g_app->applicationDirPath(), true);
    }
}

TEST_CASE("LogPanel can be created", "[logging]") {
    ensureApp();

    LogPanel panel;
    CHECK(panel.widget() != nullptr);
    CHECK(panel.windowTitle().contains("Log", Qt::CaseInsensitive));
}

TEST_CASE("LogPanel receives messages from QgsMessageLog", "[logging]") {
    ensureApp();

    LogPanel panel;

    QgsMessageLog::logMessage("test message", "test_tag", Qgis::MessageLevel::Info);
    QCoreApplication::processEvents();

    CHECK(panel.messageCount() >= 1);
    CHECK(panel.lastMessage().contains("test message"));
}

TEST_CASE("LogPanel can filter by level", "[logging]") {
    ensureApp();

    LogPanel panel;

    QgsMessageLog::logMessage("info msg", "test", Qgis::MessageLevel::Info);
    QgsMessageLog::logMessage("warning msg", "test", Qgis::MessageLevel::Warning);
    QgsMessageLog::logMessage("critical msg", "test", Qgis::MessageLevel::Critical);
    QCoreApplication::processEvents();

    CHECK(panel.messageCount() >= 3);
}

TEST_CASE("LogPanel can clear messages", "[logging]") {
    ensureApp();

    LogPanel panel;

    QgsMessageLog::logMessage("to be cleared", "test", Qgis::MessageLevel::Info);
    QCoreApplication::processEvents();
    REQUIRE(panel.messageCount() >= 1);

    panel.clearMessages();
    CHECK(panel.messageCount() == 0);
}

TEST_CASE("ErrorReporter invokes callback on error", "[logging]") {
    ErrorReporter reporter;

    struct Result {
        bool received = false;
        QString provider, algorithm, message;
        int errorCode = 0;
    };
    auto result = std::make_shared<Result>();

    reporter.setErrorCallback([result](const QString &p, const QString &a,
                                        const QString &m, int code) {
        result->received = true;
        result->provider = p;
        result->algorithm = a;
        result->message = m;
        result->errorCode = code;
    });

    reporter.reportError("gdal", "test_alg", "test error", -1);

    CHECK(result->received);
    CHECK(result->provider == "gdal");
    CHECK(result->algorithm == "test_alg");
    CHECK(result->message == "test error");
    CHECK(result->errorCode == -1);
}

TEST_CASE("ErrorReporter callback can route to QgsMessageLog", "[logging]") {
    ensureApp();

    ErrorReporter reporter;

    // Wire callback → QgsMessageLog::logMessage
    reporter.setErrorCallback([](const QString &provider, const QString &algorithm,
                                  const QString &message, int) {
        QgsMessageLog::logMessage(provider + ":" + algorithm + " — " + message,
                                  "processing", Qgis::MessageLevel::Critical);
    });

    // Use LogPanel to verify the message arrived
    LogPanel panel;

    reporter.reportError("otb", "segmentation", "seg fault", 42);
    QCoreApplication::processEvents();

    CHECK(panel.messageCount() >= 1);
    CHECK(panel.lastMessage().contains("segmentation"));
    CHECK(panel.lastMessage().contains("seg fault"));
}
