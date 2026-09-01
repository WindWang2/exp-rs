// test_layout_designer.cpp — Verification suite for Layout Designer and Tools
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMainWindow>
#include <QPaintEvent>
#include <QSignalSpy>

#include <qgslayout.h>
#include <qgsprintlayout.h>
#include <qgslayoutmanager.h>
#include <qgsproject.h>
#include <qgslayoutview.h>
#include <qgslayoutruler.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemscalebar.h>
#include <gui/layout/qgslayoutviewtoolselect.h>
#include <gui/layout/qgslayoutviewtoolpan.h>
#include <gui/layout/qgslayoutviewtoolzoom.h>
#include <gui/layout/qgslayoutviewmouseevent.h>
#include "layout/qgslayoutdesignerdialog.h"

namespace
{
QApplication *ensureApp()
{
  static int argc = 1;
  static char a[] = "layout_test";
  static char *v[] = { a, nullptr };
  return qApp ? qApp : new QApplication( argc, v );
}
} // namespace

TEST_CASE( "LayoutDesigner: QgsLayoutViewToolSelect initialization and mouse handling safety", "[layout][tool][select]" )
{
  ensureApp();

  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();

  auto *view = new QgsLayoutView();
  view->setCurrentLayout( layout );

  // Construct tool AFTER layout is set on view
  auto *selectTool = new QgsLayoutViewToolSelect( view );
  REQUIRE( selectTool->mouseHandles() != nullptr );

  view->setTool( selectTool );

  // Simulate mouse move event that previously caused crash when mouseHandles was null
  QMouseEvent qMoveEvent( QEvent::MouseMove, QPointF( 100, 100 ), QPointF( 100, 100 ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
  QgsLayoutViewMouseEvent moveEvent( view, &qMoveEvent );
  selectTool->layoutMoveEvent( &moveEvent );

  // Simulate press and release
  QMouseEvent qPressEvent( QEvent::MouseButtonPress, QPointF( 100, 100 ), QPointF( 100, 100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
  QgsLayoutViewMouseEvent pressEvent( view, &qPressEvent );
  selectTool->layoutPressEvent( &pressEvent );

  QMouseEvent qReleaseEvent( QEvent::MouseButtonRelease, QPointF( 100, 100 ), QPointF( 100, 100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
  QgsLayoutViewMouseEvent releaseEvent( view, &qReleaseEvent );
  selectTool->layoutReleaseEvent( &releaseEvent );

  delete view;
  delete layout;
}

TEST_CASE( "LayoutDesigner: QgsLayoutRuler paint with empty and initialized pages", "[layout][ruler]" )
{
  ensureApp();

  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );

  auto *view = new QgsLayoutView();
  view->setCurrentLayout( layout );

  auto *hRuler = new QgsLayoutRuler( nullptr, Qt::Horizontal );
  auto *vRuler = new QgsLayoutRuler( nullptr, Qt::Vertical );
  view->setHorizontalRuler( hRuler );
  view->setVerticalRuler( vRuler );

  // Initially 0 pages — must not crash on paint
  hRuler->resize( 800, 20 );
  vRuler->resize( 20, 600 );
  hRuler->repaint();
  vRuler->repaint();

  // Initialize pages and test repaint again
  layout->initializeDefaults();
  hRuler->repaint();
  vRuler->repaint();

  delete hRuler;
  delete vRuler;
  delete view;
  delete layout;
}

TEST_CASE( "LayoutDesigner: QgsLayoutDesignerDialog lifecycle and item creation", "[layout][designer][dialog]" )
{
  ensureApp();

  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  REQUIRE( designer->window() != nullptr );
  REQUIRE( designer->view() != nullptr );
  REQUIRE( designer->layout() == layout );

  // Ensure window can be shown and rendered without crashing
  designer->window()->show();
  QApplication::processEvents();

  // Select tool activation
  designer->activateTool( QgsLayoutDesignerInterface::ToolMoveItemContent );
  QApplication::processEvents();

  // Close and destroy
  designer->close();
  QApplication::processEvents();
}
