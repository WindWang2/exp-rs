#include "qgsclassificationmainwindow.h"

#include "rs_class_def.h"
#include "rs_class_quick_list.h"
#include "rs_class_table_widget.h"
#include "rs_classification_task.h"
#include "rs_classifier_kmeans.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_setup_bar.h"
#include "rs_classifier_svm.h"
#include "rs_jm_matrix_widget.h"
#include "rs_pixel_rasterizer.h"
#include "rs_roi.h"
#include "rs_roi_collection.h"
#include "rs_roi_tool_base.h"
#include "rs_roi_tool_freehand.h"
#include "rs_roi_tool_magicwand.h"
#include "rs_roi_tool_point.h"
#include "rs_roi_tool_polygon.h"
#include "rs_roi_tool_rectangle.h"
#include "rs_spectral_curve_widget.h"
#include "qgsapplication.h"
#include "qgsgeometry.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayerstore.h"
#include "qgsmessagelog.h"
#include "qgsrasterlayer.h"
#include "qgstaskmanager.h"

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
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

#include <gdal_priv.h>

#include <memory>

QgsClassificationMainWindow::QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  setWindowTitle( tr( "Classification · 监督分类" ) );
  resize( 1280, 800 );

  mRois = new RsRoiCollection( this );
  mLayerStore = new QgsMapLayerStore( this );

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
  setupClassifierBar();
  setupStatusBar();
}

QgsClassificationMainWindow::~QgsClassificationMainWindow() = default;

void QgsClassificationMainWindow::setupMenus()
{
  auto *fileMenu = menuBar()->addMenu( tr( "File" ) );
  mOpenRasterAction = fileMenu->addAction(
    tr( "Open source raster..." ), this,
    static_cast<bool ( QgsClassificationMainWindow::* )()>(
      &QgsClassificationMainWindow::openSourceRaster ) );
  mOpenRasterAction->setObjectName( QStringLiteral( "rsClassifyOpenRasterAction" ) );
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
  // Instantiate the 4 manual ROI map tools owned by this window. The
  // magic-wand tool (Task 10.7) joins the same exclusive group so only one
  // ROI tool is active at a time.
  mToolPoint = new RsRoiToolPoint( mCanvas );
  mToolRect = new RsRoiToolRectangle( mCanvas );
  mToolPolygon = new RsRoiToolPolygon( mCanvas );
  mToolFreehand = new RsRoiToolFreehand( mCanvas );
  mToolMagicWand = new RsRoiToolMagicWand( mCanvas );
  // Future-proofing: once Task 10.8 wires raster loading, mSourceRasterPath
  // will be non-empty and the magic-wand can read pixels; until then the
  // tool's canvasReleaseEvent no-ops on an empty path.
  mToolMagicWand->setSourceData( mSourceRasterPath );

  const QVector<RsRoiToolBase *> tools = {
    mToolPoint, mToolRect, mToolPolygon, mToolFreehand, mToolMagicWand
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
    { QStringLiteral( "rsToolRoiMagicWand" ), mToolMagicWand },
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
  if ( mToolMagicWand )
    mToolMagicWand->setCurrentClassId( classId );
}

void QgsClassificationMainWindow::setupClassifierBar()
{
  auto *bar = addToolBar( tr( "Classifier" ) );
  bar->setObjectName( QStringLiteral( "rsClassifierBar" ) );
  bar->setMovable( false );

  mClassifierBar = new RsClassifierSetupBar( bar );
  mClassifierBar->setObjectName( QStringLiteral( "rsClassifierSetupBar" ) );
  bar->addWidget( mClassifierBar );
  addToolBar( Qt::BottomToolBarArea, bar );

  connect( mClassifierBar, &RsClassifierSetupBar::applyRequested,
           this, &QgsClassificationMainWindow::applyClassification );

  // The toolbar Apply action (from Task 10.2) routes to the same slot.
  if ( auto *apply = findChild<QAction *>(
         QStringLiteral( "rsClassifyApplyAction" ) ) )
  {
    mApplyAction = apply;
    connect( apply, &QAction::triggered,
             this, &QgsClassificationMainWindow::applyClassification );
  }
}

bool QgsClassificationMainWindow::openSourceRaster()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Open source raster" ), QString(),
    tr( "Raster (*.tif *.tiff *.img *.jp2);;All files (*)" ) );
  if ( path.isEmpty() )
    return false;
  return openSourceRaster( path );
}

bool QgsClassificationMainWindow::openSourceRaster( const QString &path )
{
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "Failed to open raster: %1" ).arg( path ),
                                5000 );
    return false;
  }

  mSourceRasterPath = path;
  mSourceWidth = ds->GetRasterXSize();
  mSourceHeight = ds->GetRasterYSize();
  mSourceBandCount = ds->GetRasterCount();
  ds->GetGeoTransform( mSourceGt );
  GDALClose( ds );

  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(),
                                    QStringLiteral( "gdal" ) );
  if ( layer->isValid() )
  {
    if ( mLayerStore )
      mLayerStore->addMapLayer( layer );
    mSourceLayer = layer;
    if ( mCanvas )
    {
      mCanvas->setLayers( { layer } );
      mCanvas->setExtent( layer->extent() );
      mCanvas->refresh();
    }
  }
  else
  {
    delete layer;
  }

  if ( mToolMagicWand )
    mToolMagicWand->setSourceData( mSourceRasterPath );
  if ( mClassifierBar )
    mClassifierBar->setSourceBands( mSourceBandCount );

  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已加载源影像: %1 (%2×%3, %4 bands)" )
        .arg( QFileInfo( path ).fileName() )
        .arg( mSourceWidth )
        .arg( mSourceHeight )
        .arg( mSourceBandCount ),
      4000 );
  return true;
}

bool QgsClassificationMainWindow::buildTrainingData( const QVector<int> &bands,
                                                     cv::Mat &X,
                                                     cv::Mat &y ) const
{
  if ( !mRois || bands.isEmpty() || mSourceRasterPath.isEmpty() )
    return false;
  if ( mSourceWidth <= 0 || mSourceHeight <= 0 )
    return false;

  // Open the raster and pre-read each selected band into memory. For tiny
  // training sets (typical ROI coverage is < 0.1% of the raster) this is
  // cheaper than per-pixel RasterIO calls. Future optimisation: bounding-box
  // tile reads.
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( mSourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return false;
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( ds );
      return false;
    }
  }

  std::vector<std::vector<float>> bandBufs( bands.size(),
                                            std::vector<float>( static_cast<size_t>( W ) * H ) );
  for ( int bi = 0; bi < bands.size(); ++bi )
  {
    const CPLErr err = ds->GetRasterBand( bands[bi] )->RasterIO(
      GF_Read, 0, 0, W, H, bandBufs[bi].data(),
      W, H, GDT_Float32, 0, 0 );
    if ( err != CE_None )
    {
      GDALClose( ds );
      return false;
    }
  }
  GDALClose( ds );

  // Collect (classId, pixelIdx) pairs, recomputing rasterisation if the ROI
  // was loaded geometry-only.
  QVector<QPair<int, quint64>> samples;
  for ( const RsRoi &roi : mRois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), mSourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        samples.push_back( qMakePair( roi.classId(), i ) );
    }
  }

  if ( samples.size() < 10 )
    return false;

  X.create( samples.size(), bands.size(), CV_32F );
  y.create( samples.size(), 1, CV_32S );
  for ( int s = 0; s < samples.size(); ++s )
  {
    const quint64 idx = samples[s].second;
    y.at<int>( s, 0 ) = samples[s].first;
    for ( int bi = 0; bi < bands.size(); ++bi )
      X.at<float>( s, bi ) = bandBufs[bi][idx];
  }
  return true;
}

void QgsClassificationMainWindow::applyClassification()
{
  if ( mSourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "请先 File → Open source raster..." ), 5000 );
    return;
  }
  if ( !mClassifierBar )
    return;

  QVector<int> bands = mClassifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    // Default to the first min(3, sourceBands) bands.
    const int n = std::min( 3, mSourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
  {
    statusBar()->showMessage( tr( "无可用波段" ), 5000 );
    return;
  }

  QString outPath = mClassifierBar->outputPath();
  if ( outPath.isEmpty() )
  {
    outPath = QFileDialog::getSaveFileName(
      this, tr( "Output classified raster" ), QString(),
      tr( "GeoTIFF (*.tif)" ) );
    if ( outPath.isEmpty() )
      return;
    mClassifierBar->setOutputPath( outPath );
  }

  cv::Mat X, y;
  if ( !buildTrainingData( bands, X, y ) || X.rows < 10 )
  {
    statusBar()->showMessage(
      tr( "训练样本不足（< 10 像元）— 请先勾画 ROI 或加载已保存样本" ), 6000 );
    return;
  }

  RsClassificationTask::Config cfg;
  cfg.sourceRaster = mSourceRasterPath;
  cfg.outputRaster = outPath;
  cfg.bandIndices = bands;
  cfg.trainX = X;
  cfg.trainY = y;

  const QHash<int, RsClassDef> classDefs = mRois ? mRois->classDefs()
                                                 : QHash<int, RsClassDef>();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
    cfg.classColors[it.key()] = it.value().color();

  switch ( mClassifierBar->currentKind() )
  {
    case RsClassifierKind::NormalBayes:
      cfg.backend.reset( new RsClassifierNormalBayes );
      cfg.algoName = QStringLiteral( "NormalBayes" );
      break;
    case RsClassifierKind::SvmRbf:
      cfg.backend.reset( new RsClassifierSvm );
      cfg.algoName = QStringLiteral( "SVM_RBF" );
      break;
    case RsClassifierKind::KMeans:
      cfg.backend.reset( new RsClassifierKMeans(
        std::max( 2, static_cast<int>( classDefs.size() ) ) ) );
      cfg.algoName = QStringLiteral( "KMeans" );
      break;
  }

  const QString algoForLog = cfg.algoName;
  const QString outForLog = outPath;
  auto *task = new RsClassificationTask( std::move( cfg ) );

  connect( task, &QgsTask::taskCompleted, this, [this, task, algoForLog, outForLog]() {
    const auto &r = task->result();
    if ( r.ok )
    {
      const QJsonObject obj{
        { QStringLiteral( "event" ), QStringLiteral( "classify_finished" ) },
        { QStringLiteral( "algo" ), algoForLog },
        { QStringLiteral( "total_pixels" ), r.totalPixels },
        { QStringLiteral( "duration_ms" ), r.durationMs },
        { QStringLiteral( "status" ), QStringLiteral( "ok" ) }
      };
      QgsMessageLog::logMessage(
        QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) ),
        QStringLiteral( "Classification" ),
        Qgis::MessageLevel::Info );
      if ( statusBar() )
        statusBar()->showMessage(
          tr( "分类完成: %1 (%2 ms)" )
            .arg( QFileInfo( outForLog ).fileName() )
            .arg( r.durationMs ),
          6000 );
    }
    else
    {
      if ( statusBar() )
        statusBar()->showMessage(
          tr( "分类失败: %1" ).arg( r.errorMessage ), 6000 );
    }
  } );

  QgsApplication::taskManager()->addTask( task );
  if ( statusBar() )
    statusBar()->showMessage( tr( "分类中…" ), 3000 );
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
