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
#include "qgsbezierdata.h"
#include <catch2/catch_approx.hpp>
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
#include <QTemporaryDir>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QMessageBox>
#include <QAbstractButton>
#include <QListWidget>
#include <QUuid>
#include "qgsmanageconnectionsdialog.h"
#include "qgsvectortileconnection.h"
#include "qgstiledsceneconnection.h"
#include "qgsstacconnection.h"
#include "qgssettingsentryimpl.h"

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

TEST_CASE( "connection imports ask before overwriting migrated settings", "[gui][issue-1892]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  struct SettingsScope
  {
    QSettings::Format format = QSettings::defaultFormat();
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    QString iniRoot;
    explicit SettingsScope( const QString &path )
    {
      QDir root = QFileInfo( QSettings( QSettings::IniFormat, QSettings::UserScope,
        QStringLiteral( "scope_probe_org" ), QStringLiteral( "scope_probe_app" ) ).fileName() ).dir();
      root.cdUp();
      iniRoot = root.absolutePath();
      QSettings::setDefaultFormat( QSettings::IniFormat );
      QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, path );
      QCoreApplication::setOrganizationName( QStringLiteral( "exp-rs-tests" ) );
      QCoreApplication::setApplicationName( QStringLiteral( "connection-import" ) );
    }
    ~SettingsScope()
    {
      QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, iniRoot );
      QSettings::setDefaultFormat( format );
      QCoreApplication::setOrganizationName( organization );
      QCoreApplication::setApplicationName( application );
    }
  } scope( dir.path() );

  struct ConnectionKind
  {
    QgsManageConnectionsDialog::Type type;
    QString root;
    QString element;
    const QgsSettingsEntryString *url;
    QgsSettingsTreeNamedListNode *tree;
  };
  const ConnectionKind kinds[] = {
    { QgsManageConnectionsDialog::VectorTile, "qgsVectorTileConnections", "vectortile",
      QgsVectorTileProviderConnection::settingsUrl, QgsVectorTileProviderConnection::sTreeConnectionVectorTile },
    { QgsManageConnectionsDialog::TiledScene, "qgsTiledSceneConnections", "tiledscene",
      QgsTiledSceneProviderConnection::settingsUrl, QgsTiledSceneProviderConnection::sTreeConnectionTiledScene },
    { QgsManageConnectionsDialog::STAC, "qgsStacConnections", "stac",
      QgsStacConnection::settingsUrl, QgsStacConnection::sTreeConnectionStac }
  };
  struct Scenario { bool exists; QMessageBox::StandardButton answer; };
  for ( const auto &kind : kinds )
  {
    for ( const auto scenario : { Scenario{ true, QMessageBox::No }, Scenario{ true, QMessageBox::Yes },
                                 Scenario{ false, QMessageBox::No } } )
    {
      INFO( kind.root.toStdString() << ", existing: " << scenario.exists << ", answer: " << int( scenario.answer ) );
      const QString name = QUuid::createUuid().toString( QUuid::WithoutBraces );
      const QString original = QStringLiteral( "https://original.invalid/endpoint" );
      const QString replacement = QStringLiteral( "https://replacement.invalid/endpoint" );
      if ( scenario.exists )
      {
        REQUIRE( kind.url->setValue( original, name ) );
        REQUIRE( kind.tree->items().contains( name ) );
      }
      const QString path = dir.filePath( QStringLiteral( "connections.xml" ) );
      QFile file( path );
      REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
      const QByteArray xml = QString( "<%1 version=\"1.0\"><%2 name=\"%3\" url=\"%4\" /></%1>" )
          .arg( kind.root, kind.element, name, replacement ).toUtf8();
      REQUIRE( file.write( xml ) == xml.size() );
      file.close();
      bool prompted = false;
      bool unexpectedDialog = false;
      QTimer answer;
      QObject::connect( &answer, &QTimer::timeout, &answer, [&] {
        if ( auto *box = qobject_cast<QMessageBox *>( QApplication::activeModalWidget() ) )
        {
          if ( box->standardButtons().testFlag( QMessageBox::No ) )
          {
            prompted = true;
            if ( auto *button = box->button( scenario.answer ) )
              button->click();
          }
          else
          {
            unexpectedDialog = true;
            box->done( QMessageBox::Cancel );
          }
        }
      } );
      answer.start( 1 );
      QgsManageConnectionsDialog dialog( nullptr, QgsManageConnectionsDialog::Import, kind.type, path );
      auto *list = dialog.findChild<QListWidget *>( QStringLiteral( "listConnections" ) );
      REQUIRE( list != nullptr );
      REQUIRE( list->count() == 1 );
      dialog.selectAll();
      dialog.doExportImport();
      answer.stop();
      CHECK_FALSE( unexpectedDialog );
      CHECK( prompted == scenario.exists );
      const bool keepOriginal = scenario.exists && scenario.answer == QMessageBox::No;
      CHECK( kind.url->value( name ).toStdString() == ( keepOriginal ? original : replacement ).toStdString() );
    }
  }
}

class TestQgsMapToolCapture
{
public:
  static QgsBezierData &bezierData( QgsMapToolCapture &capture )
  {
    capture.mBezierData = std::make_unique<QgsBezierData>();
    return *capture.mBezierData;
  }
};

TEST_CASE( "dragging an existing Bezier anchor translates its handles and stops on release", "[gui][issue-1887]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  canvas.resize( 1000, 500 );
  canvas.setExtent( QgsRectangle( -5, -5, 15, 5 ) );
  QgsAdvancedDigitizingDockWidget cad( &canvas );
  struct CaptureProbe : QgsMapToolCapture {
    using QgsMapToolCapture::QgsMapToolCapture;
    using QgsMapToolCapture::startCapturing;
  } capture( &canvas, &cad, QgsMapToolCapture::CaptureLine );
  capture.setCurrentCaptureTechnique( Qgis::CaptureTechnique::PolyBezier );
  QgsMapMouseEvent press( &canvas, QEvent::MouseButtonPress, QPoint( 200, 200 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
  const QgsPoint start( press.mapPoint() );
  QgsBezierData &data = TestQgsMapToolCapture::bezierData( capture );
  data.addAnchor( start );
  data.calculateSymmetricHandles( 0, QgsPoint( start.x() + 2, start.y() ) );
  const QgsPoint other( start.x() + 10, start.y() );
  data.addAnchor( other );
  const QgsPoint left = data.handle( 0 );
  const QgsPoint right = data.handle( 1 );
  capture.startCapturing();
  capture.cadCanvasPressEvent( &press );
  QgsMapMouseEvent move( &canvas, QEvent::MouseMove, QPoint( 250, 250 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
  const QgsPoint expected( move.mapPoint() );
  REQUIRE( expected != start );
  capture.cadCanvasMoveEvent( &move );
  CHECK( data.anchor( 0 ) == expected );
  const double dx = expected.x() - start.x();
  const double dy = expected.y() - start.y();
  CHECK( data.handle( 0 ).x() == Catch::Approx( left.x() + dx ) );
  CHECK( data.handle( 0 ).y() == Catch::Approx( left.y() + dy ) );
  CHECK( data.handle( 1 ).x() == Catch::Approx( right.x() + dx ) );
  CHECK( data.handle( 1 ).y() == Catch::Approx( right.y() + dy ) );
  CHECK( data.anchor( 1 ) == other );
  QgsMapMouseEvent release( &canvas, QEvent::MouseButtonRelease, QPoint( 250, 250 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
  capture.cadCanvasReleaseEvent( &release );
  QgsMapMouseEvent idle( &canvas, QEvent::MouseMove, QPoint( 300, 300 ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
  capture.cadCanvasMoveEvent( &idle );
  CHECK( data.anchor( 0 ) == expected );
  CHECK( data.anchorCount() == 2 );
}
