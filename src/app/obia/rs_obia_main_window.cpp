// rs_obia_main_window.cpp — Phase 10B Task 10B.5
#include "rs_obia_main_window.h"
#include "sicnu_logging.h"

#include "rs_obia_task.h"
#include "rs_simple_segmenter.h"
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
    binsSpin->setToolTip( tr( "Quantization bins" ) );
    binsSpin->setObjectName( "binsSpin" );
    mToolbar->addWidget( binsSpin );

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

    // Build band indices (use all bands)
    QVector<int> bandIndices;
    for ( int b = 1; b <= mBandCount; ++b )
        bandIndices.append( b );

    // Get params from toolbar
    auto *kernelSpin = findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = findChild<QSpinBox *>( "binsSpin" );

    RsSimpleSegmenter::Params params;
    params.smoothKernel = kernelSpin ? kernelSpin->value() : 5;
    params.quantizeBins = binsSpin ? binsSpin->value() : 32;
    params.minRegionSize = 50;

    // Read band data via GDAL
    GDALDatasetH ds = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Cannot open raster for segmentation" ) );
        return;
    }

    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );

    QVector<QVector<float>> bandData( mBandCount );
    bool readOk = true;
    for ( int b = 0; b < mBandCount; ++b )
    {
        bandData[b].resize( w * h );
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        if ( !band )
        {
            readOk = false;
            break;
        }
        if ( GDALRasterIO( band, GF_Read, 0, 0, w, h, bandData[b].data(), w, h, GDT_Float32, 0, 0 ) != CE_None )
        {
            readOk = false;
            break;
        }
    }

    GDALRasterBandH firstBand = GDALGetRasterBand( ds, 1 );
    int hasNodata = 0;
    float nodata = firstBand ? static_cast<float>( GDALGetRasterNoDataValue( firstBand, &hasNodata ) ) : -9999.0f;
    GDALClose( ds );

    if ( !readOk )
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Failed to read raster band data" ) );
        return;
    }

    if ( !hasNodata )
        nodata = -9999.0f;

    // Build band pointers (must stay alive during async execution)
    auto sharedBandData = std::make_shared<QVector<QVector<float>>>( std::move(bandData) );
    QVector<const float *> bandPtrs( mBandCount );
    for ( int b = 0; b < mBandCount; ++b )
        bandPtrs[b] = (*sharedBandData)[b].data();

    // Segment asynchronously using QtConcurrent
    QGuiApplication::setOverrideCursor( Qt::WaitCursor );

    // Capture values for lambda
    int capturedBandCount = mBandCount;
    QVector<int> capturedBandIndices = bandIndices;

    QFutureWatcher<RsSegmentMap> *watcher = new QFutureWatcher<RsSegmentMap>( this );
    connect( watcher, &QFutureWatcher<RsSegmentMap>::finished, this, [this, watcher, sharedBandData, bandPtrs, capturedBandCount, capturedBandIndices]() {
        QGuiApplication::restoreOverrideCursor();
        mSegMap = watcher->result();
        watcher->deleteLater();

        if ( mSegMap.isEmpty() )
        {
            QMessageBox::warning( this, tr( "Error" ), tr( "Segmentation produced no segments" ) );
            return;
        }

        // Update segment map and features
        mSegStats = RsSegmentFeatures::extract( mRasterPath, mSegMap, capturedBandIndices );
        mSegmentLabels.clear();

        QMessageBox::information( this, tr( "Segmentation" ),
                                  tr( "Segmentation complete: %1 segments" ).arg( mSegMap.uniqueLabels().size() ) );
    });

    // Capture for async execution
    auto segFuture = QtConcurrent::run( [bandPtrs, capturedBandCount, w, h, nodata, params]() -> RsSegmentMap {
        return RsSimpleSegmenter::segmentMultiBand( bandPtrs.constData(), capturedBandCount, w, h, nodata, params );
    } );
    watcher->setFuture( segFuture );

    if ( mSegMap.isEmpty() )
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Segmentation produced no segments" ) );
        return;
    }

    // Extract features
    mSegStats = RsSegmentFeatures::extract( mRasterPath, mSegMap, bandIndices );

    // Set up select tool
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
    if ( mRasterLayer )
    {
        auto *rds = GDALOpen( mRasterPath.toUtf8().constData(), GA_ReadOnly );
        if ( rds )
        {
            GDALGetGeoTransform( rds, gt );
            GDALClose( rds );
        }
    }
    mSelectTool->setGeoTransform( gt );

    mCanvas->setMapTool( mSelectTool );

    // Update UI
    mSegmentLabels.clear();
    updateSegmentTable();
    updateStatusLabel();

    statusBar()->showMessage( tr( "Segmentation complete: %1 segments" ).arg( mSegMap.segmentCount() ), 5000 );
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

    // Build config — pass existing segment map and features to avoid re-segmentation
    RsObiaTask::Config cfg;
    cfg.sourceRaster = mRasterPath;
    cfg.outputRaster = QFileInfo( mRasterPath ).path() + "/obia_classified.tif";
    cfg.bandIndices = bandIndices;
    cfg.existingSegMap = mSegMap;       // reuse pre-computed segments
    cfg.existingStats = mSegStats;     // reuse pre-computed features
    cfg.backend = std::move( backend );
    cfg.segmentLabels = mSegmentLabels;
    cfg.classColors = classColors;
    cfg.algoName = algoName;

    // Capture output path before std::move(cfg)
    const QString outputPath = cfg.outputRaster;

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
    QMessageBox::information( this, tr( "Export" ),
                              tr( "Classification output is saved automatically to:\n%1" )
                                  .arg( QFileInfo( mRasterPath ).path() + "/obia_classified.tif" ) );
}

void RsObiaMainWindow::onSegmentSelected( quint32 segmentId )
{
    auto it = mSegStats.find( segmentId );
    if ( it != mSegStats.end() )
    {
        int classId = mSegmentLabels.value( segmentId, 0 );
        mInfoDock->showSegmentInfo( segmentId, it.value(), classId );
    }

    statusBar()->showMessage( tr( "Selected segment %1 (pixels: %2)" )
                                  .arg( segmentId )
                                  .arg( mSegMap.pixelCoords( segmentId ).size() ) );
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
