// terrain_dialog.cpp — Phase 11.2
#include "terrain_dialog.h"

#include "processing/algorithms/terrain_analysis.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
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
    : QDialog( parent )
{
    setWindowTitle( tr( "Terrain Analysis" ) );
    setMinimumWidth( 450 );

    auto *mainLayout = new QVBoxLayout( this );

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

    // Output section
    auto *outputGroup = new QGroupBox( tr( "Output" ) );
    auto *outputLayout = new QFormLayout( outputGroup );

    auto *outputRow = new QWidget;
    auto *outputRowLayout = new QHBoxLayout( outputRow );
    outputRowLayout->setContentsMargins( 0, 0, 0, 0 );
    mOutputEdit = new QLineEdit;
    mOutputEdit->setPlaceholderText( tr( "Output raster path" ) );
    auto *browseBtn = new QPushButton( tr( "Browse..." ) );
    outputRowLayout->addWidget( mOutputEdit );
    outputRowLayout->addWidget( browseBtn );
    outputLayout->addRow( tr( "Output File:" ), outputRow );

    mainLayout->addWidget( outputGroup );

    // Buttons
    auto *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
    mRunButton = buttons->button( QDialogButtonBox::Ok );
    mRunButton->setText( tr( "Run" ) );
    mainLayout->addWidget( buttons );

    // Connections
    connect( browseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName( this, tr( "Save Output" ), QString(),
                                                     tr( "GeoTIFF (*.tif)" ) );
        if ( !path.isEmpty() )
            mOutputEdit->setText( path );
    } );

    connect( buttons, &QDialogButtonBox::accepted, this, &TerrainDialog::runAnalysis );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

    // Populate layers
    const auto layers = QgsProject::instance()->mapLayers().values();
    for ( auto *layer : layers )
    {
        if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
        {
            mLayerCombo->addItem( rl->name(), QVariant::fromValue( rl ) );
        }
    }

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

void TerrainDialog::setRasterLayer( QgsRasterLayer *layer )
{
    if ( !layer )
        return;
    for ( int i = 0; i < mLayerCombo->count(); ++i )
    {
        if ( mLayerCombo->itemData( i ).value<QgsRasterLayer *>() == layer )
        {
            mLayerCombo->setCurrentIndex( i );
            return;
        }
    }
}

void TerrainDialog::runAnalysis()
{
    auto *rl = mLayerCombo->currentData().value<QgsRasterLayer *>();
    if ( !rl )
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Select a DEM layer." ) );
        return;
    }

    QString outputPath = mOutputEdit->text().trimmed();
    if ( outputPath.isEmpty() )
    {
        // Auto-generate output path
        QString inputPath = rl->source();
        QString analysisType = mAnalysisCombo->currentData().toString();
        outputPath = QFileInfo( inputPath ).path() + "/" + QFileInfo( inputPath ).baseName()
                     + "_" + analysisType + ".tif";
        mOutputEdit->setText( outputPath );
    }

    mOutputPath = outputPath;

    // Disable run button during analysis
    if ( mRunButton ) mRunButton->setEnabled( false );
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

    QFuture<bool> future = QtConcurrent::run( [sourcePath, outputPath, analysisType, cellSize,
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

        // Write output
        double gt[6] = { 0, 1, 0, 0, 0, 1 };
        GDALGetGeoTransform( ds.get(), gt );
        std::array<double, 6> geoTransform;
        std::copy(std::begin(gt), std::end(gt), geoTransform.begin());
        const char *proj = GDALGetProjectionRef( ds.get() );
        QString projection = proj ? QString::fromUtf8(proj) : QString();

        QString error;
        GdalDatasetGuard outDs(createOutputTiff(outputPath, w, h, 1,
                                               GDT_Float32, geoTransform, projection, &error));
        if ( !outDs ) return false;

        GDALRasterBandH outBand = GDALGetRasterBand( outDs.get(), 1 );
        if ( !outBand ) return false;
        GDALSetRasterNoDataValue( outBand, nodata );

        for ( int r = 0; r < h; ++r )
        {
            GDAL_SAFE_CALL( GDALRasterIO( outBand, GF_Write, 0, r, w, 1,
                            out.data() + r * w, w, 1, GDT_Float32, 0, 0 ),
                            "Failed to write output raster" );
        }

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
    if ( mRunButton ) mRunButton->setEnabled( true );

    if ( !mWatcher ) return;

    bool ok = mWatcher->result();
    if ( ok )
    {
        QMessageBox::information( this, tr( "Terrain Analysis" ),
                                  tr( "Analysis complete!\nOutput: %1" ).arg( mOutputPath ) );
        accept();
    }
    else
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Terrain analysis failed." ) );
    }
}
