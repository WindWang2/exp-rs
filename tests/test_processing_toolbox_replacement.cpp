#include <catch2/catch_test_macros.hpp>

#include "processing/qgsprocessingtoolboxtreeview.h"
#include "qgsapplication.h"
#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsshortcutsmanager.h"
#include "qgsmarkersymbollayer.h"
#include "qgssymbollayerwidget.h"
#include "qgsunitselectionwidget.h"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QPointer>

namespace {
void ensureApp()
{
  static int argc = 1;
  static char name[] = "test_processing_toolbox_replacement";
  static char *argv[] = { name, nullptr };
  static auto *app = new QgsApplication( argc, argv, false );
  (void)app;
}
}

// The view already grants this fixture access to its paired model pointers.
class TestQgsProcessingModel
{
public:
  static QgsProcessingToolboxModel *toolboxModel( const QgsProcessingToolboxTreeView &view )
  {
    return view.mToolboxModel;
  }
};

TEST_CASE( "Toolbox replacement retains the new source after deferred deletion",
           "[processing][toolbox][regression]" )
{
  ensureApp();

  QgsProcessingToolboxTreeView view;
  QPointer<QAbstractItemModel> oldProxy = view.model();
  auto *replacement = new QgsProcessingToolboxProxyModel( &view );
  QPointer<QgsProcessingToolboxModel> source = replacement->toolboxModel();
  view.setToolboxProxyModel( replacement );
  REQUIRE( view.model() == replacement );
  REQUIRE( TestQgsProcessingModel::toolboxModel( view ) == source.data() );
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  CHECK( oldProxy.isNull() );
  REQUIRE( !source.isNull() );
  CHECK( TestQgsProcessingModel::toolboxModel( view ) == source.data() );
  // Reassigning the active model must not schedule it for destruction.
  view.setToolboxProxyModel( replacement );
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  REQUIRE( !source.isNull() );
  view.setFilterString( QStringLiteral( "missing algorithm" ) );
  CHECK( view.selectedAlgorithm() == nullptr );
}


TEST_CASE( "Common shortcut actions keep stable lookup names", "[processing][gui][p1]" )
{
  ensureApp();
  QgsShortcutsManager manager;
  CHECK( manager.objectName().isEmpty() );
  for ( const char *name : { "mEditorToggleComment", "mEditorReformatCode", "mEditorRunScript", "mEditorRunSelection" } )
  {
    QAction *action = manager.actionByName( name );
    REQUIRE( action );
    CHECK( action->objectName() == name );
  }
}

TEST_CASE( "Mouse event original position survives constrained coordinates", "[processing][gui][p1]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  QgsMapMouseEvent event( &canvas, QEvent::MouseMove, QPoint( 25, 30 ) );
  const QgsPointXY original = event.originalMapPoint();
  event.setMapPoint( QgsPointXY( 123, 456 ) );
  CHECK( event.mapPoint() == QgsPointXY( 123, 456 ) );
  CHECK( event.originalMapPoint() == original );
}

TEST_CASE( "Font marker stroke units are independent of font size units", "[processing][gui][p1]" )
{
  ensureApp();
  QgsFontMarkerSymbolLayer layer;
  QgsFontMarkerSymbolLayerWidget widget( nullptr, nullptr );
  widget.setSymbolLayer( &layer );
  auto *size = widget.findChild<QgsUnitSelectionWidget *>( "mSizeUnitWidget" );
  auto *stroke = widget.findChild<QgsUnitSelectionWidget *>( "mStrokeWidthUnitWidget" );
  REQUIRE( size );
  REQUIRE( stroke );
  size->setUnit( Qgis::RenderUnit::Millimeters );
  stroke->setUnit( Qgis::RenderUnit::MapUnits );
  QgsMapUnitScale scale( 1000, 5000 );
  stroke->setMapUnitScale( scale );
  REQUIRE( QMetaObject::invokeMethod( &widget, "mStrokeWidthUnitWidget_changed", Qt::DirectConnection ) );
  CHECK( layer.strokeWidthUnit() == Qgis::RenderUnit::MapUnits );
  CHECK( layer.strokeWidthMapUnitScale() == scale );
  CHECK( layer.sizeUnit() == Qgis::RenderUnit::Millimeters );
}
