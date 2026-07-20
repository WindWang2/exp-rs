// rs_obia_main_window.cpp — Phase 10B Task 10B.5
#include "rs_obia_main_window.h"
#include "dialogs/dialog_help_catalog.h"
#include "sicnu_logging.h"

#include "rs_obia_task.h"
#include "rs_obia_segmentation.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"
#include "rs_classifier_kmeans.h"

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgstaskmanager.h>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>
#include <qgsdockwidget.h>

#include <QAction>
#include <QComboBox>
#include <QGuiApplication>
#include <QFileDialog>
#include <QSpinBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <gdal.h>

// ---------------------------------------------------------------------------
// Default class definitions (same as Phase 10A)
// ---------------------------------------------------------------------------
static QVector<RsObiaMainWindow::ClassDef> defaultClassDefs()
{
    return {
        { 1, QObject::tr( "Forest" ), QColor( 34, 139, 34 ) },
        { 2, QObject::tr( "Grassland" ), QColor( 144, 238, 144 ) },
        { 3, QObject::tr( "Water" ), QColor( 30, 144, 255 ) },
        { 4, QObject::tr( "Built-up" ), QColor( 255, 99, 71 ) },
        { 5, QObject::tr( "Cropland" ), QColor( 255, 215, 0 ) },
        { 6, QObject::tr( "Bare Soil" ), QColor( 210, 180, 140 ) },
    };
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

RsObiaMainWindow::RsObiaMainWindow( QWidget *parent )
    : QMainWindow( parent )
    , mClassDefs( defaultClassDefs() )
{
    setWindowTitle( tr( "OBIA — Object-Based Classification" ) );
  setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "obia" ), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( QStringLiteral( "obia" ), windowTitle() ) );
    resize( 1200, 800 );

    setupUi();
    setupToolbar();
    setupDocks();
    setupMapCanvas();
    updateStatusLabel();
}

RsObiaMainWindow::~RsObiaMainWindow() = default;

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void RsObiaMainWindow::setupUi()
{
    // Central widget: map canvas in a frame
    mCanvas = new QgsMapCanvas( this );
    setCentralWidget( mCanvas );
}

void RsObiaMainWindow::setupToolbar()
{
    mToolbar = addToolBar( tr( "OBIA" ) );
    mToolbar->setObjectName( "obiaToolbar" );

    mToolbar->addAction( tr( "Load Raster" ), this, &RsObiaMainWindow::loadRaster );

    mToolbar->addSeparator();

    // Segmentation params
    mToolbar->addWidget( new QLabel( tr( " Segments:" ) ) );
    auto *kernelSpin = new QSpinBox;
    kernelSpin->setRange( 3, 21 );
    kernelSpin->setValue( 5 );
    kernelSpin->setToolTip( tr( "Smoothing kernel size" ) );
    kernelSpin->setObjectName( "kernelSpin" );
    mToolbar->addWidget( kernelSpin );

    auto *binsSpin = new QSpinBox;
    binsSpin->setRange( 2, 128 );
    binsSpin->setValue( 32 );
    binsSpin->setToolTip( tr( "Quantization bins (built-in segmenter fallback)" ) );
    binsSpin->setObjectName( "binsSpin" );
    mToolbar->addWidget( binsSpin );

    auto *minRegionSpin = new QSpinBox;
    minRegionSpin->setRange( 10, 10000 );
    minRegionSpin->setValue( 100 );
    minRegionSpin->setToolTip( tr( "Minimum region size (OTB MeanShift / built-in merge)" ) );
    minRegionSpin->setObjectName( "minRegionSpin" );
    mToolbar->addWidget( minRegionSpin );

    mToolbar->addAction( tr( "Segment" ), this, &RsObiaMainWindow::runSegmentation );

    mToolbar->addSeparator();

    // Classifier selection
    mToolbar->addWidget( new QLabel( tr( " Classifier:" ) ) );
    auto *classifierCombo = new QComboBox;
    classifierCombo->addItems( { "NormalBayes", "SVM", "KMeans" } );
    classifierCombo->setObjectName( "classifierCombo" );
    mToolbar->addWidget( classifierCombo );

    mToolbar->addAction( tr( "Classify" ), this, &RsObiaMainWindow::runClassification );

    mToolbar->addSeparator();
    mToolbar->addAction( tr( "Export" ), this, &RsObiaMainWindow::exportResult );
}

void RsObiaMainWindow::setupDocks()
{
    // Left dock: class assignment
    mClassDock = new QDockWidget( tr( "Classes" ), this );
    mClassDock->setObjectName( "obiaClassDock" );
    mClassTable = new QTableWidget;
    mClassTable->setColumnCount( 3 );
    mClassTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Name" ), tr( "Color" ) } );
    mClassTable->horizontalHeader()->setStretchLastSection( true );
    mClassTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mClassTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mClassTable->setMinimumWidth( 180 );

    // Populate class table
    mClassTable->setRowCount( mClassDefs.size() );
    for ( int i = 0; i < mClassDefs.size(); ++i )
    {
        mClassTable->setItem( i, 0, new QTableWidgetItem( QString::number( mClassDefs[i].id ) ) );
        mClassTable->setItem( i, 1, new QTableWidgetItem( mClassDefs[i].name ) );
        auto *colorItem = new QTableWidgetItem;
        colorItem->setBackground( mClassDefs[i].color );
        mClassTable->setItem( i, 2, colorItem );
    }
    connect( mClassTable, &QTableWidget::cellClicked, this, [this]( int row, int ) {
        if ( row >= 0 && row < mClassDefs.size() )
            mCurrentClassId = mClassDefs[row].id;
    } );

    // Assign button
    auto *assignBtn = new QPushButton( tr( "Assign to Selected Segment" ) );
    connect( assignBtn, &QPushButton::clicked, this, &RsObiaMainWindow::onAssignClass );

    auto *classWidget = new QWidget;
    auto *classLayout = new QVBoxLayout( classWidget );
    classLayout->addWidget( mClassTable );
    classLayout->addWidget( assignBtn );
    classLayout->setContentsMargins( 0, 0, 0, 0 );
    mClassDock->setWidget( classWidget );
    addDockWidget( Qt::LeftDockWidgetArea, mClassDock );

    // Bottom dock: segment info
    mInfoDock = new RsSegmentInfoDock( this );
    addDockWidget( Qt::BottomDockWidgetArea, mInfoDock );

    // Right dock: segment list
    mSegmentDock = new QDockWidget( tr( "Segments" ), this );
    mSegmentDock->setObjectName( "obiaSegmentDock" );
    mSegmentTable = new QTableWidget;
    mSegmentTable->setColumnCount( 3 );
    mSegmentTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Pixels" ), tr( "Class" ) } );
    mSegmentTable->horizontalHeader()->setStretchLastSection( true );
    mSegmentTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mSegmentDock->setWidget( mSegmentTable );
    addDockWidget( Qt::RightDockWidgetArea, mSegmentDock );
}

void RsObiaMainWindow::setupMapCanvas()
{
    // HIGH #4 fix: QgsLayerTree has no QObject parent, so we manage it manually.
    // mLayerTreeModel takes ownership of mLayerTree. mLayerView parented to this.
    mLayerTree = new QgsLayerTree;
    mLayerTreeModel = new QgsLayerTreeModel( mLayerTree ); // takes ownership
    mLayerView = new QgsLayerTreeView( this );
    mLayerView->setModel( mLayerTreeModel );

    mCanvas->setLayers( {} );
    mCanvas->setDestinationCrs( QgsCoordinateReferenceSystem() );
    mCanvas->refresh();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void RsObiaMainWindow::loadRaster()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr( "Open Raster" ), QString(),
        tr( "Raster files (*.tif *.tiff *.img *.jp2 *.png);;All files (*)" ) );

    if ( path.isEmpty() )
        return;

    SICNU_LOG_INFO( SicnuLogTags::OBIA, QString( "Loading raster: %1" ).arg( path ) );

    auto layer = std::make_shared<QgsRasterLayer>( path, QFileInfo( path ).baseName() );
    if ( !layer->isValid() )
    {
        SICNU_LOG_ERROR( SicnuLogTags::OBIA, QString( "Invalid raster: %1" ).arg( path ) );
        QMessageBox::warning( this, tr( "Error" ), tr( "Cannot open raster: %1" ).arg( path ) );
        return;
    }

    mRasterLayer = layer;
    mRasterPath = path;
    mBandCount = layer->bandCount();

    // Update canvas
    mCanvas->setLayers( { mRasterLayer.get() } );
    mCanvas->setExtent( layer->extent() );
    mCanvas->refresh();

    // Reset segmentation
    mSegMap = RsSegmentMap();
    mSegStats.clear();
    mSegmentLabels.clear();
    updateSegmentTable();
    updateStatusLabel();

    statusBar()->showMessage( tr( "Loaded: %1 (%2 bands)" ).arg( path ).arg( mBandCount ), 5000 );
}

void RsObiaMainWindow::runSegmentation()
{
    if ( mRasterPath.isEmpty() )
    {
        SICNU_LOG_WARN( SicnuLogTags::OBIA, "Segmentation requested with no raster loaded" );
        QMessageBox::information( this, tr( "OBIA" ), tr( "Load a raster first." ) );
        return;
    }

    SICNU_LOG_INFO( SicnuLogTags::OBIA, QString( "Starting segmentation: %1" ).arg( mRasterPath ) );

    QVector<int> bandIndices;
    for ( int b = 1; b <= mBandCount; ++b )
        bandIndices.append( b );

    auto *kernelSpin = findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = findChild<QSpinBox *>( "binsSpin" );
    auto *minRegionSpin = findChild<QSpinBox *>( "minRegionSpin" );

    RsObiaSegmentationConfig segCfg;
    segCfg.rasterPath = mRasterPath;
    segCfg.bandIndices = bandIndices;
    segCfg.preferOtb = true;
    segCfg.smoothKernel = kernelSpin ? kernelSpin->value() : 5;
    segCfg.quantizeBins = binsSpin ? binsSpin->value() : 32;
    segCfg.minRegionSize = minRegionSpin ? minRegionSpin->value() : 100;

    const QString rasterPath = mRasterPath;

    // Cancel token shared with the worker
    auto canceled = std::make_shared<std::atomic<bool>>( false );

    auto *progress = new QProgressDialog(
        RsObiaSegmentation::isOtbAvailable()
            ? tr( "Segmenting with OTB MeanShift..." )
            : tr( "Segmenting with built-in segmenter..." ),
        tr( "Cancel" ), 0, 0, this );
    progress->setWindowModality( Qt::WindowModal );
    progress->setMinimumDuration( 0 );
    progress->setValue( 0 );
    progress->show();
    connect( progress, &QProgressDialog::canceled, this, [canceled]() {
        canceled->store( true );
    } );

    QGuiApplication::setOverrideCursor( Qt::WaitCursor );
    statusBar()->showMessage( progress->labelText() );

    // Bundle segmentation + feature extraction so both stay off the GUI thread.
    struct SegWorkResult
    {
        RsObiaSegmentationResult seg;
        QMap<quint32, RsSegmentFeatures::SegmentStat> stats;
    };

    auto *watcher = new QFutureWatcher<SegWorkResult>( this );
    connect( watcher, &QFutureWatcher<SegWorkResult>::finished, this,
             [this, watcher, progress]() {
                 QGuiApplication::restoreOverrideCursor();
                 progress->reset();
                 progress->deleteLater();

                 const SegWorkResult work = watcher->result();
                 watcher->deleteLater();

                 if ( !work.seg.ok )
                 {
                     if ( work.seg.errorMessage.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) )
                         statusBar()->showMessage( tr( "Segmentation canceled" ), 3000 );
                     else
                         QMessageBox::warning( this, tr( "Error" ), work.seg.errorMessage );
                     updateStatusLabel();
                     return;
                 }

                 applySegmentationResult( work.seg.segMap, work.seg.usedOtb, work.stats );
             } );

    watcher->setFuture( QtConcurrent::run( [segCfg, bandIndices, rasterPath, canceled]() {
        static bool s_gdalInit = ( GDALAllRegister(), true );
        Q_UNUSED( s_gdalInit );

        SegWorkResult work;
        work.seg = RsObiaSegmentation::run( segCfg, [canceled]() {
            return canceled->load();
        } );

        if ( work.seg.ok && !canceled->load() )
        {
            work.stats = RsSegmentFeatures::extract( rasterPath, work.seg.segMap, bandIndices );
        }
        else if ( work.seg.ok && canceled->load() )
        {
            work.seg.ok = false;
            work.seg.errorMessage = QObject::tr( "Segmentation canceled" );
        }
        return work;
    } ) );
}

void RsObiaMainWindow::applySegmentationResult(
    const RsSegmentMap &segMap,
    bool usedOtb,
    QMap<quint32, RsSegmentFeatures::SegmentStat> stats )
{
    mSegMap = segMap;
    mSegStats = std::move( stats );
    mSegmentLabels.clear();

    if ( !mSelectTool )
    {
        mSelectTool = new RsSegmentSelectTool( mCanvas );
        connect( mSelectTool, &RsSegmentSelectTool::segmentSelected,
                 this, &RsObiaMainWindow::onSegmentSelected );
        connect( mSelectTool, &RsSegmentSelectTool::selectionCleared,
                 this, &RsObiaMainWindow::onSelectionCleared );
    }
    mSelectTool->setSegmentMap( mSegMap );

    double gt[6] = { 0, 1, 0, 0, 0, 1 };
    GDALDatasetH rds = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( rds )
    {
        GDALGetGeoTransform( rds, gt );
        GDALClose( rds );
    }
    mSelectTool->setGeoTransform( gt );
    mCanvas->setMapTool( mSelectTool );

    updateSegmentTable();
    updateStatusLabel();

    const QString method = usedOtb ? tr( "OTB MeanShift" ) : tr( "built-in segmenter" );
    statusBar()->showMessage(
        tr( "Segmentation complete (%1): %2 segments" ).arg( method ).arg( mSegMap.segmentCount() ), 5000 );

    QMessageBox::information( this, tr( "Segmentation" ),
                              tr( "Segmentation complete using %1: %2 segments" )
                                  .arg( method )
                                  .arg( mSegMap.segmentCount() ) );
}

void RsObiaMainWindow::runClassification()
{
    if ( mSegMap.isEmpty() || mSegStats.isEmpty() )
    {
        SICNU_LOG_WARN( SicnuLogTags::OBIA, "Classification requested without segmentation" );
        QMessageBox::information( this, tr( "OBIA" ), tr( "Run segmentation first." ) );
        return;
    }

    // Need at least 2 labeled segments
    if ( mSegmentLabels.size() < 2 )
    {
        SICNU_LOG_WARN( SicnuLogTags::OBIA, QString( "Classification: only %1 segments labeled (need >= 2)" )
            .arg( mSegmentLabels.size() ) );
        QMessageBox::information( this, tr( "OBIA" ),
                                  tr( "Label at least 2 segments (click segment → assign class)." ) );
        return;
    }

    // Get classifier from toolbar
    auto *combo = findChild<QComboBox *>( "classifierCombo" );
    QString algoName = combo ? combo->currentText() : "NormalBayes";

    SICNU_LOG_INFO( SicnuLogTags::OBIA, QString( "Starting OBIA classification: %1 labeled segments, algorithm=%2" )
        .arg( mSegmentLabels.size() ).arg( algoName ) );

    // Count unique user-labeled classes for KMeans K
    QSet<int> uniqueClasses;
    for ( auto it = mSegmentLabels.constBegin(); it != mSegmentLabels.constEnd(); ++it )
        uniqueClasses.insert( it.value() );

    std::unique_ptr<RsClassifierBackend> backend;
    if ( algoName == "SVM" )
        backend = std::make_unique<RsClassifierSvm>();
    else if ( algoName == "KMeans" )
        backend = std::make_unique<RsClassifierKMeans>( uniqueClasses.size() );
    else
        backend = std::make_unique<RsClassifierNormalBayes>();

    // Build band indices
    QVector<int> bandIndices;
    for ( int b = 1; b <= mBandCount; ++b )
        bandIndices.append( b );

    // Build class colors
    QHash<int, QColor> classColors;
    for ( const auto &cd : mClassDefs )
        classColors[cd.id] = cd.color;

    // Prompt for classified output path (no hard-coded default write)
    const QString defaultOut = QFileInfo( mRasterPath ).path() + QStringLiteral( "/obia_classified.tif" );
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr( "Save Classified Raster" ), defaultOut,
        tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
    if ( outputPath.isEmpty() )
        return;

    // Build config — pass existing segment map and features to avoid re-segmentation
    RsObiaTask::Config cfg;
    cfg.sourceRaster = mRasterPath;
    cfg.outputRaster = outputPath;
    cfg.bandIndices = bandIndices;
    cfg.existingSegMap = mSegMap;       // reuse pre-computed segments
    cfg.existingStats = mSegStats;     // reuse pre-computed features
    cfg.backend = std::move( backend );
    cfg.segmentLabels = mSegmentLabels;
    cfg.classColors = classColors;
    cfg.algoName = algoName;

    // Run asynchronously via QgsTaskManager
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );

    auto *task = new RsObiaTask( std::move( cfg ) );
    connect( task, &QgsTask::taskCompleted, this, [this, outputPath]() {
        QGuiApplication::restoreOverrideCursor();

        QMessageBox::information( this, tr( "OBIA Classification" ),
                                  tr( "Classification complete!\nOutput: %1" ).arg( outputPath ) );

        // Load result as layer
        auto *resultLayer = new QgsRasterLayer(
            outputPath, QFileInfo( outputPath ).baseName() );
        if ( resultLayer->isValid() )
        {
            QgsProject::instance()->addMapLayer( resultLayer );
            mCanvas->refresh();
        }
        else
        {
            delete resultLayer;
        }
    });

    connect( task, &QgsTask::taskTerminated, this, [this, task]() {
        QGuiApplication::restoreOverrideCursor();
        QMessageBox::warning( this, tr( "Error" ), task->result().errorMessage );
    });

    QgsApplication::taskManager()->addTask( task );
}

void RsObiaMainWindow::exportResult()
{
    if ( mRasterPath.isEmpty() )
    {
        QMessageBox::information( this, tr( "Export" ),
                                  tr( "No classification has been run yet. Use Classify and choose a save path." ) );
        return;
    }

    const QString defaultOut = QFileInfo( mRasterPath ).path() + QStringLiteral( "/obia_classified.tif" );
    QMessageBox::information( this, tr( "Export" ),
                              tr( "Use Classify to write the result. Suggested path:\n%1" )
                                  .arg( defaultOut ) );
}

void RsObiaMainWindow::onSegmentSelected( quint32 segmentId )
{
    auto it = mSegStats.find( segmentId );
    if ( it != mSegStats.end() )
    {
        int classId = mSegmentLabels.value( segmentId, 0 );
        mInfoDock->showSegmentInfo( segmentId, it.value(), classId );
    }

    // Use pixelCount (size cache) — do not call pixelCoords().size() which forces coord build.
    statusBar()->showMessage( tr( "Selected segment %1 (pixels: %2)" )
                                  .arg( segmentId )
                                  .arg( mSegMap.pixelCount( segmentId ) ) );
}

void RsObiaMainWindow::onSelectionCleared()
{
    mInfoDock->clearInfo();
    statusBar()->showMessage( tr( "Ready" ) );
}

void RsObiaMainWindow::onAssignClass()
{
    if ( mSelectTool && mSelectTool->selectedSegmentId() != 0 )
    {
        quint32 segId = mSelectTool->selectedSegmentId();
        mSegmentLabels[segId] = mCurrentClassId;
        updateSegmentTable();

        // Update info dock
        auto it = mSegStats.find( segId );
        if ( it != mSegStats.end() )
            mInfoDock->showSegmentInfo( segId, it.value(), mCurrentClassId );

        statusBar()->showMessage( tr( "Segment %1 → Class %2" ).arg( segId ).arg( mCurrentClassId ), 3000 );
    }
    else
    {
        QMessageBox::information( this, tr( "OBIA" ), tr( "Click a segment on the map first." ) );
    }
}

// ---------------------------------------------------------------------------
// UI updates
// ---------------------------------------------------------------------------

void RsObiaMainWindow::updateSegmentTable()
{
    auto segIds = mSegMap.uniqueLabels().values();
    std::sort( segIds.begin(), segIds.end() );

    mSegmentTable->setRowCount( segIds.size() );
    for ( int i = 0; i < segIds.size(); ++i )
    {
        quint32 segId = segIds[i];
        mSegmentTable->setItem( i, 0, new QTableWidgetItem( QString::number( segId ) ) );
        mSegmentTable->setItem( i, 1, new QTableWidgetItem( QString::number( mSegMap.pixelCount( segId ) ) ) );

        int classId = mSegmentLabels.value( segId, 0 );
        QString classText = classId > 0 ? QString::number( classId ) : tr( "-" );
        mSegmentTable->setItem( i, 2, new QTableWidgetItem( classText ) );
    }
}

void RsObiaMainWindow::updateStatusLabel()
{
    if ( !statusBar() )
        return;

    if ( mSegMap.isEmpty() )
        statusBar()->showMessage( tr( "Ready — Load a raster to begin" ) );
    else
        statusBar()->showMessage( tr( "%1 segments | %2 labeled" )
                                      .arg( mSegMap.segmentCount() )
                                      .arg( mSegmentLabels.size() ) );
}
