// test_obia_main_window.cpp — Phase 10B Task 10B.5 UI smoke tests
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "app/obia/rs_obia_main_window.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QSpinBox>
#include <QToolBar>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_obia_main_window";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
    if ( !QApplication::instance() )
        return new QApplication( fake_argc, fake_argv );
    return static_cast<QApplication *>( QApplication::instance() );
}

} // namespace

TEST_CASE( "ObiaMainWindow: construction and window chrome", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;
    REQUIRE( window.windowTitle().contains( "OBIA" ) );
    REQUIRE( window.centralWidget() != nullptr );
    REQUIRE( window.statusBar() != nullptr );
}

TEST_CASE( "ObiaMainWindow: toolbar segmentation and classifier widgets", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;
    window.show();
    QApplication::processEvents();

    auto *toolbar = window.findChild<QToolBar *>( "obiaToolbar" );
    REQUIRE( toolbar != nullptr );

    auto *kernelSpin = window.findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = window.findChild<QSpinBox *>( "binsSpin" );
    auto *minRegionSpin = window.findChild<QSpinBox *>( "minRegionSpin" );
    auto *classifierCombo = window.findChild<QComboBox *>( "classifierCombo" );

    REQUIRE( kernelSpin != nullptr );
    REQUIRE( binsSpin != nullptr );
    REQUIRE( minRegionSpin != nullptr );
    REQUIRE( classifierCombo != nullptr );
    REQUIRE( kernelSpin->value() == 5 );
    REQUIRE( binsSpin->value() == 32 );
    REQUIRE( minRegionSpin->value() == 100 );
    // Classifier combo lists the available backends (NormalBayes, SVM,
    // RandomForest, KMeans) - keep this in sync with the addItems() call in
    // rs_obia_main_window.cpp.
    REQUIRE( classifierCombo->count() == 4 );
}

TEST_CASE( "ObiaMainWindow: dock panels exist", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;
    window.show();
    QApplication::processEvents();

    REQUIRE( window.findChild<QDockWidget *>( "obiaClassDock" ) != nullptr );
    REQUIRE( window.findChild<QDockWidget *>( "obiaSegmentDock" ) != nullptr );
}

TEST_CASE( "ObiaMainWindow: initial task state queries and cancelActiveTask", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;

    CHECK_FALSE( window.isBusy() );
    CHECK( window.pendingTaskId() == -1 );
    CHECK( window.pendingOp() == RsObiaMainWindow::PendingOp::None );

    window.cancelActiveTask();

    CHECK_FALSE( window.isBusy() );
    CHECK( window.pendingTaskId() == -1 );
    CHECK( window.pendingOp() == RsObiaMainWindow::PendingOp::None );
}

#include "app/obia/rs_segment_select_tool.h"
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>

TEST_CASE( "OBIA/MapTools: RsSegmentSelectTool rubber band canvas destruction safety", "[obia][maptool]" )
{
    ensureApp();

    auto canvas = std::make_unique<QgsMapCanvas>();
    auto tool = std::make_unique<RsSegmentSelectTool>( canvas.get() );

    QVector<quint32> labels = { 1, 1, 1, 1 };
    RsSegmentMap segMap( labels, 2, 2 );
    tool->setSegmentMap( segMap );

    tool->clearSelection();

    // Clean teardown: destroy tool before canvas
    tool.reset();
    canvas.reset();
}

#endif // SICNU_HAS_OPENCV