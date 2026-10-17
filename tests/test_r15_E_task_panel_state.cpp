// test_r15_E_task_panel_state.cpp — R15-E / E05 oracle.
//
// Reproduced on master 46791bc65c6f552ebbcf723bb91892736d56f319:
// TaskPanelHost::setFailed() only repainted the hint line. A previous run's
// structured result card ("✓ Processing finished" + artifact list) stayed on
// screen underneath the red error, so the panel showed a green success and a
// failure at the same time.
//
// The progress-text rule that used to live here moved to the lighter lane
// (tests/test_r15_E_progress_text.cpp) so it can be asserted without building
// the whole desktop shell.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QWidget>

#include "app/shell/task_panel_host.h"

namespace
{

QApplication &testApp()
{
    static int argc = 1;
    static char appName[] = "test_r15_E_task_panel_state";
    static char *argv[] = { appName, nullptr };
    static QApplication app( argc, argv );
    return app;
}

} // namespace

TEST_CASE( "TaskPanelHost drops the success card when the run fails",
           "[r15e][e05][task-panel]" )
{
    testApp();
    TaskPanelHost panel;

    QWidget *summary = panel.findChild<QWidget *>( QStringLiteral( "rsResultSummary" ) );
    REQUIRE( summary != nullptr );

    Json::Value result( Json::objectValue );
    result["mean"] = 0.42;
    panel.showResult( result, QStringLiteral( "rs:image_fusion" ), 1500, false );
    REQUIRE( summary->isVisibleTo( &panel ) );

    // A failure must not sit next to the previous run's green success card.
    panel.setFailed( QStringLiteral( "Run Failed" ) );
    REQUIRE_FALSE( summary->isVisibleTo( &panel ) );
}

TEST_CASE( "TaskPanelHost drops the previous card when a new run starts",
           "[r15e][e05][task-panel]" )
{
    testApp();
    TaskPanelHost panel;
    QWidget *summary = panel.findChild<QWidget *>( QStringLiteral( "rsResultSummary" ) );
    REQUIRE( summary != nullptr );

    Json::Value result( Json::objectValue );
    result["mean"] = 0.42;
    panel.showResult( result, QStringLiteral( "rs:image_fusion" ), 1500, false );
    REQUIRE( summary->isVisibleTo( &panel ) );

    panel.setRunning( true );
    REQUIRE_FALSE( summary->isVisibleTo( &panel ) );
    panel.setRunning( false );
    REQUIRE_FALSE( summary->isVisibleTo( &panel ) );
}

