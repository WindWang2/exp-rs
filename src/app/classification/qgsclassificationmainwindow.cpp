#include "qgsclassificationmainwindow.h"

#include "processing/algorithms/math_utils.h"
#include "core/sicnu_logging.h"
#include "rs_accuracy_dialog.h"
#include "rs_class_def.h"
#include "rs_class_quick_list.h"
#include "rs_class_table_widget.h"
#include "rs_classification_split.h"
#include "rs_classification_task.h"
#include "rs_classifier_kmeans.h"
#include "rs_classifier_load_dialog.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_setup_bar.h"
#include "rs_classifier_svm.h"
#include "rs_cross_validation.h"
#include "rs_cv_task.h"
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
#include "qgscoordinatereferencesystem.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayerstore.h"
#include "qgsmessagelog.h"
#include "qgsrasterlayer.h"
#include "qgstaskmanager.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
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

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal_priv.h>

#include <memory>

QgsClassificationMainWindow::QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , m_iface( iface )
{
  SICNU_LOG_INFO( SicnuLogTags::Classification, QStringLiteral( "Classification window opened" ) );
  setWindowTitle( tr( "Classification · 监督分类" ) );
  resize( 1280, 800 );

  m_rois = new RsRoiCollection( this );
  m_layerStore = new QgsMapLayerStore( this );

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
    m_rois->setClassDef( RsClassDef( d.first, d.second.first, QColor( d.second.second ) ) );
  }

  m_canvas = new QgsMapCanvas( this );
  m_canvas->setObjectName( QStringLiteral( "rsClassifyCanvas" ) );
  m_canvas->setCanvasColor( Qt::white );
  setCentralWidget( m_canvas );

  setupMenus();
  setupToolbars();
  setupDocks();
  setupRoiTools();
  setupClassifierBar();
  setupStatusBar();

  // Phase 10A review patch — JM matrix recompute throttle. m_rois::changed
  // restarts the timer; on fire, recomputeJmMatrix runs once.
  m_jmRecomputeTimer = new QTimer( this );
  m_jmRecomputeTimer->setSingleShot( true );
  m_jmRecomputeTimer->setInterval( 500 );
  connect( m_jmRecomputeTimer, &QTimer::timeout,
           this, &QgsClassificationMainWindow::recomputeJmMatrix );

  // Phase 10A review patch — feed dead controls.
  if ( m_rois )
  {
    connect( m_rois, &RsRoiCollection::changed,
             this, &QgsClassificationMainWindow::recomputeSpectralCurves );
    connect( m_rois, &RsRoiCollection::changed,
             this, [this]() {
               if ( m_jmRecomputeTimer )
                 m_jmRecomputeTimer->start();
             } );
  }
  if ( m_classTableWidget && m_spectralCurve )
  {
    connect( m_classTableWidget, &RsClassTableWidget::currentClassChanged,
             m_spectralCurve, &RsSpectralCurveWidget::setSelectedClass );
  }
}

QgsClassificationMainWindow::~QgsClassificationMainWindow() = default;

void QgsClassificationMainWindow::setupMenus()
{
  auto *fileMenu = menuBar()->addMenu( tr( "File" ) );
  m_openRasterAction = fileMenu->addAction(
    tr( "Open source raster..." ), this,
    static_cast<bool ( QgsClassificationMainWindow::* )()>(
      &QgsClassificationMainWindow::openSourceRaster ) );
  m_openRasterAction->setObjectName( QStringLiteral( "rsClassifyOpenRasterAction" ) );
  fileMenu->addAction( tr( "Load classifier model..." ), this,
                       &QgsClassificationMainWindow::loadClassifierModel );
  fileMenu->addAction( tr( "Load ROIs..." ), this,
                       &QgsClassificationMainWindow::loadRois );
  fileMenu->addAction( tr( "Save ROIs..." ), this,
                       &QgsClassificationMainWindow::exportRois );
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
  m_classListDock = new QDockWidget( tr( "类别管理" ), this );
  m_classListDock->setObjectName( QStringLiteral( "rsClassListDock" ) );
  m_classTableWidget = new RsClassTableWidget( m_classListDock );
  m_classTableWidget->setRoiCollection( m_rois );
  m_classListDock->setWidget( m_classTableWidget );
  addDockWidget( Qt::RightDockWidgetArea, m_classListDock );
  m_classListDock->resize( 380, m_classListDock->height() );

  m_classQuickListDock = new QDockWidget( tr( "类别快览" ), this );
  m_classQuickListDock->setObjectName( QStringLiteral( "rsClassQuickListDock" ) );
  m_classQuickListWidget = new RsClassQuickList( m_classQuickListDock );
  m_classQuickListWidget->setRoiCollection( m_rois );
  m_classQuickListDock->setWidget( m_classQuickListWidget );
  addDockWidget( Qt::LeftDockWidgetArea, m_classQuickListDock );

  m_jmDock = new QDockWidget( tr( "JM 分离度" ), this );
  m_jmDock->setObjectName( QStringLiteral( "rsClassJmDock" ) );
  m_jmMatrix = new RsJmMatrixWidget( m_jmDock );
  m_jmDock->setWidget( m_jmMatrix );
  addDockWidget( Qt::RightDockWidgetArea, m_jmDock );

  m_spectralDock = new QDockWidget( tr( "光谱曲线" ), this );
  m_spectralDock->setObjectName( QStringLiteral( "rsClassSpectralDock" ) );
  m_spectralCurve = new RsSpectralCurveWidget( m_spectralDock );
  m_spectralDock->setWidget( m_spectralCurve );
  addDockWidget( Qt::BottomDockWidgetArea, m_spectralDock );
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
  m_toolPoint = new RsRoiToolPoint( m_canvas );
  m_toolRect = new RsRoiToolRectangle( m_canvas );
  m_toolPolygon = new RsRoiToolPolygon( m_canvas );
  m_toolFreehand = new RsRoiToolFreehand( m_canvas );
  m_toolMagicWand = new RsRoiToolMagicWand( m_canvas );
  // Future-proofing: once Task 10.8 wires raster loading, m_sourceRasterPath
  // will be non-empty and the magic-wand can read pixels; until then the
  // tool's canvasReleaseEvent no-ops on an empty path.
  m_toolMagicWand->setSourceData( m_sourceRasterPath );

  const QVector<RsRoiToolBase *> tools = {
    m_toolPoint, m_toolRect, m_toolPolygon, m_toolFreehand, m_toolMagicWand
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
    { QStringLiteral( "rsToolRoiPoint" ), m_toolPoint },
    { QStringLiteral( "rsToolRoiRect" ), m_toolRect },
    { QStringLiteral( "rsToolRoiPolygon" ), m_toolPolygon },
    { QStringLiteral( "rsToolRoiFreehand" ), m_toolFreehand },
    { QStringLiteral( "rsToolRoiMagicWand" ), m_toolMagicWand },
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
      if ( !m_canvas )
        return;
      if ( on )
        m_canvas->setMapTool( t );
      else
        m_canvas->unsetMapTool( t );
    } );
  }

  // Track the currently-selected class so the next ROI gets the right id.
  if ( m_classTableWidget )
  {
    connect( m_classTableWidget, &RsClassTableWidget::currentClassChanged,
             this, &QgsClassificationMainWindow::onCurrentClassChanged );
  }
}

void QgsClassificationMainWindow::onCurrentClassChanged( int classId )
{
  if ( m_toolPoint )
    m_toolPoint->setCurrentClassId( classId );
  if ( m_toolRect )
    m_toolRect->setCurrentClassId( classId );
  if ( m_toolPolygon )
    m_toolPolygon->setCurrentClassId( classId );
  if ( m_toolFreehand )
    m_toolFreehand->setCurrentClassId( classId );
  if ( m_toolMagicWand )
    m_toolMagicWand->setCurrentClassId( classId );
}

void QgsClassificationMainWindow::setupClassifierBar()
{
  auto *bar = addToolBar( tr( "Classifier" ) );
  bar->setObjectName( QStringLiteral( "rsClassifierBar" ) );
  bar->setMovable( false );

  m_classifierBar = new RsClassifierSetupBar( bar );
  m_classifierBar->setObjectName( QStringLiteral( "rsClassifierSetupBar" ) );
  bar->addWidget( m_classifierBar );
  addToolBar( Qt::BottomToolBarArea, bar );

  connect( m_classifierBar, &RsClassifierSetupBar::applyRequested,
           this, &QgsClassificationMainWindow::applyClassification );
  // Phase 10A review patch — bar preview / cross-validate signals.
  connect( m_classifierBar, &RsClassifierSetupBar::previewRequested,
           this, &QgsClassificationMainWindow::applyPreview );
  connect( m_classifierBar, &RsClassifierSetupBar::crossValidateRequested,
           this, &QgsClassificationMainWindow::runCrossValidation );

  // The toolbar Apply action (from Task 10.2) routes to the same slot.
  if ( auto *apply = findChild<QAction *>(
         QStringLiteral( "rsClassifyApplyAction" ) ) )
  {
    m_applyAction = apply;
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
    if ( m_spectralDock )
      actSpectra->setChecked( m_spectralDock->isVisible() );
    connect( actSpectra, &QAction::toggled, this, [this]( bool on ) {
      if ( m_spectralDock )
        m_spectralDock->setVisible( on );
    } );
    if ( m_spectralDock )
    {
      connect( m_spectralDock, &QDockWidget::visibilityChanged,
               actSpectra, &QAction::setChecked );
    }
  }
  if ( auto *actSep = findChild<QAction *>( QStringLiteral( "rsToolSeparability" ) ) )
  {
    if ( m_jmDock )
      actSep->setChecked( m_jmDock->isVisible() );
    connect( actSep, &QAction::toggled, this, [this]( bool on ) {
      if ( m_jmDock )
        m_jmDock->setVisible( on );
    } );
    if ( m_jmDock )
    {
      connect( m_jmDock, &QDockWidget::visibilityChanged,
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
  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "Failed to open source raster: %1" ).arg( path ) );
    if ( statusBar() )
      statusBar()->showMessage( tr( "Failed to open raster: %1" ).arg( path ),
                                5000 );
    return false;
  }

  m_sourceRasterPath = path;
  m_sourceWidth = ds->GetRasterXSize();
  m_sourceHeight = ds->GetRasterYSize();
  m_sourceBandCount = ds->GetRasterCount();
  ds->GetGeoTransform( m_sourceGt );
  GDALClose( ds );

  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(),
                                    QStringLiteral( "gdal" ) );
  if ( layer->isValid() )
  {
    if ( m_layerStore )
      m_layerStore->addMapLayer( layer );
    m_sourceLayer = layer;
    if ( m_canvas )
    {
      m_canvas->setLayers( { layer } );
      m_canvas->setExtent( layer->extent() );
      m_canvas->refresh();
    }
  }
  else
  {
    delete layer;
  }

  if ( m_toolMagicWand )
    m_toolMagicWand->setSourceData( m_sourceRasterPath );
  if ( m_classifierBar )
    m_classifierBar->setSourceBands( m_sourceBandCount );

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Source raster loaded: %1 (%2x%3, %4 bands)" )
    .arg( QFileInfo( path ).fileName() ).arg( m_sourceWidth ).arg( m_sourceHeight ).arg( m_sourceBandCount ) );
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已加载源影像: %1 (%2×%3, %4 bands)" )
        .arg( QFileInfo( path ).fileName() )
        .arg( m_sourceWidth )
        .arg( m_sourceHeight )
        .arg( m_sourceBandCount ),
      4000 );
  return true;
}

bool QgsClassificationMainWindow::buildTrainingData( const QVector<int> &bands,
                                                     cv::Mat &X,
                                                     cv::Mat &y ) const
{
  if ( !m_rois || bands.isEmpty() || m_sourceRasterPath.isEmpty() )
    return false;
  if ( m_sourceWidth <= 0 || m_sourceHeight <= 0 )
    return false;

  // Open the raster and pre-read each selected band into memory. For tiny
  // training sets (typical ROI coverage is < 0.1% of the raster) this is
  // cheaper than per-pixel RasterIO calls. Future optimisation: bounding-box
  // tile reads.
  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( m_sourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
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

  // Collect (classId, pixelIdx) pairs first, recomputing rasterisation if the ROI
  // was loaded geometry-only.
  QVector<QPair<int, quint64>> samples;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), m_sourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        samples.push_back( qMakePair( roi.classId(), i ) );
    }
  }

  if ( samples.size() < 10 )
  {
    GDALClose( ds );
    return false;
  }

  // Read only the required training sample pixels from the GDAL bands.
  // Utilizing GDAL's block cache makes this highly performant without storing entire bands in memory.
  X.create( samples.size(), bands.size(), CV_32F );
  y.create( samples.size(), 1, CV_32S );

  for ( int bi = 0; bi < bands.size(); ++bi )
  {
    GDALRasterBand *band = ds->GetRasterBand( bands[bi] );
    for ( int s = 0; s < samples.size(); ++s )
    {
      const quint64 idx = samples[s].second;
      const int r = static_cast<int>( idx / W );
      const int c = static_cast<int>( idx % W );
      float val = 0.0f;
      const CPLErr err = band->RasterIO(
        GF_Read, c, r, 1, 1, &val,
        1, 1, GDT_Float32, 0, 0 );
      if ( err != CE_None )
      {
        GDALClose( ds );
        return false;
      }
      X.at<float>( s, bi ) = val;
    }
  }

  for ( int s = 0; s < samples.size(); ++s )
  {
    y.at<int>( s, 0 ) = samples[s].first;
  }

  GDALClose( ds );
  return true;
}

void QgsClassificationMainWindow::applyClassification()
{
  if ( m_sourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "请先 File → Open source raster..." ), 5000 );
    return;
  }
  if ( !m_classifierBar )
    return;

  QVector<int> bands = m_classifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    // Default to the first min(3, sourceBands) bands.
    const int n = std::min( 3, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
  {
    statusBar()->showMessage( tr( "无可用波段" ), 5000 );
    return;
  }

  QString outPath = m_classifierBar->outputPath();
  if ( outPath.isEmpty() )
  {
    outPath = QFileDialog::getSaveFileName(
      this, tr( "Output classified raster" ), QString(),
      tr( "GeoTIFF (*.tif)" ) );
    if ( outPath.isEmpty() )
      return;
    m_classifierBar->setOutputPath( outPath );
  }

  RsClassificationTask::Config cfg;
  cfg.sourceRaster = m_sourceRasterPath;
  cfg.outputRaster = outPath;
  cfg.bandIndices = bands;

  // Phase 10A.1.3 — class colour table is always built from the current ROI
  // collection so the output GTiff palette stays consistent across both the
  // train-from-ROIs flow and the load-from-disk flow.
  const QHash<int, RsClassDef> classDefs = m_rois ? m_rois->classDefs()
                                                 : QHash<int, RsClassDef>();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
    cfg.classColors[it.key()] = it.value().color();

  if ( m_loadedBackend )
  {
    // Phase 10A.1.3 — a model was loaded from disk via File → "Load
    // classifier model...". Consume it (one-shot) and skip training.
    // testX/testY left empty so RsClassificationTask::run() skips accuracy.
    cfg.algoName = QStringLiteral( "Loaded (%1)" ).arg( m_loadedBackend->name() );
    cfg.backend = std::move( m_loadedBackend );
    if ( statusBar() )
      statusBar()->showMessage( tr( "使用已加载模型 (跳过训练)" ), 3000 );
  }
  else
  {
    cv::Mat X, y;
    if ( !buildTrainingData( bands, X, y ) || X.rows < 10 )
    {
      statusBar()->showMessage(
        tr( "训练样本不足（< 10 像元）— 请先勾画 ROI 或加载已保存样本" ), 6000 );
      return;
    }

    // Phase 10A review patch — stratified 70/30 split (default ratio comes
    // from the train-ratio spinbox). testX/testY are reserved for Task 10.9
    // accuracy assessment.
    const auto split = RsClassificationSplit::stratifiedSplit(
      X, y, m_classifierBar->trainRatio() );
    cfg.trainX = split.trainX;
    cfg.trainY = split.trainY;
    cfg.testX = split.testX;
    cfg.testY = split.testY;

    switch ( m_classifierBar->currentKind() )
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
  }

  const QString algoForLog = cfg.algoName;
  const QString outForLog = outPath;
  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Classification started: algo=%1, bands=%2, output=%3" )
    .arg( cfg.algoName ).arg( cfg.bandIndices.size() ).arg( QFileInfo( outPath ).fileName() ) );
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
        if ( m_rois )
        {
          const auto defs = m_rois->classDefs();
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

  connect( task, &QgsTask::taskTerminated, this, [this]() {
    SICNU_LOG_WARN( SicnuLogTags::Classification, QStringLiteral( "Classification cancelled" ) );
    if ( statusBar() )
      statusBar()->showMessage( tr( "分类已取消" ), 3000 );
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
  // 10.5/10.7/10.8 will set m_sourceWidth/Height/Gt when the user opens a
  // raster; until then we record geometry-only ROIs.
  QSet<quint64> pixels;
  if ( m_sourceWidth > 0 && m_sourceHeight > 0 )
  {
    pixels = RsPixelRasterizer::rasterize( geom, m_sourceGt,
                                           m_sourceWidth, m_sourceHeight );
  }
  QVector<quint64> idx( pixels.begin(), pixels.end() );
  if ( m_rois )
    m_rois->appendRoi( RsRoi( classId, geom, idx ) );
}

// ---------------------------------------------------------------------------
// Phase 10A review patch — slot implementations.
// ---------------------------------------------------------------------------

void QgsClassificationMainWindow::applyPreview()
{
  if ( m_sourceRasterPath.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先 File → Open source raster..." ), 5000 );
    return;
  }
  if ( !m_classifierBar )
    return;

  QVector<int> bands = m_classifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    const int n = std::min( 3, m_sourceBandCount );
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
  cfg.sourceRaster = m_sourceRasterPath;
  cfg.outputRaster = outPath;
  cfg.bandIndices = bands;
  const auto split = RsClassificationSplit::stratifiedSplit(
    X, y, m_classifierBar->trainRatio() );
  cfg.trainX = split.trainX;
  cfg.trainY = split.trainY;
  cfg.testX = split.testX;
  cfg.testY = split.testY;

  const QHash<int, RsClassDef> classDefs = m_rois ? m_rois->classDefs()
                                                 : QHash<int, RsClassDef>();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
    cfg.classColors[it.key()] = it.value().color();

  switch ( m_classifierBar->currentKind() )
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
    // m_layerStore for lifetime management.
    auto *previewLayer = new QgsRasterLayer(
      outForLog, QStringLiteral( "classify_preview" ),
      QStringLiteral( "gdal" ) );
    if ( previewLayer->isValid() )
    {
      if ( m_layerStore )
        m_layerStore->addMapLayer( previewLayer );
      if ( m_canvas )
      {
        QList<QgsMapLayer *> layers;
        if ( m_sourceLayer )
          layers << previewLayer << m_sourceLayer;
        else
          layers << previewLayer;
        m_canvas->setLayers( layers );
        m_canvas->refresh();
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

  connect( task, &QgsTask::taskTerminated, this, [this]() {
    if ( statusBar() )
      statusBar()->showMessage( tr( "预览已取消" ), 3000 );
  } );

  QgsApplication::taskManager()->addTask( task );
  if ( statusBar() )
    statusBar()->showMessage( tr( "预览中…" ), 3000 );
}

void QgsClassificationMainWindow::runCrossValidation()
{
  // Phase 10A.1.2 — stratified 5-fold CV on the current ROIs.
  if ( m_sourceRasterPath.isEmpty() )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "请先 Open source raster…" ), 5000 );
    return;
  }
  if ( !m_classifierBar )
    return;

  QVector<int> bands = m_classifierBar->selectedBands();
  if ( bands.isEmpty() )
  {
    const int n = std::min( 3, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  cv::Mat X, y;
  if ( !buildTrainingData( bands, X, y ) || X.rows < 25 )
  {
    if ( statusBar() )
      statusBar()->showMessage( tr( "CV 需要 ≥ 25 像元" ), 5000 );
    return;
  }

  const auto kind = m_classifierBar->currentKind();
  if ( kind == RsClassifierKind::KMeans )
  {
    QMessageBox::information(
      this, tr( "K-Means CV" ),
      tr( "K-Means 交叉验证不适用 (cluster ↔ class 标签不齐)。\n"
          "请用 NormalBayes 或 SVM。" ) );
    return;
  }
  auto factory = [kind]() -> std::unique_ptr<RsClassifierBackend>
  {
    switch ( kind )
    {
      case RsClassifierKind::NormalBayes:
        return std::make_unique<RsClassifierNormalBayes>();
      case RsClassifierKind::SvmRbf:
        return std::make_unique<RsClassifierSvm>();
      default:
        return nullptr;
    }
  };

  // Run cross-validation asynchronously to avoid blocking the UI
  auto *task = new RsCvTask( X, y, factory, 5, tr( "5-fold Cross Validation" ) );

  task->setCompletionCallback( [this]( const RsCrossValidation::Result &res )
  {
    // This runs on the worker thread, so we need to invoke on the main thread
    QMetaObject::invokeMethod( this, [this, res]()
    {
      QString perFold;
      for ( int i = 0; i < res.foldAccuracies.size(); ++i )
        perFold += QString( "  fold%1: %2%\n" )
                     .arg( i + 1 )
                     .arg( res.foldAccuracies[i] * 100, 0, 'f', 1 );
      QMessageBox::information(
        this, tr( "5-fold Cross Validation" ),
        tr( "Mean accuracy: %1% ± %2%\n\n%3" )
          .arg( res.meanAccuracy * 100, 0, 'f', 1 )
          .arg( res.stdAccuracy * 100, 0, 'f', 1 )
          .arg( perFold ) );
    }, Qt::QueuedConnection );
  } );

  QgsApplication::taskManager()->addTask( task );
  statusBar()->showMessage( tr( "5-fold CV 运行中…" ), 3000 );
}

void QgsClassificationMainWindow::recomputeSpectralCurves()
{
  if ( !m_spectralCurve )
    return;
  if ( m_sourceRasterPath.isEmpty() || !m_rois )
  {
    m_spectralCurve->setClassCurves( {} );
    return;
  }
  if ( m_sourceWidth <= 0 || m_sourceHeight <= 0 )
    return;

  // Pick bands: use ClassifierBar selection if available; otherwise the
  // first min(6, m_sourceBandCount) bands.
  QVector<int> bands = m_classifierBar ? m_classifierBar->selectedBands()
                                      : QVector<int>{};
  if ( bands.isEmpty() )
  {
    const int n = std::min( 6, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
    return;

  // Read bands into memory once. Performance note: this re-reads the raster
  // on every ROI change. Acceptable for v1 (typical training rasters are
  // small / sampling is bounded by ROI pixel set); future optimisation is
  // tile-bounded RasterIO.
  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( m_sourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
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
  // Collect all unique ROI pixel indices (no need to read entire raster)
  QHash<int, QVector<quint64>> idxByClass;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), m_sourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    auto &bucket = idxByClass[roi.classId()];
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        bucket.push_back( i );
    }
  }

  // Read only ROI pixel values per band (much less memory than full raster)
  QHash<int, std::vector<double>> bandSums;   // classId -> per-band sum
  QHash<int, std::vector<double>> bandSumSq;  // classId -> per-band sum of squares
  QHash<int, int> classPixelCount;            // classId -> pixel count

  const QHash<int, RsClassDef> classDefs = m_rois->classDefs();
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
  {
    bandSums[it.key()].resize( bands.size(), 0.0 );
    bandSumSq[it.key()].resize( bands.size(), 0.0 );
    classPixelCount[it.key()] = 0;
  }

  for ( int bi = 0; bi < bands.size(); ++bi )
  {
    GDALRasterBandH band = ds->GetRasterBand( bands[bi] );
    if ( !band ) continue;

    for ( auto it = idxByClass.constBegin(); it != idxByClass.constEnd(); ++it )
    {
      int classId = it.key();
      const QVector<quint64> &px = it.value();
      for ( quint64 i : px )
      {
        int row = static_cast<int>( i / W );
        int col = static_cast<int>( i % W );
        float val = 0.0f;
        GDALRasterIO( band, GF_Read, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0 );
        bandSums[classId][bi] += val;
        bandSumSq[classId][bi] += val * val;
      }
      if ( bi == 0 )
        classPixelCount[classId] = px.size();
    }
  }
  GDALClose( ds );

  // Compute curves
  QVector<RsSpectralCurveWidget::Curve> curves;
  curves.reserve( classDefs.size() );
  for ( auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it )
  {
    int classId = it.key();
    int n = classPixelCount.value( classId, 0 );
    if ( n == 0 ) continue;

    RsSpectralCurveWidget::Curve curve;
    curve.classId = classId;
    curve.color = it.value().color();
    curve.name = it.value().name();
    curve.bandMeans.resize( bands.size() );
    curve.bandStds.resize( bands.size() );

    for ( int bi = 0; bi < bands.size(); ++bi )
    {
      MathUtils::AccumulatorStats accStats;
      accStats.count = n;
      accStats.sum = bandSums[classId][bi];
      accStats.sumSq = bandSumSq[classId][bi];
      accStats.min = 0;
      accStats.max = 0;
      MathUtils::Stats s = MathUtils::computeStatsFromAccumulators(accStats);
      curve.bandMeans[bi] = s.mean;
      curve.bandStds[bi] = s.stddev;
    }
    curves.push_back( curve );
  }
  m_spectralCurve->setClassCurves( curves );
}

void QgsClassificationMainWindow::recomputeJmMatrix()
{
  if ( !m_jmMatrix || !m_rois )
    return;
  if ( m_sourceRasterPath.isEmpty() )
    return;
  if ( m_sourceWidth <= 0 || m_sourceHeight <= 0 )
    return;

  // Same band set as the spectral curve dock for consistency.
  QVector<int> bands = m_classifierBar ? m_classifierBar->selectedBands()
                                      : QVector<int>{};
  if ( bands.isEmpty() )
  {
    const int n = std::min( 6, m_sourceBandCount );
    for ( int i = 1; i <= n; ++i )
      bands.push_back( i );
  }
  if ( bands.isEmpty() )
    return;

  ensureGdalInit();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( m_sourceRasterPath.toUtf8().constData(), GA_ReadOnly ) );
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
  // Collect ROI pixel indices per class
  QHash<int, QVector<quint64>> idxByClass;
  for ( const RsRoi &roi : m_rois->rois() )
  {
    if ( roi.classId() <= 0 )
      continue;
    QVector<quint64> idx = roi.pixelIndices();
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        roi.geometry(), m_sourceGt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    auto &bucket = idxByClass[roi.classId()];
    for ( quint64 i : idx )
    {
      if ( i < static_cast<quint64>( W ) * H )
        bucket.push_back( i );
    }
  }

  // Read only ROI pixel values per band (much less memory than full raster)
  QHash<int, cv::Mat> samplesByClass;
  for ( auto it = idxByClass.constBegin(); it != idxByClass.constEnd(); ++it )
  {
    const auto &px = it.value();
    if ( px.size() < 2 )
      continue;
    cv::Mat m( px.size(), bands.size(), CV_32F );
    for ( int bi = 0; bi < bands.size(); ++bi )
    {
      GDALRasterBandH band = ds->GetRasterBand( bands[bi] );
      if ( !band ) continue;
      for ( int r = 0; r < px.size(); ++r )
      {
        int row = static_cast<int>( px[r] / W );
        int col = static_cast<int>( px[r] % W );
        float val = 0.0f;
        GDALRasterIO( band, GF_Read, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0 );
        m.at<float>( r, bi ) = val;
      }
    }
    samplesByClass.insert( it.key(), m );
  }
  GDALClose( ds );

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
  const QHash<int, RsClassDef> classDefs = m_rois->classDefs();
  QList<int> defIds = classDefs.keys();
  std::sort( defIds.begin(), defIds.end() );
  for ( int id : defIds )
  {
    const RsClassDef d = classDefs.value( id );
    classes.push_back( { d.id(), d.name(), d.color() } );
  }
  m_jmMatrix->setData( classes, jm );
}

void QgsClassificationMainWindow::exportRois()
{
  if ( !m_rois || m_rois->size() == 0 )
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

  QgsCoordinateReferenceSystem crs;
  if ( m_sourceLayer )
  {
    crs = m_sourceLayer->crs();
  }

  const bool ok = RsRoiIO::save( path, *m_rois, crs );
  if ( ok )
    SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "ROIs exported: %1 (%2 ROIs)" ).arg( QFileInfo( path ).fileName() ).arg( m_rois->size() ) );
  else
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "ROI export failed: %1" ).arg( path ) );
  if ( statusBar() )
    statusBar()->showMessage(
      ok ? tr( "已导出 ROI: %1" ).arg( QFileInfo( path ).fileName() )
         : tr( "ROI 导出失败: %1" ).arg( path ),
      5000 );
}

void QgsClassificationMainWindow::loadRois()
{
  if ( !m_rois )
  {
    m_rois = new RsRoiCollection( this );
  }

  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load ROIs" ), QString(),
    tr( "ESRI Shapefile (*.shp)" ) );
  if ( path.isEmpty() )
    return;

  QgsCoordinateReferenceSystem crs;
  if ( m_sourceLayer )
  {
    crs = m_sourceLayer->crs();
  }

  const bool ok = RsRoiIO::load( path, *m_rois, crs );
  if ( ok )
    SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "ROIs loaded from %1" ).arg( path ) );
  if ( ok )
  {
    // Recompute pixel indices for each ROI if the source raster is valid
    if ( m_sourceWidth > 0 && m_sourceHeight > 0 )
    {
      QVector<RsRoi> loadedRois = m_rois->rois();
      m_rois->clear();
      for ( RsRoi &roi : loadedRois )
      {
        QSet<quint64> pixels = RsPixelRasterizer::rasterize(
          roi.geometry(), m_sourceGt, m_sourceWidth, m_sourceHeight );
        roi.setPixelIndices( QVector<quint64>( pixels.begin(), pixels.end() ) );
        m_rois->appendRoi( roi );
      }
    }
    
    if ( statusBar() )
      statusBar()->showMessage( tr( "成功加载 %1 个 ROI" ).arg( m_rois->size() ), 5000 );
  }
  else
  {
    QMessageBox::critical(
      this, tr( "Error" ),
      tr( "Failed to load ROIs from %1" ).arg( path ) );
  }
}

// ---------------------------------------------------------------------------
// Phase 10A.1.3 — Load classifier model from disk.
// ---------------------------------------------------------------------------

void QgsClassificationMainWindow::loadClassifierModel()
{
  RsClassifierLoadDialog dlg( this );
  if ( dlg.exec() != QDialog::Accepted )
    return;

  std::unique_ptr<RsClassifierBackend> backend;
  switch ( dlg.selectedKind() )
  {
    case RsClassifierLoadDialog::BackendKind::NormalBayes:
      backend = std::make_unique<RsClassifierNormalBayes>();
      break;
    case RsClassifierLoadDialog::BackendKind::SvmRbf:
      backend = std::make_unique<RsClassifierSvm>();
      break;
  }

  if ( !backend || !backend->load( dlg.modelPath() ) )
  {
    QMessageBox::warning(
      this, tr( "Load failed" ),
      tr( "无法加载模型：%1" ).arg( dlg.modelPath() ) );
    return;
  }
  m_loadedBackend = std::move( backend );
  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Classifier model loaded: %1" ).arg( dlg.modelPath() ) );
  if ( statusBar() )
    statusBar()->showMessage(
      tr( "已加载模型 — 下次 Apply 将跳过训练，直接 predict" ), 0 );
}
