#include "qgsgeoref_image_to_map_window.h"

#include <QIcon>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QWidget>

#include "core/sicnu_logging.h"
#include "qgsgcptransformer.h"
#include "qgslayertree.h"
#include "qgsmapcanvas.h"
#include "qgsproject.h"

QgsGeorefImageToMapWindow::QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent )
  : QgsGeorefShellWindow( iface, parent )
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QStringLiteral( "Image 2 Map georef window opened" ) );
  setWindowTitle( tr( "Image Registration · Image 2 Map" ) );
  resize( 1100, 900 );

  setupMenus();
  setupToolbars();
  setupStatusBar( QStringLiteral( "rsGeorefI2MCoordLabel" ),
                  QStringLiteral( "rsGeorefI2MCrsLabel" ),
                  QStringLiteral( "rsGeorefI2MRmsLabel" ) );
  setupCentralWidget();
  finishCommonSetup( RsGeorefParamsPanel::Profile::ImageToMap,
                     QStringLiteral( "rsGcpDockI2M" ),
                     QStringLiteral( "rsParamDockI2M" ) );

  connect( QgsProject::instance(), &QgsProject::layersAdded, this,
           [this]( const QList<QgsMapLayer *> & ) { refreshMapLayersFromProject(); } );
  connect( QgsProject::instance(),
           qOverload<const QStringList &>( &QgsProject::layersWillBeRemoved ), this,
           [this]( const QStringList & ) { refreshMapLayersFromProject(); } );
  if ( QgsLayerTree *root = QgsProject::instance()->layerTreeRoot() )
  {
    connect( root, &QgsLayerTreeNode::visibilityChanged, this,
             [this]( QgsLayerTreeNode * ) { refreshMapLayersFromProject(); } );
  }
  refreshMapLayersFromProject();
}

void QgsGeorefImageToMapWindow::setupCentralWidget()
{
  auto *splitter = new QSplitter( Qt::Vertical, this );
  splitter->setObjectName( QStringLiteral( "rsGeorefI2MSplitter" ) );

  mSrcCanvas = new QgsMapCanvas( this );
  mSrcCanvas->setObjectName( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
  mSrcCanvas->setCanvasColor( Qt::white );

  mDstCanvas = new QgsMapCanvas( this );
  mDstCanvas->setObjectName( QStringLiteral( "rsGeorefI2MMapCanvas" ) );
  mDstCanvas->setCanvasColor( QColor( 245, 245, 245 ) );

  splitter->addWidget( mSrcCanvas );
  splitter->addWidget( mDstCanvas );
  splitter->setStretchFactor( 0, 1 );
  splitter->setStretchFactor( 1, 1 );
  setCentralWidget( splitter );
}

void QgsGeorefImageToMapWindow::setupMenus()
{
  QMenu *fileMenu = createFileMenu();
  fileMenu->addAction( tr( "Refresh map layers" ),
                       this, &QgsGeorefImageToMapWindow::refreshMapLayersFromProject );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );
  addStandardMenuBar();
}

void QgsGeorefImageToMapWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefI2MToolBar" ) );
  mToolBar->setMovable( false );

  addGcpEditActions( mToolBar, QStringLiteral( "rsGeorefI2M" ) );
  mToolBar->addSeparator();
  mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Refresh map" ),
                       this, &QgsGeorefImageToMapWindow::refreshMapLayersFromProject );

  addApplyAction( mToolBar, QStringLiteral( "rsGeorefI2MApplyAction" ) );
}

void QgsGeorefImageToMapWindow::refreshMapLayersFromProject()
{
  if ( !mDstCanvas )
    return;

  QList<QgsMapLayer *> layers;
  if ( QgsLayerTree *root = QgsProject::instance()->layerTreeRoot() )
  {
    for ( QgsMapLayer *l : root->checkedLayers() )
    {
      if ( l && l->isValid() )
        layers << l;
    }
  }
  else
  {
    for ( QgsMapLayer *l : QgsProject::instance()->mapLayers().values() )
    {
      if ( l && l->isValid() )
        layers << l;
    }
  }

  mDstCanvas->setLayers( layers );
  if ( mParamsPanel && mParamsPanel->destCrs().isValid() )
    mDstCanvas->setDestinationCrs( mParamsPanel->destCrs() );
  else if ( QgsProject::instance()->crs().isValid() )
    mDstCanvas->setDestinationCrs( QgsProject::instance()->crs() );
  mDstCanvas->refresh();
}

void QgsGeorefImageToMapWindow::onTransformMethodChangedExtra()
{
  if ( !mParamsPanel )
    return;
  mParamsPanel->setRpcMode(
    mParamsPanel->transformMethod() ==
    QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
}

void QgsGeorefImageToMapWindow::captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot &s ) const
{
  s.mode = 0;
  s.syncZoom = false;
}
