#include "qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsgeoreftooladdpoint.h"
#include "qgsmapcanvas.h"
#include "qgsmapcoordsdialog.h"
#include "rs_twincanvas_sync_controller.h"

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  setWindowTitle( tr( "Georeferencer · 几何校正" ) );
  resize( 1200, 800 );

  setupMenus();
  setupToolbars();
  setupStatusBar();
  setupCentralWidget();
}

void QgsGeoreferencerMainWindow::setupCentralWidget()
{
  auto *split = new QSplitter( Qt::Horizontal, this );
  split->setObjectName( QStringLiteral( "rsGeorefSplitter" ) );

  mSrcCanvas = new QgsMapCanvas( this );
  mSrcCanvas->setObjectName( QStringLiteral( "rsSrcCanvas" ) );
  mSrcCanvas->setCanvasColor( Qt::white );

  mRefCanvas = new QgsMapCanvas( this );
  mRefCanvas->setObjectName( QStringLiteral( "rsRefCanvas" ) );
  mRefCanvas->setCanvasColor( Qt::white );

  split->addWidget( mSrcCanvas );
  split->addWidget( mRefCanvas );
  split->setStretchFactor( 0, 1 );
  split->setStretchFactor( 1, 1 );
  setCentralWidget( split );

  mSyncCtl = new RsTwinCanvasSyncController( mSrcCanvas, mRefCanvas, this );

  // Add-point map tool — clicking the SRC canvas pops the MapCoords dialog.
  mAddPointTool = new QgsGeorefToolAddPoint( mSrcCanvas );
  mAddPointTool->setParent( this );
  connect( mAddPointTool, &QgsGeorefToolAddPoint::showCoordDialog,
           this, &QgsGeoreferencerMainWindow::showCoordDialog );

  // Wire the toolbar's Add GCP action — toggle installs/uninstalls the tool.
  if ( mAddPointAction )
  {
    mAddPointAction->setCheckable( true );
    connect( mAddPointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !mSrcCanvas )
        return;
      if ( on )
        mSrcCanvas->setMapTool( mAddPointTool );
      else if ( mSrcCanvas->mapTool() == mAddPointTool )
        mSrcCanvas->unsetMapTool( mAddPointTool );
    } );
  }

  // Wire the Sync zoom action — toggle enables/disables the controller.
  if ( mSyncZoomAction )
  {
    mSyncZoomAction->setCheckable( true );
    mSyncZoomAction->setChecked( true );
    connect( mSyncZoomAction, &QAction::toggled, this, [this]( bool on ) {
      if ( mSyncCtl )
        mSyncCtl->setEnabled( on );
    } );
  }
}

void QgsGeoreferencerMainWindow::setupMenus()
{
  auto *fileMenu = menuBar()->addMenu( tr( "&File" ) );
  fileMenu->addAction( tr( "Open Raster..." ), this, []() {} );
  fileMenu->addAction( tr( "Load .points..." ), this, []() {} );
  fileMenu->addAction( tr( "Save .points..." ), this, []() {} );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );

  menuBar()->addMenu( tr( "&Edit" ) );
  menuBar()->addMenu( tr( "&View" ) );
  menuBar()->addMenu( tr( "&Settings" ) );
  menuBar()->addMenu( tr( "&Help" ) );
}

void QgsGeoreferencerMainWindow::setupToolbars()
{
  mModeBar = addToolBar( tr( "Mode" ) );
  mModeBar->setObjectName( QStringLiteral( "rsGeorefToolBar" ) );
  mModeBar->setMovable( false );

  mModeToggle = new RsGeorefModeToggle( this );
  mModeBar->addWidget( mModeToggle );
  mModeBar->addSeparator();

  // GCP ops — Add GCP gets wired to the map tool in setupCentralWidget().
  mAddPointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Add GCP" ) );
  mAddPointAction->setObjectName( QStringLiteral( "rsGeorefAddPointAction" ) );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Delete GCP" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Load .gcp" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Export .gcp" ), this, []() {} );

  mModeBar->addSeparator();
  mSyncZoomAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync zoom" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Zoom to all" ), this, []() {} );

  auto *sift = mModeBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Auto match (SIFT)" ),
    this,
    [this]() {
      statusBar()->showMessage( tr( "SIFT auto-match coming in Phase 11.5" ), 3000 );
    } );
  sift->setObjectName( QStringLiteral( "rsGeorefSiftAction" ) );

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  mModeBar->addWidget( spacer );

  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Preview" ), this, []() {} );

  auto *apply = mModeBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Apply" ),
    this,
    []() {} );
  apply->setObjectName( QStringLiteral( "rsGeorefApplyAction" ) );
}

void QgsGeoreferencerMainWindow::setupStatusBar()
{
  mCoordLabel = new QLabel( tr( "—" ), this );
  mCoordLabel->setObjectName( QStringLiteral( "rsGeorefCoordLabel" ) );

  mCrsLabel = new QLabel( tr( "CRS: —" ), this );
  mCrsLabel->setObjectName( QStringLiteral( "rsGeorefCrsLabel" ) );

  mRmsLabel = new QLabel( tr( "RMS: —" ), this );
  mRmsLabel->setObjectName( QStringLiteral( "rsGeorefRmsLabel" ) );

  statusBar()->addWidget( mCoordLabel, 1 );
  statusBar()->addPermanentWidget( mCrsLabel );
  statusBar()->addPermanentWidget( mRmsLabel );
}

void QgsGeoreferencerMainWindow::showCoordDialog( const QgsPointXY &sourcePixel )
{
  // The dialog needs a *temporary* QgsGcpPoint to preview against. The GCP
  // list itself is wired in Task 11.4.6; for now we manage a stack-local one.
  QgsGcpPoint tempGcp( sourcePixel, QgsPointXY(), QgsCoordinateReferenceSystem(), true );
  QgsGeorefDataPoint tempDataPoint( mSrcCanvas, mRefCanvas, &tempGcp );

  QgsCoordinateReferenceSystem rasterCrs = mSrcCanvas
                                             ? mSrcCanvas->mapSettings().destinationCrs()
                                             : QgsCoordinateReferenceSystem();

  auto *dlg = new QgsMapCoordsDialog( mRefCanvas, &tempDataPoint, rasterCrs, this );
  dlg->setAttribute( Qt::WA_DeleteOnClose );
  dlg->show();
}

void QgsGeoreferencerMainWindow::closeEvent( QCloseEvent *e )
{
  // TODO Task 11.4.7: persist QgsSettings, ask about unsaved GCPs.
  e->accept();
}
