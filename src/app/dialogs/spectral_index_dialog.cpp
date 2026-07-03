// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsmessagelog.h>
#include <qgsogrutils.h>
#include <qgis.h>

#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

#include <gdal.h>
#include <cpl_error.h>

#include "processing/gdal/gdal_safe_call.h"

#include <vector>

SpectralIndexDialog::SpectralIndexDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void SpectralIndexDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Index selection
    auto *idxLayout = new QHBoxLayout();
    idxLayout->addWidget(new QLabel(tr("Index:")));
    m_indexCombo = new QComboBox(this);
    m_indexCombo->addItems({tr("NDVI"), tr("EVI"), tr("SAVI"), tr("NDWI"), tr("NDBI"), tr("MNDWI")});
    connect(m_indexCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpectralIndexDialog::onIndexChanged);
    idxLayout->addWidget(m_indexCombo);
    mainLayout->addLayout(idxLayout);

    // Band selectors - Row 1
    auto *bandLayout = new QHBoxLayout();
    m_nirLabel = new QLabel(tr("NIR:"), this);
    bandLayout->addWidget(m_nirLabel);
    m_nirCombo = new QComboBox(this);
    bandLayout->addWidget(m_nirCombo);
    m_redLabel = new QLabel(tr("Red:"), this);
    bandLayout->addWidget(m_redLabel);
    m_redCombo = new QComboBox(this);
    bandLayout->addWidget(m_redCombo);
    mainLayout->addLayout(bandLayout);

    // Band selectors - Row 2
    auto *bandLayout2 = new QHBoxLayout();
    m_greenLabel = new QLabel(tr("Green:"), this);
    bandLayout2->addWidget(m_greenLabel);
    m_greenCombo = new QComboBox(this);
    bandLayout2->addWidget(m_greenCombo);
    m_blueLabel = new QLabel(tr("Blue:"), this);
    bandLayout2->addWidget(m_blueLabel);
    m_blueCombo = new QComboBox(this);
    bandLayout2->addWidget(m_blueCombo);
    m_swirLabel = new QLabel(tr("SWIR:"), this);
    bandLayout2->addWidget(m_swirLabel);
    m_swirCombo = new QComboBox(this);
    bandLayout2->addWidget(m_swirCombo);
    mainLayout->addLayout(bandLayout2);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);

    // Initialize band visibility
    updateBandVisibility();
}

void SpectralIndexDialog::populateBandCombos()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    int bandCount = m_rasterLayer->bandCount();

    // Clear existing items
    m_nirCombo->clear();
    m_redCombo->clear();
    m_greenCombo->clear();
    m_blueCombo->clear();
    m_swirCombo->clear();

    // Populate with band numbers
    for (int i = 1; i <= bandCount; ++i) {
        QString bandName = tr("Band %1").arg(i);
        m_nirCombo->addItem(bandName, i);
        m_redCombo->addItem(bandName, i);
        m_greenCombo->addItem(bandName, i);
        m_blueCombo->addItem(bandName, i);
        m_swirCombo->addItem(bandName, i);
    }

    // Set default band mappings (typical for Landsat/Sentinel-2)
    if (bandCount >= 4) {
        m_nirCombo->setCurrentIndex(3);  // Band 4 = NIR
        m_redCombo->setCurrentIndex(2);  // Band 3 = Red
        m_greenCombo->setCurrentIndex(1); // Band 2 = Green
        m_blueCombo->setCurrentIndex(0); // Band 1 = Blue
    }
    if (bandCount >= 5) {
        m_swirCombo->setCurrentIndex(4); // Band 5 = SWIR
    }
}

void SpectralIndexDialog::updateBandVisibility()
{
    int index = m_indexCombo->currentIndex();

    // Show/hide band selectors based on selected index
    // NDVI: NIR, Red
    // EVI: NIR, Red, Blue
    // SAVI: NIR, Red
    // NDWI: Green, NIR
    // NDBI: SWIR, NIR
    // MNDWI: Green, SWIR

    m_nirCombo->setVisible(true);
    m_nirLabel->setVisible(true);

    m_redCombo->setVisible(index == 0 || index == 1 || index == 2); // NDVI, EVI, SAVI
    m_redLabel->setVisible(index == 0 || index == 1 || index == 2);

    m_greenCombo->setVisible(index == 3 || index == 5); // NDWI, MNDWI
    m_greenLabel->setVisible(index == 3 || index == 5);

    m_blueCombo->setVisible(index == 1); // EVI only
    m_blueLabel->setVisible(index == 1);

    m_swirCombo->setVisible(index == 4 || index == 5); // NDBI, MNDWI
    m_swirLabel->setVisible(index == 4 || index == 5);
}

void SpectralIndexDialog::onIndexChanged(int index)
{
    updateBandVisibility();
}

/**
 * Extract a single band from a multi-band raster into a temporary single-band
 * GeoTIFF file and return a QgsRasterLayer wrapping it.  The caller takes
 * ownership of the returned layer.  Returns nullptr on failure.
 *
 * This bridges the dialog UI (which lets the user pick band numbers from a
 * multi-band layer) with the SpectralIndexAlgorithm API (which expects
 * separate single-band QgsRasterLayer objects).
 */
static QgsRasterLayer *extractBandAsLayer( QgsRasterLayer *srcLayer, int bandNum,
                                           const QString &tag, QStringList &tempFiles )
{
    if ( !srcLayer || !srcLayer->isValid() || bandNum <= 0 )
        return nullptr;

    gdal::dataset_unique_ptr srcDS( GDALOpen(
        srcLayer->source().toUtf8().constData(), GA_ReadOnly ) );
    if ( !srcDS )
        return nullptr;

    int width  = GDALGetRasterXSize( srcDS.get() );
    int height = GDALGetRasterYSize( srcDS.get() );

    QString tempPath = QDir::tempPath()
        + QStringLiteral( "/sicnu_%1_band%2_%3.tif" )
              .arg( tag )
              .arg( bandNum )
              .arg( QDateTime::currentMSecsSinceEpoch() );

    // Create output file
    double gt[6];
    GDALGetGeoTransform( srcDS.get(), gt );
    std::array<double, 6> geoTransform;
    std::copy(std::begin(gt), std::end(gt), geoTransform.begin());
    const char *proj = GDALGetProjectionRef( srcDS.get() );
    QString projection = proj ? QString::fromUtf8(proj) : QString();

    QString error;
    GdalDatasetGuard dstDS(createOutputTiff(tempPath, width, height, 1,
                                           GDT_Float32, geoTransform, projection, &error));
    if ( !dstDS ) { return nullptr; }

    // Copy pixel data for the requested band
    std::vector<float> buf( static_cast<size_t>( width ) * height );
    try {
        GDALRasterBandH srcBand = GDALGetRasterBand( srcDS.get(), bandNum );
        if ( !srcBand ) {
            QFile::remove( tempPath );
            return nullptr;
        }
        GDAL_SAFE_CALL( GDALRasterIO( srcBand, GF_Read, 0, 0, width, height,
                        buf.data(), width, height, GDT_Float32, 0, 0 ),
                        "Failed to read source band data" );
        GDALRasterBandH dstBand = GDALGetRasterBand( dstDS.get(), 1 );
        if ( !dstBand ) {
            QFile::remove( tempPath );
            return nullptr;
        }
        GDAL_SAFE_CALL( GDALRasterIO( dstBand, GF_Write, 0, 0, width, height,
                        buf.data(), width, height, GDT_Float32, 0, 0 ),
                        "Failed to write band data to temp file" );
    } catch ( const std::runtime_error & ) {
        QFile::remove( tempPath );
        return nullptr;
    }

    tempFiles.append( tempPath );

    return new QgsRasterLayer( tempPath,
                               QStringLiteral( "%1_band%2" ).arg( tag ).arg( bandNum ) );
}

void SpectralIndexDialog::onRun()
{
    // Look up the processing algorithm from the registry
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(
        QStringLiteral( "qgis_algorithms:rs_spectral_index" ) );
    if ( !alg ) {
        QMessageBox::critical( this, tr( "Spectral Index" ),
                               tr( "Processing algorithm not found: rs_spectral_index" ) );
        return;
    }

    cleanupRunResources();
    m_tempFiles.clear();
    m_tempLayers.clear();

    // Extract each visible band selection into a temporary single-band layer.
    // The algorithm expects separate QgsRasterLayer objects, each containing
    // exactly the band to be used.  Temporary files are cleaned up after run.
    int index = m_indexCombo->currentIndex();
    std::unique_ptr<QgsRasterLayer> nirLayer, redLayer, greenLayer, blueLayer, swirLayer;

    if ( m_nirCombo->isVisible() )
        nirLayer.reset( extractBandAsLayer( m_rasterLayer,
                        m_nirCombo->currentData().toInt(), QStringLiteral( "nir" ), m_tempFiles ) );
    if ( m_redCombo->isVisible() )
        redLayer.reset( extractBandAsLayer( m_rasterLayer,
                        m_redCombo->currentData().toInt(), QStringLiteral( "red" ), m_tempFiles ) );
    if ( m_greenCombo->isVisible() )
        greenLayer.reset( extractBandAsLayer( m_rasterLayer,
                          m_greenCombo->currentData().toInt(), QStringLiteral( "green" ), m_tempFiles ) );
    if ( m_blueCombo->isVisible() )
        blueLayer.reset( extractBandAsLayer( m_rasterLayer,
                         m_blueCombo->currentData().toInt(), QStringLiteral( "blue" ), m_tempFiles ) );
    if ( m_swirCombo->isVisible() )
        swirLayer.reset( extractBandAsLayer( m_rasterLayer,
                         m_swirCombo->currentData().toInt(), QStringLiteral( "swir" ), m_tempFiles ) );

    auto keepLayer = [this](std::unique_ptr<QgsRasterLayer> &layer) -> QgsRasterLayer * {
        if (!layer)
            return nullptr;
        m_tempLayers.push_back(std::move(layer));
        return m_tempLayers.back().get();
    };

    // Build parameters from dialog inputs
    QVariantMap params;
    params.insert( QStringLiteral( "INDEX" ), index );
    if ( QgsRasterLayer *nir = keepLayer(nirLayer) )
        params.insert( QStringLiteral( "NIR_BAND" ), QVariant::fromValue( nir ) );
    if ( QgsRasterLayer *red = keepLayer(redLayer) )
        params.insert( QStringLiteral( "RED_BAND" ), QVariant::fromValue( red ) );
    if ( QgsRasterLayer *green = keepLayer(greenLayer) )
        params.insert( QStringLiteral( "GREEN_BAND" ), QVariant::fromValue( green ) );
    if ( QgsRasterLayer *blue = keepLayer(blueLayer) )
        params.insert( QStringLiteral( "BLUE_BAND" ), QVariant::fromValue( blue ) );
    if ( QgsRasterLayer *swir = keepLayer(swirLayer) )
        params.insert( QStringLiteral( "SWIR_BAND" ), QVariant::fromValue( swir ) );
    params.insert( QStringLiteral( "OUTPUT" ), outputPath() );

    QgsProcessingContext context;
    context.setProject( QgsProject::instance() );

    runAlgorithmTask(alg, params, context);
}

void SpectralIndexDialog::cleanupRunResources()
{
    m_tempLayers.clear();
    for (const QString &path : std::as_const(m_tempFiles))
        QFile::remove(path);
    m_tempFiles.clear();
}
