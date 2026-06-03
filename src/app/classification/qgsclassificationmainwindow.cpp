#include "qgsclassificationmainwindow.h"

#include "rs_roi_collection.h"
#include "qgsmapcanvas.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

QgsClassificationMainWindow::QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  setWindowTitle( tr( "Classification · 监督分类" ) );
  resize( 1280, 800 );

  mRois = new RsRoiCollection( this );

  mCanvas = new QgsMapCanvas( this );
  mCanvas->setObjectName( QStringLiteral( "rsClassifyCanvas" ) );
  mCanvas->setCanvasColor( Qt::white );
  setCentralWidget( mCanvas );

  setupMenus();
  setupToolbars();
  setupDocks();
  setupStatusBar();
}

QgsClassificationMainWindow::~QgsClassificationMainWindow() = default;

void QgsClassificationMainWindow::setupMenus()
{
  auto *fileMenu = menuBar()->addMenu( tr( "File" ) );
  fileMenu->addAction( tr( "Open source raster..." ), this, []() {} );
  fileMenu->addAction( tr( "Load ROIs..." ), this, []() {} );
  fileMenu->addAction( tr( "Save ROIs..." ), this, []() {} );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );

  menuBar()->addMenu( tr( "Edit" ) );
  menuBar()->addMenu( tr( "View" ) );
  menuBar()->addMenu( tr( "Processing" ) );
  menuBar()->addMenu( tr( "Help" ) );
}

void QgsClassificationMainWindow::setupToolbars()
{
  auto *roiBar = addToolBar( tr( "ROI" ) );
  roiBar->setObjectName( QStringLiteral( "rsClassifyRoiBar" ) );
  roiBar->setMovable( false );

  roiBar->addAction( tr( "Select" ) );
  roiBar->addSeparator();

  auto *toolPoint = roiBar->addAction( tr( "Point" ) );
  toolPoint->setObjectName( QStringLiteral( "rsToolRoiPoint" ) );
  auto *toolRect = roiBar->addAction( tr( "Rectangle" ) );
  toolRect->setObjectName( QStringLiteral( "rsToolRoiRect" ) );
  auto *toolPoly = roiBar->addAction( tr( "Polygon" ) );
  toolPoly->setObjectName( QStringLiteral( "rsToolRoiPolygon" ) );
  auto *toolFree = roiBar->addAction( tr( "Freehand" ) );
  toolFree->setObjectName( QStringLiteral( "rsToolRoiFreehand" ) );
  auto *toolMagic = roiBar->addAction( tr( "Magic wand" ) );
  toolMagic->setObjectName( QStringLiteral( "rsToolRoiMagicWand" ) );

  roiBar->addSeparator();
  roiBar->addAction( tr( "Spectra" ) );
  roiBar->addAction( tr( "Separability" ) );
  roiBar->addAction( tr( "Export ROIs" ) );

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  roiBar->addWidget( spacer );

  roiBar->addAction( tr( "Quick preview" ) );
  auto *apply = roiBar->addAction( tr( "Apply classification..." ) );
  apply->setObjectName( QStringLiteral( "rsClassifyApplyAction" ) );
}

void QgsClassificationMainWindow::setupDocks()
{
  mClassListDock = new QDockWidget( tr( "类别管理" ), this );
  mClassListDock->setObjectName( QStringLiteral( "rsClassListDock" ) );
  mClassListDock->setWidget( new QLabel( tr( "[Class table — Task 10.3]" ), this ) );
  addDockWidget( Qt::RightDockWidgetArea, mClassListDock );
  mClassListDock->resize( 380, mClassListDock->height() );

  mClassQuickListDock = new QDockWidget( tr( "类别快览" ), this );
  mClassQuickListDock->setObjectName( QStringLiteral( "rsClassQuickListDock" ) );
  mClassQuickListDock->setWidget( new QLabel( tr( "[Quick list — Task 10.3]" ), this ) );
  addDockWidget( Qt::LeftDockWidgetArea, mClassQuickListDock );

  mJmDock = new QDockWidget( tr( "JM 分离度" ), this );
  mJmDock->setObjectName( QStringLiteral( "rsClassJmDock" ) );
  mJmDock->setWidget( new QLabel( tr( "[JM matrix — Task 10.6]" ), this ) );
  addDockWidget( Qt::RightDockWidgetArea, mJmDock );

  mSpectralDock = new QDockWidget( tr( "光谱曲线" ), this );
  mSpectralDock->setObjectName( QStringLiteral( "rsClassSpectralDock" ) );
  mSpectralDock->setWidget( new QLabel( tr( "[Spectral curve — Task 10.5]" ), this ) );
  addDockWidget( Qt::BottomDockWidgetArea, mSpectralDock );
}

void QgsClassificationMainWindow::setupStatusBar()
{
  auto *crsLabel = new QLabel( tr( "CRS: —" ), this );
  crsLabel->setObjectName( QStringLiteral( "rsClassifyCrsLabel" ) );
  auto *roiCountLabel = new QLabel( tr( "总 ROI: 0, 像元: 0" ), this );
  roiCountLabel->setObjectName( QStringLiteral( "rsClassifyRoiCountLabel" ) );
  statusBar()->addPermanentWidget( crsLabel );
  statusBar()->addPermanentWidget( roiCountLabel );
}
