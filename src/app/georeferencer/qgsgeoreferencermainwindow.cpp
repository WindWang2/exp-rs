#include "qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  setWindowTitle( tr( "Georeferencer · 几何校正" ) );
  resize( 1200, 800 );

  setupMenus();
  setupToolbars();
  setupStatusBar();

  auto *placeholder = new QLabel( tr( "[Canvas area placeholder — Task 11.4.5]" ), this );
  placeholder->setAlignment( Qt::AlignCenter );
  placeholder->setObjectName( QStringLiteral( "rsGeorefCanvasPlaceholder" ) );
  setCentralWidget( placeholder );
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

  // GCP ops (Task 11.4.6 wires real handlers)
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Add GCP" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Delete GCP" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Load .gcp" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Export .gcp" ), this, []() {} );

  mModeBar->addSeparator();
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync zoom" ), this, []() {} );
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

void QgsGeoreferencerMainWindow::closeEvent( QCloseEvent *e )
{
  // TODO Task 11.4.7: persist QgsSettings, ask about unsaved GCPs.
  e->accept();
}
