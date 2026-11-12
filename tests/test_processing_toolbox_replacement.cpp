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
#include "qgsdetaileditemdelegate.h"
#include "qgsdetaileditemdata.h"
#include "qgsmaptoolcapture.h"
#include "qgsadvanceddigitizingdockwidget.h"
#include "qgsdualview.h"
#include "qgsattributetableview.h"
#include "qgsvectorlayer.h"
#include "qgsvectordataprovider.h"
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QClipboard>
#include "qgsrendererwidget.h"
#include "qgsmarkersymbol.h"
#include "qgssymbolselectordialog.h"
#include "qgsstyle.h"
#include "qgsplotwidget.h"
#include "qgspiechartplot.h"
#include "qgscolorrampbutton.h"
#include "qgsgui.h"
#include "qgssourceselectproviderregistry.h"
#include "qgsabstractdatasourcewidget.h"
#include "qgsfilewidget.h"
#include "qgsproviderutils.h"
#include <QRadioButton>
#include <QComboBox>
#include <QTreeView>
#include <QTableWidget>
#include <memory>
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
  std::unique_ptr<QgsAbstractDataSourceWidget> widget(
    QgsGui::sourceSelectProviderRegistry()->createSelectionWidget(
      "vectortile", nullptr, Qt::WindowFlags(), QgsProviderRegistry::WidgetMode::Standalone ) );
  REQUIRE( widget );
  auto *radio = widget->findChild<QRadioButton *>( "mRadioSourceFile" );
  auto *file = widget->findChild<QgsFileWidget *>( "mFileWidget" );
  auto *connections = widget->findChild<QComboBox *>( "cmbConnections" );
  REQUIRE( radio );
  REQUIRE( file );
  REQUIRE( connections );
  connections->addItem( "unrelated service" );
  connections->setCurrentText( "unrelated service" );
  radio->setChecked( true );
  const QString path = "/tmp/local-roads.mbtiles";
  file->setFilePath( path );
  QString layerName;
  QObject::connect( widget.get(), &QgsAbstractDataSourceWidget::addLayer, widget.get(),
                    [&]( Qgis::LayerType, const QString &, const QString &name, const QString & ) { layerName = name; } );
  widget->addButtonClicked();
  CHECK( layerName == QgsProviderUtils::suggestLayerNameFromFilePath( path ) );
}


TEST_CASE( "Long unbroken detail text can be laid out", "[functional_gui]" )
{
  ensureApp();
  QgsDetailedItemData detail;
  detail.setTitle( "Long URL" );
  detail.setDetail( "https://example.invalid/" + QString( 2048, 'a' ) );
  QStandardItemModel model( 1, 1 );
  model.setData( model.index( 0, 0 ), QVariant::fromValue( detail ), Qt::UserRole );
  QStyleOptionViewItem option;
  option.rect = QRect( 0, 0, 200, 50 );
  option.font = QApplication::font();
  QgsDetailedItemDelegate delegate;
  const QSize size = delegate.sizeHint( option, model.index( 0, 0 ) );
  CHECK( size.height() > 100 );
  CHECK( size.height() < 100000 );
}

TEST_CASE( "Capture snapping records follow point count rather than map length", "[functional_gui]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  QgsAdvancedDigitizingDockWidget cad( &canvas );
  struct CaptureProbe : QgsMapToolCapture {
    using QgsMapToolCapture::QgsMapToolCapture;
    using QgsMapToolCapture::setPoints;
  } capture( &canvas, &cad, QgsMapToolCapture::CaptureLine );
  const QgsPointSequence farPoints = { QgsPoint( 0, 0 ), QgsPoint( 1000, 0 ), QgsPoint( 1000, 2000 ) };
  capture.setPoints( farPoints );
  CHECK( capture.snappingMatches().size() == farPoints.size() );
  const QVector<QgsPointXY> nearPoints = { QgsPointXY( 0, 0 ), QgsPointXY( 0.1, 0 ), QgsPointXY( 0.1, 0.2 ) };
  capture.setPoints( nearPoints );
  CHECK( capture.snappingMatches().size() == nearPoints.size() );
}

TEST_CASE( "Copy cell content respects the displayed sorted proxy", "[functional_gui]" )
{
  ensureApp();
  QgsVectorLayer layer( "Point?crs=EPSG:4326&field=name:string", "copy test", "memory" );
  REQUIRE( layer.isValid() );
  QgsFeatureList features;
  for ( const char *name : { "alpha", "zeta" } )
  {
    QgsFeature feature( layer.fields() );
    feature.initAttributes( 1 );
    feature.setAttribute( 0, QString( name ) );
    feature.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 0, 0 ) ) );
    features << feature;
  }
  REQUIRE( layer.dataProvider()->addFeatures( features ) );
  QgsMapCanvas canvas;
  QgsDualView view;
  view.init( &layer, &canvas, QgsFeatureRequest(), QgsAttributeEditorContext(), true, false );
  auto *table = view.tableView();
  auto *model = table->model();
  REQUIRE( model->rowCount() == 2 );
  int column = -1;
  for ( int c = 0; c < model->columnCount(); ++c )
    if ( model->headerData( c, Qt::Horizontal ).toString() == "name" ) column = c;
  REQUIRE( column >= 0 );
  table->sortByColumn( column, Qt::DescendingOrder );
  const QModelIndex displayed = model->index( 0, column );
  REQUIRE( displayed.data().toString() == "zeta" );
  table->setCurrentIndex( displayed );
  view.copyCellContent();
  CHECK( QApplication::clipboard()->text() == "zeta" );
}
