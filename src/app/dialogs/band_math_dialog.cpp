// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"
#include "processing/algorithms/band_math.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>

BandMathDialog::BandMathDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
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

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);
}

void BandMathDialog::onRun()
{
    // Validate expression
    QString expression = m_expressionEdit->text().trimmed();
    if (expression.isEmpty()) {
        QMessageBox::warning(this, tr("Band Math"), tr("Please enter an expression."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->source())) {
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
    QString error;
    GdalDatasetGuard dstDataset(createOutputTiff(outputPath(), width, height, 1,
                                                  GDT_Float32, srcDataset.geoTransform(),
                                                  srcDataset.projection(), &error));
    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file: %1").arg(error),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }

    // Write output band
    GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset.get(), 1);
    if (!dstBand) {
        QgsMessageLog::logMessage(tr("Failed to get output band."),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }
    CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                               output.data(), width, height, GDT_Float32, 0, 0);

    if (err != CE_None) {
        QgsMessageLog::logMessage(tr("Failed to write output file."),
                                  "band_math", Qgis::MessageLevel::Critical);
        return;
    }

    handleCompleted(outputPath());
}
