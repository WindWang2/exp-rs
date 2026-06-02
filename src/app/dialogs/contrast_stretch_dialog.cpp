// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QMessageBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>

ContrastStretchDialog::ContrastStretchDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Contrast Stretch"));
    setupUi();
}

void ContrastStretchDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

void ContrastStretchDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Method selection
    auto *methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:"), this));
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItems({tr("Linear"), tr("Percentage Clip"),
                             tr("Std Dev"), tr("Histogram Equalization")});
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContrastStretchDialog::onMethodChanged);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);

    // Clip percentage parameter
    auto *clipLayout = new QHBoxLayout();
    m_clipLabel = new QLabel(tr("Clip %:"), this);
    clipLayout->addWidget(m_clipLabel);
    m_clipSpin = new QDoubleSpinBox(this);
    m_clipSpin->setRange(0.1, 50.0);
    m_clipSpin->setValue(2.0);
    m_clipSpin->setSingleStep(0.5);
    m_clipSpin->setDecimals(1);
    m_clipSpin->setSuffix("%");
    clipLayout->addWidget(m_clipSpin);
    mainLayout->addLayout(clipLayout);

    // Std Dev K parameter
    auto *stddevLayout = new QHBoxLayout();
    m_stddevLabel = new QLabel(tr("Std Dev K:"), this);
    stddevLayout->addWidget(m_stddevLabel);
    m_stddevSpin = new QDoubleSpinBox(this);
    m_stddevSpin->setRange(0.1, 10.0);
    m_stddevSpin->setValue(2.0);
    m_stddevSpin->setSingleStep(0.5);
    m_stddevSpin->setDecimals(1);
    stddevLayout->addWidget(m_stddevSpin);
    mainLayout->addLayout(stddevLayout);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:"), this));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &ContrastStretchDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &ContrastStretchDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    // Initialize visibility
    onMethodChanged(0);
}

void ContrastStretchDialog::onMethodChanged(int index)
{
    // Linear: no extra params
    // Percentage Clip: show clip %
    // Std Dev: show K
    // Histogram Equalization: no extra params
    m_clipLabel->setVisible(index == 1);
    m_clipSpin->setVisible(index == 1);
    m_stddevLabel->setVisible(index == 2);
    m_stddevSpin->setVisible(index == 2);
}

void ContrastStretchDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void ContrastStretchDialog::onRun()
{
    // Validate inputs
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Contrast Stretch"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("Contrast Stretch"), tr("No valid raster layer selected."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "contrast_stretch", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    int bandCount = srcDataset.bandCount();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Read all bands
    std::vector<std::vector<float>> allBands(bandCount, std::vector<float>(pixelCount));
    for (int b = 0; b < bandCount; ++b) {
        if (!srcDataset.readBandData(b + 1, allBands[b].data(), width, height)) {
            QgsMessageLog::logMessage(tr("Failed to read band %1.").arg(b + 1),
                                      "contrast_stretch", Qgis::MessageLevel::Critical);
            return;
        }
    }

    // Apply stretch to each band
    int methodIndex = m_methodCombo->currentIndex();
    std::vector<std::vector<float>> outputBands(bandCount, std::vector<float>(pixelCount));

    for (int b = 0; b < bandCount; ++b) {
        switch (methodIndex) {
        case 0: // Linear
        {
            // Compute per-band min/max for full-range stretch
            float minVal = *std::min_element(allBands[b].begin(), allBands[b].end());
            float maxVal = *std::max_element(allBands[b].begin(), allBands[b].end());
            ImageEnhancement::linearStretch(allBands[b].data(), outputBands[b].data(),
                                            pixelCount, minVal, maxVal);
            break;
        }
        case 1: // Percentage Clip
            ImageEnhancement::percentClipStretch(allBands[b].data(), outputBands[b].data(),
                                                 pixelCount,
                                                 static_cast<float>(m_clipSpin->value()));
            break;
        case 2: // Std Dev
            ImageEnhancement::stddevStretch(allBands[b].data(), outputBands[b].data(),
                                            pixelCount,
                                            static_cast<float>(m_stddevSpin->value()));
            break;
        case 3: // Histogram Equalization
            ImageEnhancement::histogramEqualize(allBands[b].data(), outputBands[b].data(),
                                                pixelCount);
            break;
        }
    }

    // Create output file using GDAL
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        QgsMessageLog::logMessage(tr("Failed to get GeoTIFF driver."),
                                  "contrast_stretch", Qgis::MessageLevel::Critical);
        return;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    GDALDatasetH dstDataset = GDALCreate(driver, outputPath.toUtf8().constData(),
                                          width, height, bandCount, GDT_Float32, options);
    CSLDestroy(options);

    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file."),
                                  "contrast_stretch", Qgis::MessageLevel::Critical);
        return;
    }

    // Copy geotransform and projection from source
    std::array<double, 6> gt = srcDataset.geoTransform();
    GDALSetGeoTransform(dstDataset, gt.data());
    GDALSetProjection(dstDataset, srcDataset.projection().toUtf8().constData());

    // Write all output bands
    for (int b = 0; b < bandCount; ++b) {
        GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset, b + 1);
        if (!dstBand) {
            QgsMessageLog::logMessage(tr("Failed to get output band %1.").arg(b + 1),
                                      "contrast_stretch", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
        CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                                   outputBands[b].data(), width, height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            QgsMessageLog::logMessage(tr("Failed to write output band %1.").arg(b + 1),
                                      "contrast_stretch", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
    }

    GDALClose(dstDataset);

    QgsMessageLog::logMessage(tr("Contrast stretch completed successfully! Output: %1").arg(outputPath),
                              "contrast_stretch", Qgis::MessageLevel::Success);
    accept();
}
