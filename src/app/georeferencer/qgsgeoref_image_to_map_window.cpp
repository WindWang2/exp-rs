#include "qgsgeoref_image_to_map_window.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QSizePolicy>
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
  auto *fileMenu = menuBar()->addMenu( tr( "&File" ) );
  fileMenu->addAction( tr( "Open source raster..." ),
                       this, &QgsGeorefShellWindow::openSourceRaster );
  fileMenu->addAction( tr( "Refresh map layers" ),
                       this, &QgsGeorefImageToMapWindow::refreshMapLayersFromProject );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );

  menuBar()->addMenu( tr( "&Edit" ) );
  menuBar()->addMenu( tr( "&View" ) );
  menuBar()->addMenu( tr( "&Settings" ) );
  menuBar()->addMenu( tr( "&Help" ) );
}

void QgsGeorefImageToMapWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefI2MToolBar" ) );
  mToolBar->setMovable( false );

  mAddPointAction = mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Add GCP" ) );
  mAddPointAction->setObjectName( QStringLiteral( "rsGeorefI2MAddPointAction" ) );
  mAddPointAction->setCheckable( true );

  mMovePointAction = mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Move GCP" ) );
  mMovePointAction->setObjectName( QStringLiteral( "rsGeorefI2MMovePointAction" ) );
  mMovePointAction->setCheckable( true );

  mDeletePointAction = mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Delete GCP" ) );
  mDeletePointAction->setObjectName( QStringLiteral( "rsGeorefI2MDeletePointAction" ) );
  mDeletePointAction->setCheckable( true );

  auto *mapToolGroup = new QActionGroup( this );
  mapToolGroup->setExclusive( true );
  mapToolGroup->addAction( mAddPointAction );
  mapToolGroup->addAction( mMovePointAction );
  mapToolGroup->addAction( mDeletePointAction );

  mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Load .gcp" ),
                       this, &QgsGeorefShellWindow::loadPoints );
  mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Export .gcp" ),
                       this, &QgsGeorefShellWindow::savePoints );
  mToolBar->addSeparator();
  mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Refresh map" ),
                       this, &QgsGeorefImageToMapWindow::refreshMapLayersFromProject );

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  mToolBar->addWidget( spacer );

  mApplyAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Apply" ), this, &QgsGeorefShellWindow::applyTransform );
  mApplyAction->setObjectName( QStringLiteral( "rsGeorefI2MApplyAction" ) );
  mApplyAction->setEnabled( false );
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
  s.mode = 0; // ImageToMap legacy index
  s.syncZoom = false;
}
