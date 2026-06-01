// src/app/dialogs/atmospheric_dialog.cpp
#include "atmospheric_dialog.h"
#include "processing/algorithms/atmospheric_correction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>

AtmosphericDialog::AtmosphericDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Atmospheric Correction"));
    setupUi();
}

void AtmosphericDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
    populateBandCombo();
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

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &AtmosphericDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &AtmosphericDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

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

void AtmosphericDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void AtmosphericDialog::onRun()
{
    // Validate inputs
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Atmospheric Correction"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("Atmospheric Correction"), tr("No valid raster layer selected."));
        return;
    }

    int bandNum = m_bandCombo->currentData().toInt();
    float gain = static_cast<float>(m_gainSpin->value());
    float bias = static_cast<float>(m_biasSpin->value());
    int method = m_methodCombo->currentIndex();

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
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
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        QgsMessageLog::logMessage(tr("Failed to get GeoTIFF driver."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    GDALDatasetH dstDataset = GDALCreate(driver, outputPath.toUtf8().constData(),
                                          width, height, 1, GDT_Float32, options);
    CSLDestroy(options);

    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    // Copy geotransform and projection from source
    std::array<double, 6> gt = srcDataset.geoTransform();
    GDALSetGeoTransform(dstDataset, gt.data());
    GDALSetProjection(dstDataset, srcDataset.projection().toUtf8().constData());

    // Write output band
    GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset, 1);
    if (!dstBand) {
        QgsMessageLog::logMessage(tr("Failed to get output band."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        GDALClose(dstDataset);
        return;
    }
    CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                               output.data(), width, height, GDT_Float32, 0, 0);

    GDALClose(dstDataset);

    if (err != CE_None) {
        QgsMessageLog::logMessage(tr("Failed to write output file."),
                                  "atmospheric", Qgis::MessageLevel::Critical);
        return;
    }

    QgsMessageLog::logMessage(tr("Atmospheric correction completed successfully! Output: %1").arg(outputPath),
                              "atmospheric", Qgis::MessageLevel::Success);
    accept();
}
