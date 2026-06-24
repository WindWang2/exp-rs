// src/app/dialogs/atmospheric_dialog.cpp
#include "atmospheric_dialog.h"
#include "processing/algorithms/atmospheric_correction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>

AtmosphericDialog::AtmosphericDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void AtmosphericDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Method selection
    auto *methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:")));
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItems({tr("DN to Radiance"), tr("DOS1"), tr("DOS2")});
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AtmosphericDialog::onMethodChanged);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);

    // Band selection
    auto *bandLayout = new QHBoxLayout();
    bandLayout->addWidget(new QLabel(tr("Band:")));
    m_bandCombo = new QComboBox(this);
    bandLayout->addWidget(m_bandCombo);
    mainLayout->addLayout(bandLayout);

    // Gain and Bias
    auto *gainLayout = new QHBoxLayout();
    gainLayout->addWidget(new QLabel(tr("Gain:")));
    m_gainSpin = new QDoubleSpinBox(this);
    m_gainSpin->setRange(-999.0, 999.0);
    m_gainSpin->setDecimals(6);
    m_gainSpin->setValue(1.0);
    gainLayout->addWidget(m_gainSpin);
    gainLayout->addWidget(new QLabel(tr("Bias:")));
    m_biasSpin = new QDoubleSpinBox(this);
    m_biasSpin->setRange(-999.0, 999.0);
    m_biasSpin->setDecimals(6);
    m_biasSpin->setValue(0.0);
    gainLayout->addWidget(m_biasSpin);
    mainLayout->addLayout(gainLayout);

    // Airmass (for DOS2)
    auto *airmassLayout = new QHBoxLayout();
    m_airmassLabel = new QLabel(tr("Airmass:"), this);
    airmassLayout->addWidget(m_airmassLabel);
    m_airmassSpin = new QDoubleSpinBox(this);
    m_airmassSpin->setRange(1.0, 10.0);
    m_airmassSpin->setDecimals(2);
    m_airmassSpin->setValue(1.0);
    airmassLayout->addWidget(m_airmassSpin);
    airmassLayout->addStretch();
    mainLayout->addLayout(airmassLayout);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);

    // Initialize airmass visibility (hidden for DN to Radiance and DOS1)
    onMethodChanged(0);
}

void AtmosphericDialog::populateBandCombo()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    int bandCount = m_rasterLayer->bandCount();
    m_bandCombo->clear();

    for (int i = 1; i <= bandCount; ++i) {
        m_bandCombo->addItem(tr("Band %1").arg(i), i);
    }
}

void AtmosphericDialog::onMethodChanged(int index)
{
    // Show airmass field only for DOS2 (index 2)
    bool showAirmass = (index == 2);
    m_airmassSpin->setVisible(showAirmass);
    m_airmassLabel->setVisible(showAirmass);
}

void AtmosphericDialog::onRun()
{
    int bandNum = m_bandCombo->currentData().toInt();
    float gain = static_cast<float>(m_gainSpin->value());
    float bias = static_cast<float>(m_biasSpin->value());
    int method = m_methodCombo->currentIndex();

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->source())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Read band data
    std::vector<float> dn(pixelCount);
    if (!srcDataset.readBandData(bandNum, dn.data(), width, height)) {
        QgsMessageLog::logMessage(tr("Failed to read band %1").arg(bandNum),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    // Allocate output buffer
    std::vector<float> output(pixelCount);
    bool success = false;

    // Execute selected method
    switch (method) {
    case 0: // DN to Radiance
        success = AtmosphericCorrection::dnToRadiance(dn.data(), output.data(), pixelCount, gain, bias);
        break;
    case 1: // DOS1
        success = AtmosphericCorrection::dos1(dn.data(), output.data(), pixelCount, gain, bias);
        break;
    case 2: // DOS2
    {
        float airmass = static_cast<float>(m_airmassSpin->value());
        float transmittance = AtmosphericCorrection::estimateTransmittance(airmass);
        success = AtmosphericCorrection::dos2(dn.data(), output.data(), pixelCount, gain, bias, transmittance);
        break;
    }
    }

    if (!success) {
        QgsMessageLog::logMessage(tr("Failed to perform atmospheric correction."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    // Create output file using GDAL
    QString error;
    GdalDatasetGuard dstDataset(createOutputTiff(outputPath(), width, height, 1,
                                                  GDT_Float32, srcDataset.geoTransform(),
                                                  srcDataset.projection(), &error));
    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file: %1").arg(error),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    // Write output band
    GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset.get(), 1);
    if (!dstBand) {
        QgsMessageLog::logMessage(tr("Failed to get output band."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }
    CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                               output.data(), width, height, GDT_Float32, 0, 0);

    if (err != CE_None) {
        QgsMessageLog::logMessage(tr("Failed to write output file."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    handleCompleted(outputPath());
}
