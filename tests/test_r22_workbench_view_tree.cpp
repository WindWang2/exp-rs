// test_r22_workbench_view_tree.cpp — R22-05 W3+W4
//
// W3 (#zoom-native-crs): "Zoom to Native Resolution (1:1)" must reproject the
// 1:1 extent from the raster's CRS into the canvas destination CRS before
// applying it. Writing layer-CRS units straight into setExtent() silently
// relocates/rescales the view whenever the two CRS differ (the same
// wrong-spatial-product class #1030 guards elsewhere).
//
// W4 (#group-remove-lease): the layer-tree context menu's group "Remove"
// must route layer removal through ActiveViewHost → QgisDisplayManager so
// Display Layer records and Asset leases are released. The stock QGIS
// default action deletes project layers behind the manager's back, leaving
// ghost records and held leases ("asset is leased / cannot unload").
#include <catch2/catch_test_macros.hpp>

#include <QAbstractProxyModel>
#include <QAction>
#include <QMenu>
#include <QTemporaryDir>

#include <gdal.h>
#include <cpl_conv.h>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreeview.h>
#include <qgslayertreemodel.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>

#include "app/active_view_host.h"
#include "app/layer_tree_menu.h"
#include "app/display/qgis_display_manager.h"
#include "data/data_manager.h"

#include <cmath>
#include <memory>
#include <vector>

namespace
{

void ensureQgisApplication()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r22_workbench_view_tree";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  (void)application;
  QgsApplication::initQgis();
}

// A 16x16 GeoTIFF in EPSG:3857 with 100 m pixels: extent x [0,1600], y [400,2000].
QString syntheticWebMercatorTif( const QString &path )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 16, H = 16;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 100.0, 0.0, 2000.0, 0.0, -100.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection( ds, "EPSG:3857" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return path;
}

sicnu::data::AssetId registerRaster( sicnu::data::DataManager &dataManager, const QString &path )
{
  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  const sicnu::data::RegisterResult registered =
      dataManager.registerSource( sicnu::data::RegisterRequest{ source } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

} // namespace

TEST_CASE( "r22 W3: zoomToNativeResolution reprojects the 1:1 extent into the canvas CRS",
           "[r22][workbench][active_view_host]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  QgsMapCanvas canvas;
  canvas.resize( 800, 600 );
  const QgsCoordinateReferenceSystem canvasCrs = QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) );
  canvas.setDestinationCrs( canvasCrs );

  const QString tif = syntheticWebMercatorTif( dir.filePath( QStringLiteral( "merc.tif" ) ) );
  QgsRasterLayer layer( tif, QStringLiteral( "merc" ) );
  REQUIRE( layer.isValid() );
  REQUIRE( layer.crs().authid() == QStringLiteral( "EPSG:3857" ) );

  ActiveViewHost host( &canvas, nullptr, nullptr, nullptr, nullptr, sicnu::display::DisplayViewId(), nullptr );

  host.zoomToNativeResolution( &layer );

  // Expected: the 800x600-pixel 1:1 window (80000 x 60000 m) around the
  // layer center, transformed 3857 → 4326.
  const double xRes = layer.rasterUnitsPerPixelX();
  const double yRes = layer.rasterUnitsPerPixelY();
  const QgsRectangle ext = layer.extent();
  const double cx = ( ext.xMinimum() + ext.xMaximum() ) / 2.0;
  const double cy = ( ext.yMinimum() + ext.yMaximum() ) / 2.0;
  QgsRectangle expected( cx - canvas.width() * xRes / 2.0, cy - canvas.height() * yRes / 2.0,
                         cx + canvas.width() * xRes / 2.0, cy + canvas.height() * yRes / 2.0 );
  const QgsCoordinateTransform ct( layer.crs(), canvasCrs, QgsProject::instance() );
  expected = ct.transformBoundingBox( expected );

  const QgsRectangle got = canvas.extent();
  // QgsMapCanvas::setExtent() re-buffers the rectangle to the canvas aspect
  // ratio (one axis may widen), so the oracle is: the applied extent is
  // centered on the transformed 1:1 window, contains it, and stays within a
  // tight factor of it. The RED behaviour (raw 3857 meters misread as
  // degrees) is off by five orders of magnitude and fails every clause.
  CHECK( std::abs( got.center().x() - expected.center().x() ) < 1e-6 );
  CHECK( std::abs( got.center().y() - expected.center().y() ) < 1e-6 );
  CHECK( got.xMinimum() <= expected.xMinimum() + 1e-9 );
  CHECK( got.xMaximum() >= expected.xMaximum() - 1e-9 );
  CHECK( got.yMinimum() <= expected.yMinimum() + 1e-9 );
  CHECK( got.yMaximum() >= expected.yMaximum() - 1e-9 );
  CHECK( got.width() < expected.width() * 1.5 );
  CHECK( got.height() < expected.height() * 1.5 );

  SECTION( "same-CRS raster keeps the raw 1:1 window" )
  {
    QgsMapCanvas sameCrsCanvas;
    sameCrsCanvas.resize( 800, 600 );
    sameCrsCanvas.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3857" ) ) );
    ActiveViewHost sameHost( &sameCrsCanvas, nullptr, nullptr, nullptr, nullptr,
                             sicnu::display::DisplayViewId(), nullptr );
    sameHost.zoomToNativeResolution( &layer );
    const QgsRectangle raw = sameCrsCanvas.extent();
    // setExtent may pad by up to a pixel to the canvas aspect; the window
    // itself is 800x600 px at 4:3, matching the widget, so any real drift
    // beyond a couple of pixels is a defect.
    REQUIRE( std::abs( raw.width() - canvas.width() * xRes ) < 2.0 * xRes );
    REQUIRE( std::abs( raw.height() - canvas.height() * yRes ) < 2.0 * yRes );
    REQUIRE( std::abs( raw.center().x() - cx ) < 2.0 * xRes );
    REQUIRE( std::abs( raw.center().y() - cy ) < 2.0 * yRes );
  }
}

TEST_CASE( "r22 W4: layer-tree group Remove releases Display Layer records and Asset leases",
           "[r22][workbench][layer_tree_menu]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  sicnu::data::DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  sicnu::display::QgisDisplayManager displayManager( &dataManager );

  sicnu::display::DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &layerTree;
  spec.layerStore = &layerStore;
  const auto created = displayManager.createView( spec );
  REQUIRE( created );
  const sicnu::display::DisplayViewId viewId = created.value();

  const sicnu::data::AssetId assetId =
      registerRaster( dataManager, syntheticWebMercatorTif( dir.filePath( QStringLiteral( "g.tif" ) ) ) );
  const auto added = displayManager.addLayer( viewId, assetId );
  REQUIRE( added );
  REQUIRE( dataManager.leaseCount( assetId ) == 1 );
  REQUIRE( layerTree.findLayers().size() == 1 );

  // File the layer into a group exactly the way
  // ActiveViewHost::placeInTreeGroup does: insert the new node first, then
  // remove the old one (remove-first crashes QgsLayerTree's custom-order
  // bookkeeping — the production comment cites the same rule).
  QgsLayerTreeGroup *group = layerTree.insertGroup( -1, QStringLiteral( "Raster Layers" ) );
  REQUIRE( group != nullptr );
  QgsLayerTreeLayer *layerNode = layerTree.findLayers().constFirst();
  REQUIRE( layerNode->layer() != nullptr );
  group->addLayer( layerNode->layer() );
  layerTree.removeChildNode( layerNode );
  REQUIRE( group->findLayers().size() == 1 );

  QgsLayerTreeView treeView;
  QgsLayerTreeModel model( &layerTree );
  treeView.setModel( &model );
  // QgsLayerTreeView::setModel wraps the tree model in a proxy; the view's
  // current index lives in proxy coordinates, so map the source index.
  const QModelIndex groupIndex = model.node2index( group );
  REQUIRE( groupIndex.isValid() );
  auto *proxy = qobject_cast<QAbstractProxyModel *>( treeView.model() );
  REQUIRE( proxy != nullptr );
  const QModelIndex proxyIndex = proxy->mapFromSource( groupIndex );
  REQUIRE( proxyIndex.isValid() );
  treeView.setCurrentIndex( proxyIndex );
  REQUIRE( treeView.index2node( treeView.currentIndex() ) == group );

  ActiveViewHost host( &canvas, &treeView, nullptr, &dataManager, &displayManager, viewId, nullptr );
  host.setConfirmationFn( []( const QString & ) { return true; } );

  LayerTreeMenuProvider provider( &treeView, &host, nullptr );
  std::unique_ptr<QMenu> menu( provider.createContextMenu() );
  REQUIRE( menu != nullptr );

  QAction *removeAction = nullptr;
  const auto actions = menu->actions();
  QStringList actionTexts;
  for ( QAction *action : actions )
  {
    actionTexts << action->text();
    if ( action->text().remove( QLatin1Char( '&' ) ).compare( QStringLiteral( "Remove" ), Qt::CaseInsensitive ) == 0 )
    {
      removeAction = action;
      break;
    }
  }
  INFO( "menu actions: " << actionTexts.join( QStringLiteral( ", " ) ).toStdString() );
  REQUIRE( removeAction != nullptr );
  removeAction->trigger();

  // The lease must be released and no ghost Display Layer record may remain.
  CHECK( dataManager.leaseCount( assetId ) == 0 );
  const auto snapshot = displayManager.view( viewId );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->layerIds().isEmpty() );
  // The empty group node is removed with its layers.
  bool groupStillPresent = false;
  for ( QgsLayerTreeGroup *g : layerTree.findGroups() )
  {
    if ( g->name() == QStringLiteral( "Raster Layers" ) )
      groupStillPresent = true;
  }
  CHECK_FALSE( groupStillPresent );

  QgsProject::instance()->removeAllMapLayers();
}
