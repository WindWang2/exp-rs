// src/app/dialogs/change_detection_dialog.cpp
#include "change_detection_dialog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"
#include "processing/algorithms/change_detection.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>

#include <qgsproject.h>
#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_string.h>

#include "qgsogrutils.h"

#include <vector>
#include <cstddef>
#include <cstdint>

ChangeDetectionDialog::ChangeDetectionDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(tr("Change Detection"));
    setMinimumWidth(450);
    setupUi();
}

void ChangeDetectionDialog::setupUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
    }

    // --- Input group ---
    auto *inputGroup = new QGroupBox(tr("Input Images"), this);
    auto *formLayout = new QFormLayout(inputGroup);

    m_beforeLayerCombo = new QComboBox(this);
    m_afterLayerCombo = new QComboBox(this);
    m_beforeBandCombo = new QComboBox(this);
    m_afterBandCombo = new QComboBox(this);

    formLayout->addRow(tr("Before Image:"), m_beforeLayerCombo);
    formLayout->addRow(tr("Before Band:"), m_beforeBandCombo);
    formLayout->addRow(tr("After Image:"), m_afterLayerCombo);
    formLayout->addRow(tr("After Band:"), m_afterBandCombo);

    mainLayout->addWidget(inputGroup);

    // --- Method group ---
    auto *methodGroup = new QGroupBox(tr("Detection Method"), this);
    auto *methodLayout = new QFormLayout(methodGroup);

    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Difference"));
    m_methodCombo->addItem(tr("Normalized Difference"));
    m_methodCombo->addItem(tr("Change Mask"));

    m_thresholdLabel = new QLabel(tr("Threshold:"), this);
    m_thresholdSpin = new QDoubleSpinBox(this);
    m_thresholdSpin->setRange(0.0, 10000.0);
    m_thresholdSpin->setDecimals(2);
    m_thresholdSpin->setValue(10.0);
    m_thresholdSpin->setVisible(false);
    m_thresholdLabel->setVisible(false);

    methodLayout->addRow(tr("Method:"), m_methodCombo);
    methodLayout->addRow(m_thresholdLabel, m_thresholdSpin);

    mainLayout->addWidget(methodGroup);

    // --- Output section (using base class) ---
    setupOutputRow(mainLayout);

    // --- Status ---
    m_statusLabel = new QLabel(tr("Ready"), this);
    mainLayout->addWidget(m_statusLabel);

    // --- Buttons (using base class) ---
    setupButtonBar(mainLayout);

    // --- Connections ---
    connect(m_beforeLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChangeDetectionDialog::updateBandSelectors);
    connect(m_afterLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChangeDetectionDialog::updateBandSelectors);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChangeDetectionDialog::onMethodChanged);
}

void ChangeDetectionDialog::populateLayers()
{
    m_beforeLayerCombo->clear();
    m_afterLayerCombo->clear();

    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer *>(it.value());
        if (rasterLayer && rasterLayer->isValid()) {
            m_beforeLayerCombo->addItem(rasterLayer->name(), rasterLayer->id());
            m_afterLayerCombo->addItem(rasterLayer->name(), rasterLayer->id());
        }
    }

    updateBandSelectors();
}

void ChangeDetectionDialog::updateBandSelectors()
{
    // Update before band combo
    if (m_beforeLayerCombo->count() > 0) {
        QString beforeId = m_beforeLayerCombo->currentData().toString();
        QgsRasterLayer *beforeLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(beforeId);
        m_beforeBandCombo->clear();
        if (beforeLayer && beforeLayer->isValid()) {
            int bandCount = beforeLayer->bandCount();
            for (int i = 1; i <= bandCount; ++i) {
                m_beforeBandCombo->addItem(tr("Band %1").arg(i), i);
            }
        }
    } else {
        m_beforeBandCombo->clear();
    }

    // Update after band combo
    if (m_afterLayerCombo->count() > 0) {
        QString afterId = m_afterLayerCombo->currentData().toString();
        QgsRasterLayer *afterLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(afterId);
        m_afterBandCombo->clear();
        if (afterLayer && afterLayer->isValid()) {
            int bandCount = afterLayer->bandCount();
            for (int i = 1; i <= bandCount; ++i) {
                m_afterBandCombo->addItem(tr("Band %1").arg(i), i);
            }
        }
    } else {
        m_afterBandCombo->clear();
    }
}

void ChangeDetectionDialog::onMethodChanged(int index)
{
    bool isChangeMask = (index == 2);
    m_thresholdSpin->setVisible(isChangeMask);
    m_thresholdLabel->setVisible(isChangeMask);
}

void ChangeDetectionDialog::onRun()
{
    // Validate layer selection
    if (m_beforeLayerCombo->count() == 0 || m_afterLayerCombo->count() == 0) {
        QMessageBox::warning(this, tr("Change Detection"),
                             tr("Please ensure both before and after images are available."));
        return;
    }

    QString beforeId = m_beforeLayerCombo->currentData().toString();
    QString afterId = m_afterLayerCombo->currentData().toString();
    QgsRasterLayer *beforeLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(beforeId);
    QgsRasterLayer *afterLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(afterId);

    if (!beforeLayer || !beforeLayer->isValid()) {
        QMessageBox::warning(this, tr("Change Detection"),
                             tr("Invalid before image layer."));
        return;
    }
    if (!afterLayer || !afterLayer->isValid()) {
        QMessageBox::warning(this, tr("Change Detection"),
                             tr("Invalid after image layer."));
        return;
    }

    // Validate output path
    QString outPath = outputPath();
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, tr("Change Detection"),
                             tr("Please specify an output file."));
        return;
    }

    // Capture parameters for async execution
    QString beforeSourcePath = beforeLayer->source();
    QString afterSourcePath = afterLayer->source();
    int beforeBand = m_beforeBandCombo->currentData().toInt();
    int afterBand = m_afterBandCombo->currentData().toInt();
    int methodIndex = m_methodCombo->currentIndex();
    double threshold = m_thresholdSpin->value();

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &ChangeDetectionDialog::handleCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &ChangeDetectionDialog::handleFailed);
    }

    m_runButton->setEnabled(false);
    m_statusLabel->setText(tr("Processing..."));

    m_runner->run([this, beforeSourcePath, afterSourcePath, beforeBand, afterBand,
                   methodIndex, threshold, outPath]() -> QString {
    try {
        // Open source datasets
        GdalDatasetWrapper beforeDs;
        if (!beforeDs.open(beforeSourcePath)) return QString();

        GdalDatasetWrapper afterDs;
        if (!afterDs.open(afterSourcePath)) return QString();

        // Validate dimensions
        int width = beforeDs.width();
        int height = beforeDs.height();
        if (width != afterDs.width() || height != afterDs.height()) return QString();

        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

        // Read band data
        std::vector<float> beforeBuf(pixelCount);
        std::vector<float> afterBuf(pixelCount);
        if (!beforeDs.readBandData(beforeBand, beforeBuf.data(), width, height)) return QString();
        if (!afterDs.readBandData(afterBand, afterBuf.data(), width, height)) return QString();

        // Compute difference (needed for all methods)
        std::vector<float> diffBuf(pixelCount);
        if (!ChangeDetection::difference(beforeBuf.data(), afterBuf.data(),
                                          diffBuf.data(), pixelCount)) return QString();

        // Compute statistics on the difference result
        ChangeDetection::ChangeStats stats = ChangeDetection::statistics(diffBuf.data(), pixelCount);
        (void)stats;

        // Prepare output data and type based on method
        std::vector<float> ndviBuf;
        std::vector<uint8_t> maskBuf;
        GDALDataType outputType = GDT_Float32;
        void *outputData = diffBuf.data();

        if (methodIndex == 1) {
            // Normalized Difference
            ndviBuf.resize(pixelCount);
            if (!ChangeDetection::normalizedDifference(beforeBuf.data(), afterBuf.data(),
                                                        ndviBuf.data(), pixelCount)) return QString();
            outputData = ndviBuf.data();
            outputType = GDT_Float32;
        } else if (methodIndex == 2) {
            // Change Mask
            float thresh = static_cast<float>(threshold);
            maskBuf.resize(pixelCount);
            if (!ChangeDetection::changeMask(diffBuf.data(), maskBuf.data(),
                                              pixelCount, thresh)) return QString();
            outputData = maskBuf.data();
            outputType = GDT_Byte;
        }

        // Create output
        QString error;
        GdalDatasetGuard dstGuard(createOutputTiff(outPath, width, height, 1,
                                                   GDT_Float32, beforeDs.geoTransform(),
                                                   beforeDs.projection(), &error));
        if (!dstGuard) return QString();

        // Write output band
        GDALRasterBandH dstBand = GDALGetRasterBand(dstGuard.get(), 1);
        if (!dstBand) return QString();

        GDAL_SAFE_CALL( GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                        outputData, width, height, outputType, 0, 0),
                        "Failed to write output raster" );

        return outPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}
