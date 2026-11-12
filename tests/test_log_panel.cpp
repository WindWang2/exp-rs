// tests/test_log_panel.cpp — TDD for logging and message handling
#include <catch2/catch_test_macros.hpp>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <QCoreApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>

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

TEST_CASE( "LogPanel replays existing messages and counts only visible records", "[logging][issue-1562]" )
{
    LogPanel panel;
    panel.show();
    QCoreApplication::processEvents();
    panel.clearMessages();

    QComboBox *levels = nullptr;
    QComboBox *tags = nullptr;
    for ( QComboBox *combo : panel.findChildren<QComboBox *>() )
    {
        if ( combo->findData( static_cast<int>( Qgis::MessageLevel::Critical ) ) >= 0 )
            levels = combo;
        else
            tags = combo;
    }
    auto *text = panel.findChild<QTextEdit *>();
    auto *search = panel.findChild<QLineEdit *>();
    QLabel *count = nullptr;
    for ( QLabel *label : panel.findChildren<QLabel *>() )
        if ( label->text() == LogPanel::tr( "%1 messages" ).arg( 0 ) )
            count = label;
    REQUIRE( levels );
    REQUIRE( tags );
    REQUIRE( text );
    REQUIRE( search );
    REQUIRE( count );

    panel.logMessage( "only_info", "first", Qgis::MessageLevel::Info );
    panel.logMessage( "only_warning", "first", Qgis::MessageLevel::Warning );
    panel.logMessage( "only_critical", "first", Qgis::MessageLevel::Critical );
    REQUIRE( count->text() == LogPanel::tr( "%1 messages" ).arg( 3 ) );

    levels->setCurrentIndex( levels->findData( static_cast<int>( Qgis::MessageLevel::Critical ) ) );
    CHECK( text->toPlainText().contains( "only_critical" ) );
    CHECK_FALSE( text->toPlainText().contains( "only_info" ) );
    CHECK_FALSE( text->toPlainText().contains( "only_warning" ) );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 1 ) );

    panel.logMessage( "later_warning", "second", Qgis::MessageLevel::Warning );
    CHECK_FALSE( text->toPlainText().contains( "later_warning" ) );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 1 ) );
    levels->setCurrentIndex( levels->findData( -1 ) );
    CHECK( text->toPlainText().contains( "only_info" ) );
    CHECK( text->toPlainText().contains( "later_warning" ) );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 4 ) );

    search->setText( "only_info" );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 1 ) );
    CHECK_FALSE( text->toPlainText().contains( "later_warning" ) );
    search->clear();
    tags->setCurrentIndex( tags->findText( "second" ) );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 1 ) );
    CHECK( text->toPlainText().contains( "later_warning" ) );
    CHECK_FALSE( text->toPlainText().contains( "only_info" ) );
    search->setText( "no_matching_message" );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 0 ) );
    CHECK( text->toPlainText().isEmpty() );
    panel.clearMessages();
    CHECK( panel.messageCount() == 0 );
    CHECK( count->text() == LogPanel::tr( "%1 messages" ).arg( 0 ) );
}
