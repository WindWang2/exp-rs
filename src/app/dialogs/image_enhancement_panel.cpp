// image_enhancement_panel.cpp — Unified Image Enhancement Panel
#include "image_enhancement_panel.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"

#include <raster/qgsrasterlayer.h>
#include <qgsproject.h>
#include <qgsmessagelog.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>

#include <gdal.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <cmath>

ImageEnhancementPanel::ImageEnhancementPanel(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Image Enhancement"));
    resize(500, 600);
    setupUi();
}

void ImageEnhancementPanel::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
    if (layer) {
        m_layerCombo->setCurrentText(layer->name());
    }
}

void ImageEnhancementPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Layer selection
    auto *layerLayout = new QHBoxLayout();
    layerLayout->addWidget(new QLabel(tr("Input Layer:")));
    m_layerCombo = new QComboBox();
    layerLayout->addWidget(m_layerCombo);
    mainLayout->addLayout(layerLayout);

    // Populate layers
    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        if (auto *rl = qobject_cast<QgsRasterLayer *>(it.value())) {
            m_layerCombo->addItem(rl->name(), QVariant::fromValue(rl));
        }
    }

    // Method selection
    auto *methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:")));
    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(tr("Contrast Stretch"), 0);
    m_methodCombo->addItem(tr("Spatial Filter"), 1);
    m_methodCombo->addItem(tr("Band Ratio / IHS"), 2);
    m_methodCombo->addItem(tr("Speckle Filter (SAR)"), 3);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);

    // Parameters stack
    m_paramsStack = new QStackedWidget();
    auto *contrastGroup = new QGroupBox(tr("Contrast Stretch Parameters"));
    setupContrastStretchUi(contrastGroup);
    m_paramsStack->addWidget(contrastGroup);

    auto *spatialGroup = new QGroupBox(tr("Spatial Filter Parameters"));
    setupSpatialFilterUi(spatialGroup);
    m_paramsStack->addWidget(spatialGroup);

    auto *ratioGroup = new QGroupBox(tr("Band Ratio / IHS Parameters"));
    setupBandRatioUi(ratioGroup);
    m_paramsStack->addWidget(ratioGroup);

    auto *speckleGroup = new QGroupBox(tr("Speckle Filter Parameters"));
    setupSpeckleFilterUi(speckleGroup);
    m_paramsStack->addWidget(speckleGroup);

    mainLayout->addWidget(m_paramsStack);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit();
    m_outputEdit->setPlaceholderText(tr("Output raster path"));
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."));
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Status
    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"));
    m_runButton->setDefault(true);
    btnLayout->addWidget(m_runButton);
    auto *closeBtn = new QPushButton(tr("Close"));
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageEnhancementPanel::onMethodChanged);
    connect(browseBtn, &QPushButton::clicked, this, &ImageEnhancementPanel::onBrowseOutput);
    connect(m_runButton, &QPushButton::clicked, this, &ImageEnhancementPanel::onRun);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Initial state
    onMethodChanged(0);
}

void ImageEnhancementPanel::setupContrastStretchUi(QGroupBox *group)
{
    auto *layout = new QFormLayout(group);

    m_stretchTypeCombo = new QComboBox();
    m_stretchTypeCombo->addItem(tr("Linear Min-Max"), 0);
    m_stretchTypeCombo->addItem(tr("Percentage Clip"), 1);
    m_stretchTypeCombo->addItem(tr("Standard Deviation"), 2);
    m_stretchTypeCombo->addItem(tr("Histogram Equalization"), 3);
    layout->addRow(tr("Type:"), m_stretchTypeCombo);

    m_clipPercentSpin = new QDoubleSpinBox();
    m_clipPercentSpin->setRange(0.1, 10.0);
    m_clipPercentSpin->setValue(2.0);
    m_clipPercentSpin->setSuffix("%");
    layout->addRow(tr("Clip %:"), m_clipPercentSpin);

    m_stddevMultiplierSpin = new QDoubleSpinBox();
    m_stddevMultiplierSpin->setRange(0.5, 5.0);
    m_stddevMultiplierSpin->setValue(2.0);
    layout->addRow(tr("StdDev ×:"), m_stddevMultiplierSpin);
}

void ImageEnhancementPanel::setupSpatialFilterUi(QGroupBox *group)
{
    auto *layout = new QFormLayout(group);

    m_filterTypeCombo = new QComboBox();
    m_filterTypeCombo->addItem(tr("Mean"), 0);
    m_filterTypeCombo->addItem(tr("Gaussian"), 1);
    m_filterTypeCombo->addItem(tr("Median"), 2);
    m_filterTypeCombo->addItem(tr("Sobel (Edge)"), 3);
    m_filterTypeCombo->addItem(tr("Laplacian (Edge)"), 4);
    m_filterTypeCombo->addItem(tr("Custom Convolution"), 5);
    layout->addRow(tr("Filter:"), m_filterTypeCombo);

    m_customKernelCombo = new QComboBox();
    m_customKernelCombo->addItem(tr("3x3 Sharpen"), 0);
    m_customKernelCombo->addItem(tr("3x3 Emboss"), 1);
    m_customKernelCombo->addItem(tr("3x3 Edge Detect"), 2);
    m_customKernelCombo->addItem(tr("5x5 Gaussian"), 3);
    m_customKernelCombo->addItem(tr("Custom (enter below)"), 4);
    layout->addRow(tr("Preset:"), m_customKernelCombo);

    m_customKernelEdit = new QLineEdit();
    m_customKernelEdit->setPlaceholderText(tr("e.g., 0 -1 0 -1 5 -1 0 -1 0 (3x3 row-major)"));
    m_customKernelEdit->setEnabled(false);
    layout->addRow(tr("Kernel:"), m_customKernelEdit);

    connect(m_filterTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        bool isCustom = (idx == 5);
        m_customKernelCombo->setEnabled(isCustom);
        m_customKernelEdit->setEnabled(isCustom && m_customKernelCombo->currentIndex() == 4);
    });
    connect(m_customKernelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_customKernelEdit->setEnabled(idx == 4);
        if (idx == 0) m_customKernelEdit->setText("0 -1 0 -1 5 -1 0 -1 0");
        else if (idx == 1) m_customKernelEdit->setText("-2 -1 0 -1 1 1 0 1 2");
        else if (idx == 2) m_customKernelEdit->setText("-1 -1 -1 -1 8 -1 -1 -1 -1");
        else if (idx == 3) m_customKernelEdit->setText("1 4 6 4 1 4 16 24 16 4 6 24 36 24 6 4 16 24 16 4 1 4 6 4 1");
    });

    m_kernelSizeSpin = new QSpinBox();
    m_kernelSizeSpin->setRange(3, 15);
    m_kernelSizeSpin->setValue(5);
    m_kernelSizeSpin->setSingleStep(2);
    layout->addRow(tr("Kernel Size:"), m_kernelSizeSpin);

    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0.1, 10.0);
    m_sigmaSpin->setValue(1.0);
    m_sigmaSpin->setPrefix("σ = ");
    layout->addRow(tr("Sigma:"), m_sigmaSpin);
}

void ImageEnhancementPanel::setupBandRatioUi(QGroupBox *group)
{
    auto *layout = new QFormLayout(group);

    m_ratioTypeCombo = new QComboBox();
    m_ratioTypeCombo->addItem(tr("Band Ratio"), 0);
    m_ratioTypeCombo->addItem(tr("IHS Transform"), 1);
    layout->addRow(tr("Type:"), m_ratioTypeCombo);

    m_band1Combo = new QComboBox();
    m_band2Combo = new QComboBox();
    layout->addRow(tr("Band 1:"), m_band1Combo);
    layout->addRow(tr("Band 2:"), m_band2Combo);

    // Populate band combos when layer changes
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
        m_band1Combo->clear();
        m_band2Combo->clear();
        if (rl) {
            for (int i = 1; i <= rl->bandCount(); ++i) {
                QString name = rl->bandName(i);
                if (name.isEmpty()) name = tr("Band %1").arg(i);
                m_band1Combo->addItem(name, i);
                m_band2Combo->addItem(name, i);
            }
            if (m_band2Combo->count() > 1) m_band2Combo->setCurrentIndex(1);
        }
    });
}

void ImageEnhancementPanel::setupSpeckleFilterUi(QGroupBox *group)
{
    auto *layout = new QFormLayout(group);

    m_speckleTypeCombo = new QComboBox();
    m_speckleTypeCombo->addItem(tr("Lee"), 0);
    m_speckleTypeCombo->addItem(tr("Frost"), 1);
    m_speckleTypeCombo->addItem(tr("Kuan"), 2);
    m_speckleTypeCombo->addItem(tr("Gamma MAP"), 3);
    layout->addRow(tr("Filter:"), m_speckleTypeCombo);

    m_speckleKernelSpin = new QSpinBox();
    m_speckleKernelSpin->setRange(3, 15);
    m_speckleKernelSpin->setValue(5);
    m_speckleKernelSpin->setSingleStep(2);
    layout->addRow(tr("Kernel Size:"), m_speckleKernelSpin);

    m_noiseVarianceSpin = new QDoubleSpinBox();
    m_noiseVarianceSpin->setRange(0.001, 1.0);
    m_noiseVarianceSpin->setValue(0.1);
    m_noiseVarianceSpin->setDecimals(4);
    layout->addRow(tr("Noise Variance:"), m_noiseVarianceSpin);

    m_dampingSpin = new QDoubleSpinBox();
    m_dampingSpin->setRange(0.1, 10.0);
    m_dampingSpin->setValue(1.0);
    layout->addRow(tr("Damping (Frost):"), m_dampingSpin);
}

void ImageEnhancementPanel::onMethodChanged(int index)
{
    m_paramsStack->setCurrentIndex(index);
    updateOutputPath();
}

void ImageEnhancementPanel::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Output"), QString(),
                                                 tr("GeoTIFF (*.tif *.tiff)"));
    if (!path.isEmpty()) {
        m_outputEdit->setText(path);
    }
}

void ImageEnhancementPanel::updateOutputPath()
{
    auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
    if (!rl) return;

    QString basePath = QFileInfo(rl->source()).path();
    QString baseName = QFileInfo(rl->source()).baseName();
    int method = m_methodCombo->currentIndex();

    QString suffix;
    switch (method) {
        case 0: suffix = "_enhanced"; break;
        case 1: suffix = "_filtered"; break;
        case 2: suffix = "_ratio"; break;
        case 3: suffix = "_despeckled"; break;
    }

    m_outputEdit->setText(basePath + "/" + baseName + suffix + ".tif");
}

void ImageEnhancementPanel::onRun()
{
    auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
    if (!rl || !rl->isValid()) {
        QMessageBox::warning(this, tr("Error"), tr("Select a valid raster layer."));
        return;
    }

    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Specify an output file."));
        return;
    }

    // Capture parameters for async execution
    QString sourcePath = rl->source();
    int method = m_methodCombo->currentIndex();
    int stretchType = m_stretchTypeCombo->currentIndex();
    double clipPercent = m_clipPercentSpin->value();
    double stddevMult = m_stddevMultiplierSpin->value();
    int filterType = m_filterTypeCombo->currentIndex();
    int kernelSize = m_kernelSizeSpin->value();
    double sigma = m_sigmaSpin->value();
    int ratioType = m_ratioTypeCombo->currentIndex();
    int band1 = m_band1Combo->currentData().toInt();
    int band2 = m_band2Combo->currentData().toInt();
    int speckleType = m_speckleTypeCombo->currentIndex();
    int speckleKernel = m_speckleKernelSpin->value();
    double noiseVar = m_noiseVarianceSpin->value();
    double damping = m_dampingSpin->value();
    QString customKernelStr = m_customKernelEdit->text().trimmed();

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &ImageEnhancementPanel::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &ImageEnhancementPanel::onFailed);
    }

    m_runButton->setEnabled(false);
    m_statusLabel->setText(tr("Processing..."));

    m_runner->run([sourcePath, outputPath, method, stretchType, clipPercent, stddevMult,
                    filterType, kernelSize, sigma, ratioType, band1, band2,
                    speckleType, speckleKernel, noiseVar, damping, customKernelStr]() -> QString {
        // Open source
        GDALDatasetH srcDs = GDALOpen(sourcePath.toUtf8().constData(), GA_ReadOnly);
        if (!srcDs) return QString();

        int w = GDALGetRasterXSize(srcDs);
        int h = GDALGetRasterYSize(srcDs);
        int bands = GDALGetRasterCount(srcDs);

        // Read all bands
        std::vector<std::vector<float>> inputBands(bands);
        for (int b = 0; b < bands; ++b) {
            inputBands[b].resize(w * h);
            GDALRasterBandH band = GDALGetRasterBand(srcDs, b + 1);
            if (!band) { GDALClose(srcDs); return QString(); }
            if (GDALRasterIO(band, GF_Read, 0, 0, w, h, inputBands[b].data(), w, h, GDT_Float32, 0, 0) != CE_None) {
                GDALClose(srcDs);
                return QString();
            }
        }

        int hasNodata = 0;
        float nodata = static_cast<float>(GDALGetRasterNoDataValue(GDALGetRasterBand(srcDs, 1), &hasNodata));
        if (!hasNodata) nodata = -9999.0f;
        GDALClose(srcDs);

        // Process based on method
        std::vector<std::vector<float>> outputBands;
        int outBandCount = bands;

        if (method == 0) {
            // Contrast stretch
            outputBands.resize(bands);
            for (int b = 0; b < bands; ++b) {
                outputBands[b].resize(w * h);
                if (stretchType == 0) {
                    ImageEnhancement::linearStretch(inputBands[b].data(), outputBands[b].data(), w * h, 0, 0, nodata);
                } else if (stretchType == 1) {
                    ImageEnhancement::percentClipStretch(inputBands[b].data(), outputBands[b].data(), w * h, clipPercent / 100.0, nodata);
                } else if (stretchType == 2) {
                    ImageEnhancement::stddevStretch(inputBands[b].data(), outputBands[b].data(), w * h, stddevMult, nodata);
                } else {
                    ImageEnhancement::histogramEqualize(inputBands[b].data(), outputBands[b].data(), w * h, 256, nodata);
                }
            }
        } else if (method == 1) {
            // Spatial filter
            outputBands.resize(bands);
            for (int b = 0; b < bands; ++b) {
                outputBands[b].resize(w * h);
                if (filterType == 0) {
                    ImageEnhancement::meanFilter(inputBands[b].data(), outputBands[b].data(), w, h, kernelSize);
                } else if (filterType == 1) {
                    ImageEnhancement::gaussianFilter(inputBands[b].data(), outputBands[b].data(), w, h, kernelSize, sigma);
                } else if (filterType == 2) {
                    ImageEnhancement::medianFilter(inputBands[b].data(), outputBands[b].data(), w, h, kernelSize);
                } else if (filterType == 3) {
                    ImageEnhancement::sobelFilter(inputBands[b].data(), outputBands[b].data(), w, h);
                } else if (filterType == 4) {
                    ImageEnhancement::laplacianFilter(inputBands[b].data(), outputBands[b].data(), w, h);
                } else if (filterType == 5) {
                    // Custom convolution
                    QStringList parts = customKernelStr.split(' ', Qt::SkipEmptyParts);
                    std::vector<float> kernel;
                    for (const QString &p : parts) {
                        bool ok;
                        float val = p.toFloat(&ok);
                        if (ok) kernel.push_back(val);
                    }
                    if (kernel.size() >= 4) {
                        int kSize = static_cast<int>(std::sqrt(kernel.size()));
                        if (kSize * kSize == static_cast<int>(kernel.size()) && kSize % 2 == 1) {
                            ImageEnhancement::convolve(inputBands[b].data(), outputBands[b].data(),
                                                       w, h, kernel.data(), kSize);
                        }
                    }
                }
            }
        } else if (method == 2) {
            // Band ratio / IHS
            if (ratioType == 0) {
                outBandCount = 1;
                outputBands.resize(1);
                outputBands[0].resize(w * h);
                if (band1 > 0 && band1 <= bands && band2 > 0 && band2 <= bands) {
                    ImageEnhancement::bandRatio(inputBands[band1-1].data(), inputBands[band2-1].data(),
                                               outputBands[0].data(), w * h);
                }
            } else {
                outBandCount = 3;
                outputBands.resize(3);
                for (int b = 0; b < 3; ++b) outputBands[b].resize(w * h);
                if (bands >= 3) {
                    // IHS transform pixel by pixel
                    for (size_t p = 0; p < static_cast<size_t>(w) * h; ++p) {
                        ImageEnhancement::rgbToIhs(
                            inputBands[0][p], inputBands[1][p], inputBands[2][p],
                            outputBands[0][p], outputBands[1][p], outputBands[2][p]);
                    }
                }
            }
        } else {
            // Speckle filter
            outputBands.resize(bands);
            for (int b = 0; b < bands; ++b) {
                outputBands[b].resize(w * h);
                if (speckleType == 0) {
                    ImageEnhancement::leeFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, noiseVar);
                } else if (speckleType == 1) {
                    ImageEnhancement::frostFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, damping);
                } else if (speckleType == 2) {
                    ImageEnhancement::kuanFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, noiseVar);
                } else {
                    ImageEnhancement::gammaMapFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, noiseVar);
                }
            }
        }

        // Write output
