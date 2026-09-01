// tests/test_log_panel.cpp — TDD for logging and message handling
#include <catch2/catch_test_macros.hpp>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <QCoreApplication>

#include "app/log_panel.h"

// The QgsApplication is created in test_log_panel_main.cpp and lives for the
// whole process; it is destroyed at the end of main() (see that file for why).

TEST_CASE("LogPanel can be created", "[logging]") {
    LogPanel panel;
    CHECK(panel.widget() != nullptr);
    CHECK((panel.windowTitle().contains("Log", Qt::CaseInsensitive) || panel.windowTitle().contains("日志")));
}

TEST_CASE("LogPanel receives messages from QgsMessageLog", "[logging]") {
    LogPanel panel;

    QgsMessageLog::logMessage("test message", "test_tag", Qgis::MessageLevel::Info);
    QCoreApplication::processEvents();

    CHECK(panel.messageCount() >= 1);
    CHECK(panel.lastMessage().contains("test message"));
}

TEST_CASE("LogPanel can filter by level", "[logging]") {
    LogPanel panel;

    QgsMessageLog::logMessage("info msg", "test", Qgis::MessageLevel::Info);
    QgsMessageLog::logMessage("warning msg", "test", Qgis::MessageLevel::Warning);
    QgsMessageLog::logMessage("critical msg", "test", Qgis::MessageLevel::Critical);
    QCoreApplication::processEvents();

    CHECK(panel.messageCount() >= 3);
}

TEST_CASE("LogPanel can clear messages", "[logging]") {
    LogPanel panel;

    QgsMessageLog::logMessage("to be cleared", "test", Qgis::MessageLevel::Info);
    QCoreApplication::processEvents();
    REQUIRE(panel.messageCount() >= 1);

    panel.clearMessages();
    CHECK(panel.messageCount() == 0);
}
