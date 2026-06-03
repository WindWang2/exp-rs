#include "qgsclassificationmainwindow.h"

#include "rs_accuracy_dialog.h"
#include "rs_class_def.h"
#include "rs_class_quick_list.h"
#include "rs_class_table_widget.h"
#include "rs_classification_split.h"
#include "rs_classification_task.h"
#include "rs_classifier_kmeans.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_setup_bar.h"
#include "rs_classifier_svm.h"
#include "rs_jm_matrix_widget.h"
#include "rs_jm_separability.h"
#include "rs_pixel_rasterizer.h"
#include "rs_roi.h"
#include "rs_roi_collection.h"
#include "rs_roi_io.h"
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
#include <QDir>
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
#include <QMessageBox>
#include <QPair>
#include <QSet>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVector>
#include <QWidget>

#include <opencv2/ml.hpp>

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

  // Phase 10A review patch — JM matrix recompute throttle. mRois::changed
  // restarts the timer; on fire, recomputeJmMatrix runs once.
  mJmRecomputeTimer = new QTimer( this );
  mJmRecomputeTimer->setSingleShot( true );
  mJmRecomputeTimer->setInterval( 500 );
  connect( mJmRecomputeTimer, &QTimer::timeout,
           this, &QgsClassificationMainWindow::recomputeJmMatrix );

  // Phase 10A review patch — feed dead controls.
  if ( mRois )
  {
    connect( mRois, &RsRoiCollection::changed,
             this, &QgsClassificationMainWindow::recomputeSpectralCurves );
    connect( mRois, &RsRoiCollection::changed,
             this, [this]() {
               if ( mJmRecomputeTimer )
                 mJmRecomputeTimer->start();
             } );
  }
  if ( mClassTableWidget && mSpectralCurve )
  {
    connect( mClassTableWidget, &RsClassTableWidget::currentClassChanged,
             mSpectralCurve, &RsSpectralCurveWidget::setSelectedClass );
  }
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
  // Phase 10A review patch — wire the three trailing toolbar actions:
  // Spectra/Separability toggle their dock visibility, Export saves to .shp.
  auto *actSpectra = roiBar->addAction( tr( "Spectra" ) );
  actSpectra->setObjectName( QStringLiteral( "rsToolSpectra" ) );
  actSpectra->setCheckable( true );
  auto *actSeparability = roiBar->addAction( tr( "Separability" ) );
  actSeparability->setObjectName( QStringLiteral( "rsToolSeparability" ) );
  actSeparability->setCheckable( true );
  auto *actExport = roiBar->addAction( tr( "Export ROIs" ) );
  actExport->setObjectName( QStringLiteral( "rsToolExportRois" ) );

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  roiBar->addWidget( spacer );

  auto *preview = roiBar->addAction( tr( "Quick preview" ) );
  preview->setObjectName( QStringLiteral( "rsClassifyPreviewAction" ) );
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
  // Phase 10A review patch — bar preview / cross-validate signals.
  connect( mClassifierBar, &RsClassifierSetupBar::previewRequested,
           this, &QgsClassificationMainWindow::applyPreview );
  connect( mClassifierBar, &RsClassifierSetupBar::crossValidateRequested,
           this, &QgsClassificationMainWindow::runCrossValidation );

  // The toolbar Apply action (from Task 10.2) routes to the same slot.
  if ( auto *apply = findChild<QAction *>(
         QStringLiteral( "rsClassifyApplyAction" ) ) )
  {
    mApplyAction = apply;
    connect( apply, &QAction::triggered,
             this, &QgsClassificationMainWindow::applyClassification );
  }
  // Phase 10A review patch — toolbar Quick preview routes to applyPreview.
  if ( auto *preview = findChild<QAction *>(
         QStringLiteral( "rsClassifyPreviewAction" ) ) )
  {
    connect( preview, &QAction::triggered,
             this, &QgsClassificationMainWindow::applyPreview );
  }
  // Phase 10A review patch — Spectra/Separability toggle dock visibility,
  // Export ROIs saves to a shapefile via RsRoiIO.
  if ( auto *actSpectra = findChild<QAction *>( QStringLiteral( "rsToolSpectra" ) ) )
  {
    if ( mSpectralDock )
      actSpectra->setChecked( mSpectralDock->isVisible() );
    connect( actSpectra, &QAction::toggled, this, [this]( bool on ) {
      if ( mSpectralDock )
        mSpectralDock->setVisible( on );
    } );
    if ( mSpectralDock )
    {
      connect( mSpectralDock, &QDockWidget::visibilityChanged,
               actSpectra, &QAction::setChecked );
    }
  }
  if ( auto *actSep = findChild<QAction *>( QStringLiteral( "rsToolSeparability" ) ) )
  {
    if ( mJmDock )
      actSep->setChecked( mJmDock->isVisible() );
    connect( actSep, &QAction::toggled, this, [this]( bool on ) {
      if ( mJmDock )
        mJmDock->setVisible( on );
    } );
    if ( mJmDock )
    {
      connect( mJmDock, &QDockWidget::visibilityChanged,
               actSep, &QAction::setChecked );
    }
  }
  if ( auto *actExport = findChild<QAction *>( QStringLiteral( "rsToolExportRois" ) ) )
  {
    connect( actExport, &QAction::triggered,
             this, &QgsClassificationMainWindow::exportRois );
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
  // Phase 10A review patch — stratified 70/30 split (default ratio comes
  // from the train-ratio spinbox). testX/testY are reserved for Task 10.9
  // accuracy assessment.
  const auto split = RsClassificationSplit::stratifiedSplit(
    X, y, mClassifierBar->trainRatio() );
  cfg.trainX = split.trainX;
  cfg.trainY = split.trainY;
  cfg.testX = split.testX;
  cfg.testY = split.testY;

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
      QJsonObject obj{
        { QStringLiteral( "event" ), QStringLiteral( "classify_finished" ) },
        { QStringLiteral( "algo" ), algoForLog },
        { QStringLiteral( "total_pixels" ), r.totalPixels },
        { QStringLiteral( "duration_ms" ), r.durationMs },
        { QStringLiteral( "status" ), QStringLiteral( "ok" ) }
      };
      if ( !r.accuracy.classIds.isEmpty() )
      {
        obj.insert( QStringLiteral( "overall_accuracy" ), r.accuracy.overallAccuracy );
        obj.insert( QStringLiteral( "kappa" ), r.accuracy.kappa );
      }
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

      // Phase 10A Task 10.9 — present accuracy dialog when the task
      // computed metrics from the held-out split. Non-modal so the
      // lambda returns promptly.
      if ( !r.accuracy.classIds.isEmpty() )
      {
        QHash<int, QString> classNames;
        if ( mRois )
        {
          const auto defs = mRois->classDefs();
          for ( auto it = defs.constBegin(); it != defs.constEnd(); ++it )
            classNames[it.key()] = it.value().name();
        }
        auto *dlg = new RsAccuracyDialog( r.accuracy, classNames, this );
        dlg->setAttribute( Qt::WA_DeleteOnClose );
        dlg->show();
      }
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

// ---------------------------------------------------------------------------
// Phase 10A review patch — slot implementations.
// ---------------------------------------------------------------------------

void QgsClassificationMainWindow::applyPreview()
{
  if ( mSourceRasterPath.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先 File → Open source raster..." ), 5000 );
    return;
  }
  if ( !mClassifierBar )
    return;

  QVector<int> bands = mClassifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    const int n = std::min( 3, mSourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "无可用波段" ), 5000 );
    return;
  }

  cv::Mat X, y;
  if ( !buildTrainingData( bands, X, y ) || X.rows < 10 )
  {
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "训练样本不足（< 10 像元）— 请先勾画 ROI 或加载已保存样本" ), 6000 );
    return;
  }

  // v1 simplification: classify the FULL raster but route output to a
  // temp file, skip the file dialog, and add the result as a temporary
  // canvas layer. Viewport-cropped windowed classification deferred.
  const QString outPath = QDir::temp().filePath(
    QStringLiteral( "classify_preview.tif" ) );

  RsClassificationTask::Config cfg;
  cfg.sourceRaster = mSourceRasterPath;
  cfg.outputRaster = outPath;
  cfg.bandIndices = bands;
  const auto split = RsClassificationSplit::stratifiedSplit(
    X, y, mClassifierBar->trainRatio() );
  cfg.trainX = split.trainX;
  cfg.trainY = split.trainY;
  cfg.testX = split.testX;
  cfg.testY = split.testY;

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

  auto *task = new RsClassificationTask( std::move( cfg ) );
  const QString outForLog = outPath;
  connect( task, &QgsTask::taskCompleted, this, [this, task, outForLog]() {
    const auto &r = task->result();
    if ( !r.ok )
    {
      if ( statusBar() )
        statusBar()->showMessage(
          tr( "预览失败: %1" ).arg( r.errorMessage ), 6000 );
      return;
    }
    // Add the preview as a temporary layer on the canvas. Reuses
    // mLayerStore for lifetime management.
    auto *previewLayer = new QgsRasterLayer(
      outForLog, QStringLiteral( "classify_preview" ),
      QStringLiteral( "gdal" ) );
    if ( previewLayer->isValid() )
    {
      if ( mLayerStore )
        mLayerStore->addMapLayer( previewLayer );
      if ( mCanvas )
      {
        QList<QgsMapLayer *> layers;
        if ( mSourceLayer )
          layers << previewLayer << mSourceLayer;
        else
          layers << previewLayer;
        mCanvas->setLayers( layers );
        mCanvas->refresh();
      }
    }
    else
    {
      delete previewLayer;
    }
    if ( statusBar() )
      statusBar()->showMessage(
        tr( "预览完成 (%1 ms)" ).arg( r.durationMs ), 5000 );
  } );
  QgsApplication::taskManager()->addTask( task );
  if ( statusBar() )
    statusBar()->showMessage( tr( "预览中…" ), 3000 );
}

void QgsClassificationMainWindow::runCrossValidation()
{
  // v1 stub — a proper k-fold loop with OpenCV's TrainData split is more
  // than 50 lines once we account for backend re-fit + label remapping.
  // Defer to Phase 10A.1 to keep this review patch focused.
  QMessageBox::information(
    this, tr( "交叉验证" ),
    tr( "Cross-validation coming soon — Phase 10A.1" ) );
}

void QgsClassificationMainWindow::recomputeSpectralCurves()
{
  if ( !mSpectralCurve )
    return;
  if ( mSourceRasterPath.isEmpty() || !mRois )
  {
    mSpectralCurve->setClassCurves( {} );
    return;
  }
  if ( mSourceWidth <= 0 || mSourceHeight <= 0 )
    return;

  // Pick bands: use ClassifierBar selection if available; otherwise the
  // first min(6, mSourceBandCount) bands.
  QVector<int> bands = mClassifierBar ? mClassifierBar->selectedBands()
                                      : QVector<int>{};
  if ( bands.isEmpty() )
  {
    const int n = std::min( 6, mSourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
    return;

  // Read bands into memory once. Performance note: this re-reads the raster
  // on every ROI change. Acceptable for v1 (typical training rasters are
  // small / sampling is bounded by ROI pixel set); future optimisation is
  // tile-bounded RasterIO.
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( mSourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( ds );
      return;
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
      return;
    }
  }
  GDALClose( ds );

  // Bucket pixel indices by class.
  QHash<int, QVector<quint64>> idxByClass;
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
    auto &bucket = idxByClass[roi.classId()];
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        bucket.push_back( i );
    }
  }

  const QHash<int, RsClassDef> classDefs = mRois->classDefs();
  QVector<RsSpectralCurveWidget::Curve> curves;
  curves.reserve( classDefs.size() );
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
  {
    const QVector<quint64> &px = idxByClass.value( it.key() );
    if ( px.isEmpty() )
      continue;
    RsSpectralCurveWidget::Curve curve;
    curve.classId = it.key();
    curve.color = it.value().color();
    curve.name = it.value().name();
    curve.bandMeans.resize( bands.size() );
    curve.bandStds.resize( bands.size() );
    for ( int bi = 0; bi < bands.size(); ++bi )
    {
      double sum = 0.0;
      double sumSq = 0.0;
      const auto &buf = bandBufs[bi];
      for ( quint64 i : px )
      {
        const double v = buf[i];
        sum += v;
        sumSq += v * v;
      }
      const double n = static_cast<double>( px.size() );
      const double mean = sum / n;
      const double var = std::max( 0.0, sumSq / n - mean * mean );
      curve.bandMeans[bi] = mean;
      curve.bandStds[bi] = std::sqrt( var );
    }
    curves.push_back( curve );
  }
  mSpectralCurve->setClassCurves( curves );
}

void QgsClassificationMainWindow::recomputeJmMatrix()
{
  if ( !mJmMatrix || !mRois )
    return;
  if ( mSourceRasterPath.isEmpty() )
    return;
  if ( mSourceWidth <= 0 || mSourceHeight <= 0 )
    return;

  // Same band set as the spectral curve dock for consistency.
  QVector<int> bands = mClassifierBar ? mClassifierBar->selectedBands()
                                      : QVector<int>{};
  if ( bands.isEmpty() )
  {
    const int n = std::min( 6, mSourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
    return;

  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( mSourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;
  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( ds );
      return;
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
      return;
    }
  }
  GDALClose( ds );

  // Bucket pixel indices by class, then build a cv::Mat per class.
  QHash<int, QVector<quint64>> idxByClass;
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
    auto &bucket = idxByClass[roi.classId()];
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        bucket.push_back( i );
    }
  }

  QHash<int, cv::Mat> samplesByClass;
  for ( auto it = idxByClass.constBegin(); it != idxByClass.constEnd(); ++it )
  {
    const auto &px = it.value();
    if ( px.size() < 2 )
      continue;
    cv::Mat m( px.size(), bands.size(), CV_32F );
    for ( int r = 0; r < px.size(); ++r )
    {
      for ( int bi = 0; bi < bands.size(); ++bi )
        m.at<float>( r, bi ) = bandBufs[bi][px[r]];
    }
    samplesByClass.insert( it.key(), m );
  }

  // Sorted class id list for pairwise iteration.
  QList<int> ids = samplesByClass.keys();
  std::sort( ids.begin(), ids.end() );

  QHash<QPair<int, int>, double> jm;
  for ( int i = 0; i < ids.size(); ++i )
  {
    for ( int j = i + 1; j < ids.size(); ++j )
    {
      const double d = RsJmSeparability::pairJm(
        samplesByClass.value( ids[i] ),
        samplesByClass.value( ids[j] ) );
      jm.insert( qMakePair( ids[i], ids[j] ), d );
    }
  }

  QVector<RsJmMatrixWidget::ClassEntry> classes;
  const QHash<int, RsClassDef> classDefs = mRois->classDefs();
  QList<int> defIds = classDefs.keys();
  std::sort( defIds.begin(), defIds.end() );
  for ( int id : defIds )
  {
    const RsClassDef d = classDefs.value( id );
    classes.push_back( { d.id(), d.name(), d.color() } );
  }
  mJmMatrix->setData( classes, jm );
}

void QgsClassificationMainWindow::exportRois()
{
  if ( !mRois || mRois->size() == 0 )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "无 ROI 可导出" ), 4000 );
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Export ROIs" ), QString(),
    tr( "ESRI Shapefile (*.shp)" ) );
  if ( path.isEmpty() )
    return;
  const bool ok = RsRoiIO::save( path, *mRois );
  if ( statusBar() )
    statusBar()->showMessage(
      ok ? tr( "已导出 ROI: %1" ).arg( QFileInfo( path ).fileName() )
         : tr( "ROI 导出失败: %1" ).arg( path ),
      5000 );
}
