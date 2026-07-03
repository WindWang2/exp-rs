// extract_band_dialog.cpp — Extract single band from multi-band raster
#include "extract_band_dialog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>

#include "processing/gdal/gdal_safe_call.h"

ExtractBandDialog::ExtractBandDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(tr("Extract Band"));
    setupUi();
}

void ExtractBandDialog::setupUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
    }

    // Input section
    auto *inputGroup = new QGroupBox(tr("Input"));
    auto *inputLayout = new QFormLayout(inputGroup);

    m_layerCombo = new QComboBox;
    m_layerCombo->setToolTip(tr("Select a multi-band raster layer"));
    inputLayout->addRow(tr("Raster Layer:"), m_layerCombo);

    m_bandCombo = new QComboBox;
    m_bandCombo->setToolTip(tr("Select the band to extract"));
    inputLayout->addRow(tr("Band:"), m_bandCombo);

    mainLayout->addWidget(inputGroup);

    // Output section (using base class)
    setupOutputRow(mainLayout);

    // Buttons (using base class)
    setupButtonBar(mainLayout);

    // Connections
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExtractBandDialog::onLayerChanged);

    // Populate layers (only multi-band)
    populateRasterLayerCombo(m_layerCombo);
    for (int i = m_layerCombo->count() - 1; i >= 0; --i) {
        auto *rl = m_layerCombo->itemData(i).value<QgsRasterLayer *>();
        if (!rl || rl->bandCount() <= 1) {
            m_layerCombo->removeItem(i);
        }
    }

    // Populate bands for first layer
    if (m_layerCombo->count() > 0) {
        populateBandCombo();
    }
}

void ExtractBandDialog::populateBandCombo()
{
    m_bandCombo->clear();

    auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
    if (!rl) return;

    for (int i = 1; i <= rl->bandCount(); ++i) {
        QString bandName = rl->bandName(i);
        if (bandName.isEmpty()) {
            bandName = tr("Band %1").arg(i);
        }
        m_bandCombo->addItem(bandName, i);
    }
}

void ExtractBandDialog::onRun()
{
    auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
    if (!rl) {
        QMessageBox::warning(this, tr("Error"), tr("Select a raster layer."));
        return;
    }

    int bandIndex = m_bandCombo->currentData().toInt();
    if (bandIndex < 1) {
        QMessageBox::warning(this, tr("Error"), tr("Select a band to extract."));
        return;
    }

    QString outPath = outputPath();
    if (outPath.isEmpty()) {
        // Auto-generate output path
        QString inputPath = rl->source();
        outPath = QFileInfo(inputPath).path() + "/" + QFileInfo(inputPath).baseName()
                     + tr("_band%1.tif").arg(bandIndex);
        m_outputEdit->setText(outPath);
    }

    // Capture parameters for async execution
    QString sourcePath = rl->source();

    runGdalTask([sourcePath, bandIndex, outPath]() -> QString {
    try {
        // Open source raster
        GdalDatasetGuard srcGuard( GDALOpen(sourcePath.toUtf8().constData(), GA_ReadOnly) );
        if (!srcGuard) return QString();

        int w = GDALGetRasterXSize(srcGuard.get());
        int h = GDALGetRasterYSize(srcGuard.get());

        GDALRasterBandH srcBand = GDALGetRasterBand(srcGuard.get(), bandIndex);
        if (!srcBand) return QString();

        // Read band data
        std::vector<float> buf(static_cast<size_t>(w) * h);
        GDAL_SAFE_CALL( GDALRasterIO(srcBand, GF_Read, 0, 0, w, h, buf.data(), w, h, GDT_Float32, 0, 0),
                        "Failed to read band data" );

        // Write output using shared utility
        GeoInfo geo = extractGeoInfo(srcGuard.get());
        std::vector<std::vector<float>> bands = { buf };
        QString error;
        if (!writeGdalOutput(outPath, w, h, bands, geo.geoTransform, geo.projection, &error))
            return QString();

        return outPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}

void ExtractBandDialog::onLayerChanged()
{
    populateBandCombo();
}
