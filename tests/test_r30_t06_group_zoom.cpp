// test_r30_t06_group_zoom.cpp — R30-T06 / F1 (WP02)
//
// The layer-tree "Zoom to Group" action must be bound to the LIVE active
// canvas and be honestly unavailable when the group cannot be zoomed to.
// Baseline defect: src/app/layer_tree_menu.cpp:95 called
//   defActions->actionZoomToGroup( nullptr )
// and QGIS stores that raw canvas pointer in QAction::data. Triggering it on
// any group with a spatial layer dereferences canvas->mapSettings()
// (qgslayertreeviewdefaultactions.cpp:435/445) — a real null dereference;
// an empty / non-spatial group silently does nothing, and a canvas deleted
// while the menu was open leaves a dangling void* (use-after-free).
//
// RED stage: state assertions only (enabled / tooltip / statusTip). They fail
// on the baseline WITHOUT crashing the test process (triggering the stock
// action would SIGSEGV). After the production fix a behaviour assertion on
// the resulting canvas extent is added, so the test verifies a real outcome
// rather than a source string.
#include <catch2/catch_test_macros.hpp>

#include <QAbstractProxyModel>
#include <QAction>
#include <QMenu>
#include <QString>

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <qgsmapcanvas.h>
#include <qgspointxy.h>
#include <qgsproject.h>
#include <qgsrectangle.h>
#include <qgsvectorlayer.h>

#include <qgsmaplayerstore.h>

#include "app/active_view_host.h"
#include "app/layer_tree_menu.h"
#include "app/display/qgis_display_manager.h"
#include "data/data_manager.h"

#include <cmath>
#include <memory>

namespace
{

void ensureQgisApplication()
{
  if ( QCoreApplication::instance() )
    return;
  qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
  static int argc = 1;
  static char applicationName[] = "test_r30_t06_group_zoom";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

struct Fixture
{
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsLayerTreeModel model;
  QgsLayerTreeView view;
  ActiveViewHost *host = nullptr;
  std::unique_ptr<QMenu> menu;

  Fixture()
    : model( &tree )
  {
    canvas.resize( 800, 600 );
    canvas.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    view.setModel( &model );
    host = new ActiveViewHost( &canvas, &view, nullptr, nullptr, nullptr,
                               sicnu::display::DisplayViewId(), nullptr );
  }

  ~Fixture()
  {
    menu.reset();
    delete host;
    QgsProject::instance()->clear();
  }
};

QAction *zoomActionFor( Fixture &fixture, QgsLayerTreeGroup *group )
{
  const QModelIndex source = fixture.model.node2index( group );
  REQUIRE( source.isValid() );
  // QgsLayerTreeView::setModel wraps the tree model in a proxy; the current
  // index lives in proxy coordinates (same rule as test_r22_workbench_view_tree).
  auto *proxy = qobject_cast<QAbstractProxyModel *>( fixture.view.model() );
  REQUIRE( proxy != nullptr );
  const QModelIndex index = proxy->mapFromSource( source );
  REQUIRE( index.isValid() );
  fixture.view.setCurrentIndex( index );
  REQUIRE( fixture.view.index2node( fixture.view.currentIndex() ) == group );

  LayerTreeMenuProvider provider( &fixture.view, fixture.host, nullptr );
  fixture.menu.reset( provider.createContextMenu() );
  REQUIRE( fixture.menu != nullptr );
  for ( QAction *action : fixture.menu->actions() )
  {
    QString text = action->text();
    text.remove( QLatin1Char( '&' ) );
    if ( text.contains( QStringLiteral( "Zoom to Group" ), Qt::CaseInsensitive ) )
      return action;
  }
  return nullptr;
}

} // namespace

TEST_CASE( "R30-T06 F1: an empty group is not zoomable and explains why",
           "[r30_t06][group_zoom][f1]" )
{
  ensureQgisApplication();
  Fixture fixture;
  QgsLayerTreeGroup *group = fixture.tree.addGroup( QStringLiteral( "Empty group" ) );
  REQUIRE( group != nullptr );
  REQUIRE( group->findLayers().isEmpty() );

  QAction *zoom = zoomActionFor( fixture, group );
  REQUIRE( zoom != nullptr );
  // Baseline: the stock QGIS action is enabled with no explanation at all.
  CHECK_FALSE( zoom->isEnabled() );
  CHECK_FALSE( zoom->toolTip().isEmpty() );
  CHECK_FALSE( zoom->statusTip().isEmpty() );
}

TEST_CASE( "R30-T06 F1: a non-spatial attribute-table group is not zoomable",
           "[r30_t06][group_zoom][f1]" )
{
  ensureQgisApplication();
  Fixture fixture;
  auto *table = new QgsVectorLayer( QStringLiteral( "None?crs=EPSG:4326" ),
                                    QStringLiteral( "table" ), QStringLiteral( "memory" ) );
  REQUIRE( table->isValid() );
  REQUIRE( table->geometryType() == Qgis::GeometryType::Null );
  QgsProject::instance()->addMapLayer( table, false );
  QgsLayerTreeGroup *group = fixture.tree.addGroup( QStringLiteral( "Tables" ) );
  group->addLayer( table );
  REQUIRE( group->findLayers().size() == 1 );

  QAction *zoom = zoomActionFor( fixture, group );
  REQUIRE( zoom != nullptr );
  CHECK_FALSE( zoom->isEnabled() );
  CHECK_FALSE( zoom->toolTip().isEmpty() );
}

TEST_CASE( "R30-T06 F1: a spatial group targets and names the active canvas",
           "[r30_t06][group_zoom][f1]" )
{
  ensureQgisApplication();
  Fixture fixture;
  auto *points = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                     QStringLiteral( "points" ), QStringLiteral( "memory" ) );
  REQUIRE( points->isValid() );
  QgsFeature first( points->fields() );
  first.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 10.0, 20.0 ) ) );
  QgsFeature second( points->fields() );
  second.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 30.0, 40.0 ) ) );
  QgsFeatureList features{ first, second }; // addFeatures takes a non-const lvalue ref
  REQUIRE( points->dataProvider()->addFeatures( features ) );
  points->updateExtents();
  QgsProject::instance()->addMapLayer( points, false );
  QgsLayerTreeGroup *group = fixture.tree.addGroup( QStringLiteral( "Spatial" ) );
  group->addLayer( points );
  REQUIRE( group->findLayers().size() == 1 );
  REQUIRE_FALSE( points->extent().isNull() );

  QAction *zoom = zoomActionFor( fixture, group );
  REQUIRE( zoom != nullptr );
  CHECK( zoom->isEnabled() );
  // Baseline: no tooltip / statusTip at all — the user cannot tell which
  // canvas the action will touch, nor that a null canvas was wired.
  CHECK_FALSE( zoom->toolTip().isEmpty() );
  CHECK( zoom->toolTip().contains( QStringLiteral( "active map canvas" ), Qt::CaseInsensitive ) );

  // GREEN behaviour: actually TRIGGER the action and verify the ACTIVE
  // canvas was fitted to the group's real extent. On the baseline this would
  // dereference a null canvas (qgslayertreeviewdefaultactions.cpp:435), so
  // this assertion is only safe with the production fix in place.
  const QgsRectangle before = fixture.canvas.extent();
  zoom->trigger();
  const QgsRectangle after = fixture.canvas.extent();
  const QgsPointXY center = after.center();
  CHECK( std::abs( center.x() - 20.0 ) < 0.5 ); // midpoint of (10,30)
  CHECK( std::abs( center.y() - 30.0 ) < 0.5 ); // midpoint of (20,40)
  CHECK( after.width() >= 20.0 );               // 5% margin, then aspect-rebuffer
  CHECK( after.height() >= 20.0 );
  CHECK( after.width() > before.width() );
}

TEST_CASE( "R30-T06 F1: the pure group extent projection refuses unusable inputs",
           "[r30_t06][group_zoom][f1][projection]" )
{
  ensureQgisApplication();
  Fixture fixture;
  QgsLayerTreeGroup *group = fixture.tree.addGroup( QStringLiteral( "Empty" ) );
  QString reason;

  // Null canvas / null group are honest refusals, never dereferences.
  CHECK( LayerTreeMenuProvider::groupZoomExtent( nullptr, group, &reason ).isNull() );
  CHECK_FALSE( reason.isEmpty() );
  reason.clear();
  CHECK( LayerTreeMenuProvider::groupZoomExtent( &fixture.canvas, nullptr, &reason ).isNull() );
  CHECK_FALSE( reason.isEmpty() );
  reason.clear();
  CHECK( LayerTreeMenuProvider::groupZoomExtent( &fixture.canvas, group, &reason ).isNull() );
  CHECK( reason.contains( QStringLiteral( "empty" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "R30-T06 F1: the active canvas follows the active Display View",
           "[r30_t06][group_zoom][f1][multiview]" )
{
  ensureQgisApplication();
  // R1 / P1-3: mapCanvas() is the fixed main canvas, but setExtent() and the
  // host's other view operations resolve the LIVE active view. Group zoom
  // must use that same resolution or a secondary view silently zooms the
  // wrong canvas.
  sicnu::data::DataManager dataManager;
  QgsMapCanvas canvasA;
  QgsMapCanvas canvasB;
  QgsLayerTree treeA;
  QgsLayerTree treeB;
  QgsMapLayerStore storeA;
  QgsMapLayerStore storeB;
  sicnu::display::QgisDisplayManager displayManager( &dataManager );

  sicnu::display::DisplayViewSpec specA;
  specA.canvas = &canvasA;
  specA.layerTree = &treeA;
  specA.layerStore = &storeA;
  const auto createdA = displayManager.createView( specA );
  REQUIRE( createdA );
  sicnu::display::DisplayViewSpec specB;
  specB.canvas = &canvasB;
  specB.layerTree = &treeB;
  specB.layerStore = &storeB;
  const auto createdB = displayManager.createView( specB );
  REQUIRE( createdB );

  const sicnu::display::DisplayViewId viewA = createdA.value();
  const sicnu::display::DisplayViewId viewB = createdB.value();
  ActiveViewHost host( &canvasA, nullptr, nullptr, &dataManager, &displayManager,
                       viewA, nullptr );
  REQUIRE( host.setActiveViewId( viewA ) );
  CHECK( host.activeMapCanvas() == &canvasA );
  CHECK( host.mapCanvas() == &canvasA ); // legacy accessor stays stable

  REQUIRE( host.setActiveViewId( viewB ) );
  CHECK( host.activeMapCanvas() == &canvasB );
  CHECK( host.mapCanvas() == &canvasA ); // unchanged: why the fix is needed
}
