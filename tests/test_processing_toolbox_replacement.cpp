#include <catch2/catch_test_macros.hpp>

#include "processing/qgsprocessingtoolboxtreeview.h"
#include "qgsapplication.h"
#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsshortcutsmanager.h"
#include "qgsmarkersymbollayer.h"
#include "qgssymbollayerwidget.h"
#include "qgsunitselectionwidget.h"
#include "qgsmessagebaritem.h"
#include "qgsrendererwidget.h"
#include "qgsmarkersymbol.h"
#include "qgssymbolselectordialog.h"
#include "qgsstyle.h"
#include "qgsplotwidget.h"
#include "qgspiechartplot.h"
#include "qgscolorrampbutton.h"
#include "qgsvectortilesourceselect.h"
#include "qgsfilewidget.h"
#include "qgsproviderutils.h"
#include <QRadioButton>
#include <QComboBox>
#include <QTreeView>
#include <QTableWidget>
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


TEST_CASE( "Widget-only messages replace and clear their custom content", "[gui_safety]" )
{
  ensureApp();
  QPointer<QWidget> first = new QWidget;
  QgsMessageBarItem item( first.data() );
  QPointer<QWidget> second = new QWidget;
  item.setWidget( second );
  CHECK( first.isNull() );
  CHECK( item.widget() == second.data() );
  item.setWidget( second );
  REQUIRE( !second.isNull() );
  item.setWidget( nullptr );
  CHECK( second.isNull() );
  CHECK( item.widget() == nullptr );
}

TEST_CASE( "Data-defined dialogs tolerate selected rules without symbols", "[gui_safety]" )
{
  ensureApp();
  std::unique_ptr<QgsMarkerSymbol> marker( QgsMarkerSymbol::createSimple( {} ) );
  marker->setDataDefinedSize( QgsProperty::fromExpression( "2+3" ) );
  QgsDataDefinedSizeDialog size( { nullptr, marker.get() }, nullptr );
  QgsDataDefinedRotationDialog rotation( { nullptr, marker.get() }, nullptr );
  CHECK( marker->dataDefinedSize().isActive() );
}

TEST_CASE( "Symbol layer slots tolerate an empty selection", "[gui_safety]" )
{
  ensureApp();
  std::unique_ptr<QgsMarkerSymbol> symbol( QgsMarkerSymbol::createSimple( {} ) );
  QgsSymbolSelectorWidget widget( symbol.get(), QgsStyle::defaultStyle(), nullptr );
  auto *tree = widget.findChild<QTreeView *>( "layersTree" );
  REQUIRE( tree );
  tree->setCurrentIndex( QModelIndex() );
  const int count = symbol->symbolLayerCount();
  widget.removeLayer();
  widget.changeLayer( nullptr );
  CHECK( symbol->symbolLayerCount() == count );
}

TEST_CASE( "Pie chart widgets round-trip cleared color ramps", "[gui_safety]" )
{
  ensureApp();
  QgsPieChartPlot plot;
  QgsPieChartPlotWidget widget;
  widget.setPlot( &plot );
  const auto buttons = widget.findChildren<QgsColorRampButton *>();
  REQUIRE( !buttons.isEmpty() );
  for ( auto *button : buttons ) button->setColorRamp( nullptr );
  std::unique_ptr<QgsPlot> output( widget.createPlot() );
  auto *pie = dynamic_cast<QgsPieChartPlot *>( output.get() );
  REQUIRE( pie );
  CHECK( pie->colorRampAt( 0 ) == nullptr );
  widget.setPlot( output.get() );
  std::unique_ptr<QgsPlot> again( widget.createPlot() );
  REQUIRE( again );
}

TEST_CASE( "Local vector tile layers use the file name instead of a connection name", "[gui_safety]" )
{
  ensureApp();
  QgsVectorTileSourceSelect widget;
  auto *radio = widget.findChild<QRadioButton *>( "mRadioSourceFile" );
  auto *file = widget.findChild<QgsFileWidget *>( "mFileWidget" );
  auto *connections = widget.findChild<QComboBox *>( "cmbConnections" );
  REQUIRE( radio );
  REQUIRE( file );
  REQUIRE( connections );
  connections->addItem( "unrelated service" );
  connections->setCurrentText( "unrelated service" );
  radio->setChecked( true );
  const QString path = "/tmp/local-roads.mbtiles";
  file->setFilePath( path );
  QString layerName;
  QObject::connect( &widget, &QgsAbstractDataSourceWidget::addLayer, &widget,
                    [&]( Qgis::LayerType, const QString &, const QString &name, const QString & ) { layerName = name; } );
  widget.addButtonClicked();
  CHECK( layerName == QgsProviderUtils::suggestLayerNameFromFilePath( path ) );
}
