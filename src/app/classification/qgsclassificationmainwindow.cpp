#include "qgsclassificationmainwindow.h"

#include "rs_class_def.h"
#include "rs_class_quick_list.h"
#include "rs_class_table_widget.h"
#include "rs_jm_matrix_widget.h"
#include "rs_pixel_rasterizer.h"
#include "rs_roi.h"
#include "rs_roi_collection.h"
#include "rs_roi_tool_base.h"
#include "rs_roi_tool_freehand.h"
#include "rs_roi_tool_point.h"
#include "rs_roi_tool_polygon.h"
#include "rs_roi_tool_rectangle.h"
#include "rs_spectral_curve_widget.h"
#include "qgsgeometry.h"
#include "qgsmapcanvas.h"

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QDockWidget>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QPair>
#include <QSet>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>
#include <QWidget>

QgsClassificationMainWindow::QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  setWindowTitle( tr( "Classification · 监督分类" ) );
  resize( 1280, 800 );

  mRois = new RsRoiCollection( this );

  // Seed default 6 classes per UI/design.html ArtboardClassify spec.
  const QList<QPair<int, QPair<QString, QString>>> defaults = {
    { 1, { tr( "林地" ), QStringLiteral( "#2da44e" ) } },
    { 2, { tr( "草地" ), QStringLiteral( "#a3e635" ) } },
    { 3, { tr( "水体" ), QStringLiteral( "#0969da" ) } },
    { 4, { tr( "建成区" ), QStringLiteral( "#cf222e" ) } },
    { 5, { tr( "耕地" ), QStringLiteral( "#d29922" ) } },
    { 6, { tr( "裸地" ), QStringLiteral( "#8a92a0" ) } },
  };
  for ( const auto &d : defaults )
  {
    mRois->setClassDef( RsClassDef( d.first, d.second.first, QColor( d.second.second ) ) );
  }

  mCanvas = new QgsMapCanvas( this );
  mCanvas->setObjectName( QStringLiteral( "rsClassifyCanvas" ) );
  mCanvas->setCanvasColor( Qt::white );
  setCentralWidget( mCanvas );

  setupMenus();
  setupToolbars();
  setupDocks();
  setupRoiTools();
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
  mClassTableWidget = new RsClassTableWidget( mClassListDock );
  mClassTableWidget->setRoiCollection( mRois );
  mClassListDock->setWidget( mClassTableWidget );
  addDockWidget( Qt::RightDockWidgetArea, mClassListDock );
  mClassListDock->resize( 380, mClassListDock->height() );

  mClassQuickListDock = new QDockWidget( tr( "类别快览" ), this );
  mClassQuickListDock->setObjectName( QStringLiteral( "rsClassQuickListDock" ) );
  mClassQuickListWidget = new RsClassQuickList( mClassQuickListDock );
  mClassQuickListWidget->setRoiCollection( mRois );
  mClassQuickListDock->setWidget( mClassQuickListWidget );
  addDockWidget( Qt::LeftDockWidgetArea, mClassQuickListDock );

  mJmDock = new QDockWidget( tr( "JM 分离度" ), this );
  mJmDock->setObjectName( QStringLiteral( "rsClassJmDock" ) );
  mJmMatrix = new RsJmMatrixWidget( mJmDock );
  mJmDock->setWidget( mJmMatrix );
  addDockWidget( Qt::RightDockWidgetArea, mJmDock );

  mSpectralDock = new QDockWidget( tr( "光谱曲线" ), this );
  mSpectralDock->setObjectName( QStringLiteral( "rsClassSpectralDock" ) );
  mSpectralCurve = new RsSpectralCurveWidget( mSpectralDock );
  mSpectralDock->setWidget( mSpectralCurve );
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

void QgsClassificationMainWindow::setupRoiTools()
{
  // Instantiate the 4 manual ROI map tools owned by this window.
  mToolPoint = new RsRoiToolPoint( mCanvas );
  mToolRect = new RsRoiToolRectangle( mCanvas );
  mToolPolygon = new RsRoiToolPolygon( mCanvas );
  mToolFreehand = new RsRoiToolFreehand( mCanvas );

  const QVector<RsRoiToolBase *> tools = {
    mToolPoint, mToolRect, mToolPolygon, mToolFreehand
  };
  for ( RsRoiToolBase *t : tools )
  {
    connect( t, &RsRoiToolBase::roiDrawn,
             this, &QgsClassificationMainWindow::onRoiDrawn );
  }

  // Bind toolbar actions to tools. Each action is checkable and grouped so
  // only one tool is active at a time. Toggling on installs the tool on the
  // canvas; toggling off uninstalls it.
  const QHash<QString, RsRoiToolBase *> actionToTool = {
    { QStringLiteral( "rsToolRoiPoint" ), mToolPoint },
    { QStringLiteral( "rsToolRoiRect" ), mToolRect },
    { QStringLiteral( "rsToolRoiPolygon" ), mToolPolygon },
    { QStringLiteral( "rsToolRoiFreehand" ), mToolFreehand },
  };

  auto *group = new QActionGroup( this );
  group->setExclusive( true );

  for ( auto it = actionToTool.constBegin(); it != actionToTool.constEnd(); ++it )
  {
    QAction *a = findChild<QAction *>( it.key() );
    if ( !a )
      continue;
    a->setCheckable( true );
    group->addAction( a );
    RsRoiToolBase *t = it.value();
    connect( a, &QAction::toggled, this, [this, t]( bool on ) {
      if ( !mCanvas )
        return;
      if ( on )
        mCanvas->setMapTool( t );
      else
        mCanvas->unsetMapTool( t );
    } );
  }

  // Track the currently-selected class so the next ROI gets the right id.
  if ( mClassTableWidget )
  {
    connect( mClassTableWidget, &RsClassTableWidget::currentClassChanged,
             this, &QgsClassificationMainWindow::onCurrentClassChanged );
  }
}

void QgsClassificationMainWindow::onCurrentClassChanged( int classId )
{
  if ( mToolPoint )
    mToolPoint->setCurrentClassId( classId );
  if ( mToolRect )
    mToolRect->setCurrentClassId( classId );
  if ( mToolPolygon )
    mToolPolygon->setCurrentClassId( classId );
  if ( mToolFreehand )
    mToolFreehand->setCurrentClassId( classId );
}

void QgsClassificationMainWindow::onRoiDrawn( const QgsGeometry &geom, int classId )
{
  if ( classId <= 0 )
  {
    statusBar()->showMessage( tr( "请先在类别表中选一个类别" ), 3000 );
    return;
  }
  // Only rasterize when a source raster has been associated. Tasks
  // 10.5/10.7/10.8 will set mSourceWidth/Height/Gt when the user opens a
  // raster; until then we record geometry-only ROIs.
  QSet<quint64> pixels;
  if ( mSourceWidth > 0 && mSourceHeight > 0 )
  {
    pixels = RsPixelRasterizer::rasterize( geom, mSourceGt,
                                           mSourceWidth, mSourceHeight );
  }
  QVector<quint64> idx( pixels.begin(), pixels.end() );
  if ( mRois )
    mRois->appendRoi( RsRoi( classId, geom, idx ) );
}
