// terrain_dialog.cpp — Phase 11.2
#include "terrain_dialog.h"
#include "dialog_utils.h"

#include "processing/algorithms/terrain_analysis.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QApplication>

#include <gdal.h>
#include <gdal_priv.h>
#include <QtConcurrent>
#include <cpl_error.h>
#include <cpl_conv.h>
#include <cpl_string.h>

#include "qgsogrutils.h"

TerrainDialog::TerrainDialog( QWidget *parent )
    : RasterProcessingDialogBase( parent )
{
    setWindowTitle( tr( "Terrain Analysis" ) );
    setMinimumWidth( 450 );
    setupUi();
}

void TerrainDialog::setupUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
    }

    // Input section
    auto *inputGroup = new QGroupBox( tr( "Input" ) );
    auto *inputLayout = new QFormLayout( inputGroup );

    mLayerCombo = new QComboBox;
    inputLayout->addRow( tr( "DEM Layer:" ), mLayerCombo );

    mAnalysisCombo = new QComboBox;
    mAnalysisCombo->addItem( tr( "Slope (degrees)" ), "slope" );
    mAnalysisCombo->addItem( tr( "Aspect (degrees)" ), "aspect" );
    mAnalysisCombo->addItem( tr( "Hillshade" ), "hillshade" );
    mAnalysisCombo->addItem( tr( "Roughness (max-min)" ), "roughness" );
    mAnalysisCombo->addItem( tr( "TRI (Terrain Ruggedness)" ), "tri" );
    mAnalysisCombo->addItem( tr( "TPI (Topographic Position)" ), "tpi" );
    inputLayout->addRow( tr( "Analysis:" ), mAnalysisCombo );

    mainLayout->addWidget( inputGroup );

    // Parameters section
    auto *paramGroup = new QGroupBox( tr( "Parameters" ) );
    auto *paramLayout = new QFormLayout( paramGroup );

    mCellSizeSpin = new QDoubleSpinBox;
    mCellSizeSpin->setRange( 0.001, 10000.0 );
    mCellSizeSpin->setValue( 1.0 );
    mCellSizeSpin->setToolTip( tr( "Cell size in map units (auto-detected from raster)" ) );
    paramLayout->addRow( tr( "Cell Size:" ), mCellSizeSpin );

    mSunAzimuthSpin = new QDoubleSpinBox;
    mSunAzimuthSpin->setRange( 0, 360 );
    mSunAzimuthSpin->setValue( 315.0 );
    mSunAzimuthSpin->setSuffix( "°" );
    mSunAzimuthSpin->setToolTip( tr( "Sun direction clockwise from north" ) );
    paramLayout->addRow( tr( "Sun Azimuth:" ), mSunAzimuthSpin );

    mSunElevationSpin = new QDoubleSpinBox;
    mSunElevationSpin->setRange( 0, 90 );
    mSunElevationSpin->setValue( 45.0 );
    mSunElevationSpin->setSuffix( "°" );
    mSunElevationSpin->setToolTip( tr( "Sun altitude above horizon" ) );
    paramLayout->addRow( tr( "Sun Elevation:" ), mSunElevationSpin );

    mainLayout->addWidget( paramGroup );

    // Output section (using base class)
    setupOutputRow(mainLayout);

    // Status
    mStatusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(mStatusLabel);

    // Buttons (using base class)
    setupButtonBar(mainLayout);

    // Populate layers
    populateRasterLayerCombo( mLayerCombo );

    // Auto-detect cell size from first layer
    connect( mLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             [this]( int idx ) {
                 if ( auto *rl = mLayerCombo->itemData( idx ).value<QgsRasterLayer *>() )
                 {
                     // Try to get pixel size from raster extent
                     auto extent = rl->extent();
                     if ( extent.width() > 0 && extent.height() > 0 && rl->width() > 0 )
                     {
                         double cs = extent.width() / rl->width();
                         mCellSizeSpin->setValue( cs );
                     }
                 }
             } );

    // Trigger initial cell size detection
    if ( mLayerCombo->count() > 0 )
        mLayerCombo->setCurrentIndex( 0 );
}

void TerrainDialog::onRun()
{
    auto *rl = mLayerCombo->currentData().value<QgsRasterLayer *>();
    if ( !rl )
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Select a DEM layer." ) );
        return;
    }

    QString outPath = outputPath();
    if ( outPath.isEmpty() )
    {
        // Auto-generate output path
        QString inputPath = rl->source();
        QString analysisType = mAnalysisCombo->currentData().toString();
        outPath = QFileInfo( inputPath ).path() + "/" + QFileInfo( inputPath ).baseName()
                     + "_" + analysisType + ".tif";
        m_outputEdit->setText( outPath );
    }

    // Disable run button during analysis
    m_runButton->setEnabled( false );
    mStatusLabel->setText(tr("Processing..."));
    QApplication::setOverrideCursor( Qt::WaitCursor );

    // Run analysis asynchronously using QtConcurrent
    if ( !mWatcher ) {
        mWatcher = new QFutureWatcher<bool>( this );
        connect( mWatcher, &QFutureWatcher<bool>::finished, this, &TerrainDialog::onAnalysisFinished );
    }

    // Capture parameters for the async task
    QString sourcePath = rl->source();
    QString analysisType = mAnalysisCombo->currentData().toString();
    float cellSize = static_cast<float>( mCellSizeSpin->value() );
    float sunAzimuth = static_cast<float>( mSunAzimuthSpin->value() );
    float sunElevation = static_cast<float>( mSunElevationSpin->value() );

    QFuture<bool> future = QtConcurrent::run( [sourcePath, outPath, analysisType, cellSize,
                                                sunAzimuth, sunElevation]() -> bool {
    try {
        // Open source raster
        gdal::dataset_unique_ptr ds( GDALOpen( sourcePath.toUtf8().constData(), GA_ReadOnly ) );
        if ( !ds ) return false;

        const int w = GDALGetRasterXSize( ds.get() );
        const int h = GDALGetRasterYSize( ds.get() );

        GDALRasterBandH band = GDALGetRasterBand( ds.get(), 1 );
        if ( !band ) return false;

        int hasNodata = 0;
        float nodata = static_cast<float>( GDALGetRasterNoDataValue( band, &hasNodata ) );
        if ( !hasNodata ) nodata = -9999.0f;

        // Read DEM data
        std::vector<float> dem( w * h );
        GDAL_SAFE_CALL( GDALRasterIO( band, GF_Read, 0, 0, w, h, dem.data(), w, h, GDT_Float32, 0, 0 ),
                        "Failed to read DEM data" );

        // Run analysis
        std::vector<float> out( w * h );
        bool ok = false;
        if ( analysisType == "slope" )
            ok = TerrainAnalysis::slope( dem.data(), out.data(), w, h, cellSize, nodata );
        else if ( analysisType == "aspect" )
            ok = TerrainAnalysis::aspect( dem.data(), out.data(), w, h, cellSize, nodata );
        else if ( analysisType == "hillshade" )
            ok = TerrainAnalysis::hillshade( dem.data(), out.data(), w, h, cellSize, nodata, sunAzimuth, sunElevation );
        else if ( analysisType == "roughness" )
            ok = TerrainAnalysis::roughness( dem.data(), out.data(), w, h, nodata );
        else if ( analysisType == "tri" )
            ok = TerrainAnalysis::tri( dem.data(), out.data(), w, h, nodata );
        else if ( analysisType == "tpi" )
            ok = TerrainAnalysis::tpi( dem.data(), out.data(), w, h, nodata );

        if ( !ok ) return false;

        // Write output using shared utility
        GeoInfo geo = extractGeoInfo( ds.get() );
        std::vector<std::vector<float>> bands = { out };
        QString error;
        if ( !writeGdalOutput( outPath, w, h, bands, geo.geoTransform, geo.projection, &error ) )
            return false;

        return true;
    } catch ( const std::runtime_error & ) {
        return false;
    }
    } );

    mWatcher->setFuture( future );
}

void TerrainDialog::onAnalysisFinished()
{
    QApplication::restoreOverrideCursor();
    m_runButton->setEnabled( true );

    if ( !mWatcher ) return;

    bool ok = mWatcher->result();
    if ( ok )
    {
        mStatusLabel->setText(tr("Completed!"));
        handleCompleted(outputPath());
    }
    else
    {
        mStatusLabel->setText(tr("Failed!"));
        handleFailed(tr("Terrain analysis failed."));
    }
}
