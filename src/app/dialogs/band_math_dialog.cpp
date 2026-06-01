// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"
#include "processing/algorithms/band_math.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>

BandMathDialog::BandMathDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Band Math"));
    setupUi();
}

void BandMathDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

void BandMathDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Expression
    auto *exprLayout = new QHBoxLayout();
    exprLayout->addWidget(new QLabel(tr("Expression:")));
    m_expressionEdit = new QLineEdit(this);
    m_expressionEdit->setPlaceholderText(tr("e.g., (b1 - b2) / (b1 + b2)"));
    exprLayout->addWidget(m_expressionEdit);
    mainLayout->addLayout(exprLayout);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &BandMathDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &BandMathDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void BandMathDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void BandMathDialog::onRun()
{
    // Validate inputs
    QString expression = m_expressionEdit->text().trimmed();
    if (expression.isEmpty()) {
        QMessageBox::warning(this, tr("Band Math"), tr("Please enter an expression."));
        return;
    }

    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Band Math"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("Band Math"), tr("No valid raster layer selected."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    int bandCount = srcDataset.bandCount();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Read all bands into BandData
    BandMath::BandData bands;
    for (int i = 1; i <= bandCount; ++i) {
        std::vector<float> buffer(pixelCount);
        if (!srcDataset.readBandData(i, buffer.data(), width, height)) {
            QgsMessageLog::logMessage(tr("Failed to read band %1").arg(i),
                                      "band_math", Qgis::MessageLevel::Critical);
            return;
        }
        bands[i] = std::move(buffer);
    }

    // Allocate output buffer
    std::vector<float> output(pixelCount);

    // Evaluate expression
    if (!BandMath::evaluate(expression, bands, output.data(), pixelCount)) {
        QgsMessageLog::logMessage(tr("Failed to evaluate expression. Check syntax (use b1, b2, ... for bands)."),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }

    // Create output file using GDAL
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        QgsMessageLog::logMessage(tr("Failed to get GeoTIFF driver."),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    GDALDatasetH dstDataset = GDALCreate(driver, outputPath.toUtf8().constData(),
                                          width, height, 1, GDT_Float32, options);
    CSLDestroy(options);

    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file."),
                                  "band_math", Qgis::MessageLevel::Critical);
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
                                  "band_math", Qgis::MessageLevel::Critical);
        GDALClose(dstDataset);
        return;
    }
    CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                               output.data(), width, height, GDT_Float32, 0, 0);

    GDALClose(dstDataset);

    if (err != CE_None) {
        QgsMessageLog::logMessage(tr("Failed to write output file."),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }

    QgsMessageLog::logMessage(tr("Band Math completed successfully! Output: %1").arg(outputPath),
                              "band_math", Qgis::MessageLevel::Success);
    accept();
}
