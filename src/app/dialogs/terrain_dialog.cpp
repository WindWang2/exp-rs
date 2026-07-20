// terrain_dialog.cpp — Phase 11.2
#include "terrain_dialog.h"
#include "dialog_utils.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFileInfo>

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
    
    setupHelpBanner(mainLayout);
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
        const QString inputPath = rl->source();
        const QString analysisType = mAnalysisCombo->currentData().toString();
        outPath = QFileInfo( inputPath ).path() + "/" + QFileInfo( inputPath ).baseName()
                     + "_" + analysisType + ".tif";
        m_outputEdit->setText( outPath );
    }

    if ( mStatusLabel )
        mStatusLabel->setText( tr( "Processing..." ) );

    Json::Value params( Json::objectValue );
    params["input"] = rl->source().toStdString();
    params["output"] = outPath.toStdString();
    params["product"] = mAnalysisCombo->currentData().toString().toStdString();
    params["cellSize"] = mCellSizeSpin->value();
    params["sunAzimuth"] = mSunAzimuthSpin->value();
    params["sunElevation"] = mSunElevationSpin->value();
    params["nodata"] = -9999.0;

    runOperatorTask( QStringLiteral( "rs:terrain_analysis" ), params );
}

void TerrainDialog::onAnalysisFinished()
{
    // Legacy slot kept for binary compatibility of any external connections;
    // terrain now uses runOperatorTask / base-class completion handlers.
    if ( mStatusLabel )
        mStatusLabel->setText( tr( "Ready" ) );
}
