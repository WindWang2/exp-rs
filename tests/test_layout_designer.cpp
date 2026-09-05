// test_layout_designer.cpp — Verification suite for Layout Designer and Tools
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMainWindow>
#include <QPaintEvent>
#include <QSignalSpy>
#include <QDoubleSpinBox>

#include <qgslayout.h>
#include <qgsprintlayout.h>
#include <qgslayoutmanager.h>
#include <qgsproject.h>
#include <qgsapplication.h>
#include <qgsgui.h>
#include <qgslayoutundostack.h>
#include <qgslayoutview.h>
#include <qgslayoutruler.h>
#include <qgslayoutitemlabel.h>
#include <gui/layout/qgslayoutitemwidget.h>
#include <gui/layout/qgslayoutlabelwidget.h>
#include <gui/layout/qgslayoutmapwidget.h>
#include <gui/layout/qgslayoutlegendwidget.h>
#include <gui/layout/qgslayoutitemguiregistry.h>
#include <gui/layout/qgslayoutpagepropertieswidget.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemscalebar.h>
#include <cstdlib>
#include <gui/layout/qgslayoutviewtoolselect.h>
#include <gui/layout/qgslayoutviewtoolpan.h>
#include <gui/layout/qgslayoutviewtoolzoom.h>
#include <gui/layout/qgslayoutviewmouseevent.h>
#include "layout/qgslayoutdesignerdialog.h"

namespace
{
void cleanupQgisAtExit()
{
  // Invalidate PROJ caches while libproj is still alive: otherwise the static
  // QgsCoordinateTransform cache is destroyed after exit() has freed PROJ
  // internals (heap-use-after-free in freeProj()->proj_context_create, and a
  // plain SEGFAULT without sanitizers). exitQgis() also tears down providers.
  QgsApplication::exitQgis();
}

QApplication *ensureApp()
{
  static int argc = 1;
  static char a[] = "layout_test";
  static char *v[] = { a, nullptr };
  if ( qApp )
    return static_cast<QApplication *>( qApp );
  // QgsApplication (not plain QApplication): item widget registration touches
  // theme icons and chart items need the plot registry.
  static auto *app = new QgsApplication( argc, v, true );
  QgsApplication::initQgis();
  QgsGui::instance(); // ensure the layout item GUI registry exists
  static const bool registered = [] {
    std::atexit( cleanupQgisAtExit );
    return true;
  }();
  (void)registered;
  (void)app;
  return app;
}

QAction *findAction( QMenu *menu, const QString &text )
{
  if ( !menu )
    return nullptr;
  const auto actions = menu->actions();
  for ( QAction *action : actions )
  {
    if ( action->text() == text )
      return action;
    if ( QMenu *sub = action->menu() )
    {
      if ( QAction *subAction = findAction( sub, text ) )
        return subAction;
    }
  }
  return nullptr;
}

void triggerAction( QMenu *menu, const QString &text )
{
  QAction *action = findAction( menu, text );
  REQUIRE( action != nullptr );
  action->trigger();
  QApplication::processEvents();
}

bool fuzzyEquals( double a, double b, double epsilon = 0.01 )
{
  return std::abs( a - b ) < epsilon;
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
  layout->setName( QStringLiteral( "designer_case_1" ) );
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

// ---------------------------------------------------------------------------
// Cartographic Layout Studio 2.0 — property editing pipeline regressions.
// These tests cover the acceptance chain:
//   select → inspector shows real properties → edit → real item updates
//   → drag updates inspector → undo/redo → switch selection → delete safety.
// ---------------------------------------------------------------------------

TEST_CASE( "LayoutDesigner: item widget registry populated on designer open", "[layout][inspector]" )
{
  ensureApp();
  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "designer_case_2" ) );
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );

  // The designer constructor must have registered the per-type item widgets;
  // before this fix, the registry was empty and every item showed nothing.
  QgsLayoutItemGuiRegistry *registry = QgsGui::layoutItemGuiRegistry();
  CHECK( registry->metadataIdForItemType( QgsLayoutItemRegistry::LayoutMap ) != -1 );
  CHECK( registry->metadataIdForItemType( QgsLayoutItemRegistry::LayoutLabel ) != -1 );
  CHECK( registry->metadataIdForItemType( QgsLayoutItemRegistry::LayoutLegend ) != -1 );
  CHECK( registry->metadataIdForItemType( QgsLayoutItemRegistry::LayoutScaleBar ) != -1 );

  auto *label = new QgsLayoutItemLabel( layout );
  layout->addLayoutItem( label );
  QgsLayoutItemBaseWidget *widget = registry->createItemWidget( label );
  REQUIRE( widget != nullptr );
  REQUIRE( qobject_cast<QgsLayoutLabelWidget *>( widget ) != nullptr );
  delete widget;

  designer->close();
  QApplication::processEvents();
  layout->removeLayoutItem( label );
}

TEST_CASE( "LayoutDesigner: selection shows type widget and inspector edits reach the item",
           "[layout][inspector][edit]" )
{
  ensureApp();
  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "designer_case_3" ) );
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  designer->window()->show();
  QApplication::processEvents();

  // Add a label through the real menu action path.
  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Label" ) );
  QList<QgsLayoutItemLabel *> labels;
  layout->layoutItems( labels );
  REQUIRE( labels.size() == 1 );
  QgsLayoutItemLabel *label = labels.first();

  // Selecting the item must populate the inspector with the per-type widget
  // (which embeds the common geometry properties widget).
  label->setSelected( true );
  designer->showItemOptions( label );
  QApplication::processEvents();
  REQUIRE( designer->window()->findChild<QgsLayoutLabelWidget *>() != nullptr );
  QgsLayoutItemPropertiesWidget *common = designer->window()->findChild<QgsLayoutItemPropertiesWidget *>();
  REQUIRE( common != nullptr );

  // --- inspector → item: change X through the real spinbox ------------------
  const double originalX = label->positionWithUnits().x();
  auto *xSpin = designer->window()->findChild<QDoubleSpinBox *>( QStringLiteral( "mXPosSpin" ) );
  REQUIRE( xSpin != nullptr );
  REQUIRE( fuzzyEquals( xSpin->value(), originalX ) );
  const double newX = originalX + 33.5;
  xSpin->setValue( newX );
  QApplication::processEvents();
  REQUIRE( fuzzyEquals( label->positionWithUnits().x(), newX ) );

  // --- item → inspector: a programmatic move (like a canvas drag) must be
  // reflected back in the spinbox without any panel rebuild ------------------
  label->attemptMoveBy( 5.0, 0.0 );
  QApplication::processEvents();
  REQUIRE( fuzzyEquals( xSpin->value(), newX + 5.0 ) );

  // --- undo/redo of an inspector edit ----------------------------------------
  // The command snapshots the whole item, so undo restores the pre-edit X
  // (the later programmatic move was not a widget command).
  QUndoStack *stack = layout->undoStack()->stack();
  stack->undo();
  QApplication::processEvents();
  REQUIRE( fuzzyEquals( label->positionWithUnits().x(), originalX ) );
  stack->redo();
  QApplication::processEvents();
  REQUIRE( fuzzyEquals( label->positionWithUnits().x(), newX ) );

  designer->close();
  QApplication::processEvents();
}

TEST_CASE( "LayoutDesigner: switching selection writes only to the new item; delete is safe",
           "[layout][inspector][lifetime]" )
{
  ensureApp();
  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "designer_case_4" ) );
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  designer->window()->show();
  QApplication::processEvents();

  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Label" ) );
  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Label" ) );
  QList<QgsLayoutItemLabel *> labels;
  layout->layoutItems( labels );
  REQUIRE( labels.size() == 2 );
  QgsLayoutItemLabel *a = labels.at( 0 );
  QgsLayoutItemLabel *b = labels.at( 1 );

  // Focus A, then switch to B: the panel must rebind to B.
  a->setSelected( true );
  designer->showItemOptions( a );
  b->setSelected( true );
  a->setSelected( false );
  designer->showItemOptions( b );
  QApplication::processEvents();

  const double ax = a->positionWithUnits().x();
  const double bx = b->positionWithUnits().x();

  auto *xSpin = designer->window()->findChild<QDoubleSpinBox *>( QStringLiteral( "mXPosSpin" ) );
  REQUIRE( xSpin != nullptr );
  xSpin->setValue( bx + 21.0 );
  QApplication::processEvents();

  // Only B moved; A is untouched (no stale write to the previously focused item).
  REQUIRE( fuzzyEquals( a->positionWithUnits().x(), ax ) );
  REQUIRE( fuzzyEquals( b->positionWithUnits().x(), bx + 21.0 ) );

  // Deleting the selected item must not crash and must clear the inspector.
  triggerAction( designer->editMenu(), QStringLiteral( "Delete Selected Items" ) );
  labels.clear();
  layout->layoutItems( labels );
  REQUIRE( labels.size() == 1 );
  // The panel was cleared before the item was destroyed; the spinbox belongs
  // to a widget pending deletion now.
  QApplication::processEvents();
  REQUIRE( designer->window()->findChild<QgsLayoutItemPropertiesWidget *>() == nullptr );
  REQUIRE( designer->window()->findChild<QgsLayoutLabelWidget *>() == nullptr );

  // Touching the designer afterwards stays safe.
  triggerAction( designer->viewMenu(), QStringLiteral( "Zoom to Page" ) );

  designer->close();
  QApplication::processEvents();
}

TEST_CASE( "LayoutDesigner: multi-selection batch edits apply to all selected items",
           "[layout][inspector][multi]" )
{
  ensureApp();
  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "designer_case_5" ) );
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  designer->window()->show();
  QApplication::processEvents();

  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Label" ) );
  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Label" ) );
  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Label" ) );
  QList<QgsLayoutItemLabel *> labels;
  layout->layoutItems( labels );
  REQUIRE( labels.size() == 3 );

  for ( QgsLayoutItemLabel *label : labels )
    label->setSelected( true );
  designer->showItemOptions( labels.first() ); // focused item is one of many
  QApplication::processEvents();

  // With a multi-selection the designer must NOT bind a single-item type
  // widget; it shows the batch panel instead.
  REQUIRE( designer->window()->findChild<QgsLayoutLabelWidget *>() == nullptr );
  auto *opacity = designer->window()->findChild<QDoubleSpinBox *>( QStringLiteral( "mMultiOpacitySpin" ) );
  REQUIRE( opacity != nullptr );

  opacity->setValue( 40.0 );
  QApplication::processEvents();
  for ( QgsLayoutItemLabel *label : labels )
    REQUIRE( fuzzyEquals( label->itemOpacity(), 0.4 ) );

  // One undo of the batch macro restores all items.
  layout->undoStack()->stack()->undo();
  QApplication::processEvents();
  for ( QgsLayoutItemLabel *label : labels )
    REQUIRE( fuzzyEquals( label->itemOpacity(), 1.0 ) );

  designer->close();
  QApplication::processEvents();
}

TEST_CASE( "LayoutDesigner: page properties panel opens from the Layout menu",
           "[layout][inspector][page]" )
{
  ensureApp();
  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "designer_case_6" ) );
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  designer->window()->show();
  QApplication::processEvents();

  triggerAction( designer->layoutMenu(), QStringLiteral( "Page Properties..." ) );
  REQUIRE( designer->window()->findChild<QgsLayoutPagePropertiesWidget *>() != nullptr );

  designer->close();
  QApplication::processEvents();
}

TEST_CASE( "LayoutDesigner: map item gets its dedicated property widget",
           "[layout][inspector][map]" )
{
  ensureApp();
  auto *project = QgsProject::instance();
  auto *layout = new QgsPrintLayout( project );
  layout->initializeDefaults();
  layout->setName( QStringLiteral( "designer_case_7" ) );
  project->layoutManager()->addLayout( layout );

  auto *designer = new QgsLayoutDesignerDialog( layout, nullptr, nullptr );
  designer->window()->show();
  QApplication::processEvents();

  triggerAction( designer->itemsMenu(), QStringLiteral( "Add Map" ) );
  QList<QgsLayoutItemMap *> maps;
  layout->layoutItems( maps );
  REQUIRE( maps.size() == 1 );

  maps.first()->setSelected( true );
  designer->showItemOptions( maps.first() );
  QApplication::processEvents();
  REQUIRE( qobject_cast<QgsLayoutMapWidget *>(
               designer->window()->findChild<QgsLayoutMapWidget *>() ) != nullptr );

  designer->close();
  QApplication::processEvents();
}
