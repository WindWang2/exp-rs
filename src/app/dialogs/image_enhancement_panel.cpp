// image_enhancement_panel.cpp — Unified Image Enhancement Panel
#include "image_enhancement_panel.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/image_fusion.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

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
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(tr("Image Enhancement"));
    resize(500, 600);
    setupUi();
}

void ImageEnhancementPanel::setupUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
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
    m_stackedWidget = new QStackedWidget();

    // Stretch options
    auto *stretchGroup = new QGroupBox(tr("Contrast Stretch Parameters"));
    setupStretchOptions(new QVBoxLayout(stretchGroup));
    m_stackedWidget->addWidget(stretchGroup);

    // Filter options
    auto *filterGroup = new QGroupBox(tr("Spatial Filter Parameters"));
    setupFilterOptions(new QVBoxLayout(filterGroup));
    m_stackedWidget->addWidget(filterGroup);

    // Band ratio options
    auto *ratioGroup = new QGroupBox(tr("Band Ratio / IHS Parameters"));
    setupBandRatioOptions(new QVBoxLayout(ratioGroup));
    m_stackedWidget->addWidget(ratioGroup);

    // Speckle options
    auto *speckleGroup = new QGroupBox(tr("Speckle Filter Parameters"));
    setupSpeckleOptions(new QVBoxLayout(speckleGroup));
    m_stackedWidget->addWidget(speckleGroup);

    mainLayout->addWidget(m_stackedWidget);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Status
    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    // Buttons (from base class)
    setupButtonBar(mainLayout);

    // Connections
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageEnhancementPanel::onMethodChanged);

    // Initial state
    onMethodChanged(0);
}

void ImageEnhancementPanel::setupStretchOptions(QVBoxLayout *layout)
{
    auto *formLayout = new QFormLayout();

    m_stretchTypeCombo = new QComboBox();
    m_stretchTypeCombo->addItem(tr("Linear Min-Max"), 0);
    m_stretchTypeCombo->addItem(tr("Percentage Clip"), 1);
    m_stretchTypeCombo->addItem(tr("Standard Deviation"), 2);
    m_stretchTypeCombo->addItem(tr("Histogram Equalization"), 3);
    formLayout->addRow(tr("Type:"), m_stretchTypeCombo);

    m_clipPercentSpin = new QDoubleSpinBox();
    m_clipPercentSpin->setRange(0.1, 10.0);
    m_clipPercentSpin->setValue(2.0);
    m_clipPercentSpin->setSuffix("%");
    m_clipLabel = new QLabel(tr("Clip %:"));
    formLayout->addRow(m_clipLabel, m_clipPercentSpin);

    m_stddevMultSpin = new QDoubleSpinBox();
    m_stddevMultSpin->setRange(0.5, 5.0);
    m_stddevMultSpin->setValue(2.0);
    m_stddevLabel = new QLabel(tr("StdDev ×:"));
    formLayout->addRow(m_stddevLabel, m_stddevMultSpin);

    layout->addLayout(formLayout);

    // Show/hide based on type
    connect(m_stretchTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_clipLabel->setVisible(idx == 1);
        m_clipPercentSpin->setVisible(idx == 1);
        m_stddevLabel->setVisible(idx == 2);
        m_stddevMultSpin->setVisible(idx == 2);
    });
    m_clipLabel->setVisible(false);
    m_clipPercentSpin->setVisible(false);
    m_stddevLabel->setVisible(false);
    m_stddevMultSpin->setVisible(false);
}

void ImageEnhancementPanel::setupFilterOptions(QVBoxLayout *layout)
{
    auto *formLayout = new QFormLayout();

    m_filterTypeCombo = new QComboBox();
    m_filterTypeCombo->addItem(tr("Mean"), 0);
    m_filterTypeCombo->addItem(tr("Gaussian"), 1);
    m_filterTypeCombo->addItem(tr("Median"), 2);
    m_filterTypeCombo->addItem(tr("Sobel (Edge)"), 3);
    m_filterTypeCombo->addItem(tr("Laplacian (Edge)"), 4);
    formLayout->addRow(tr("Filter:"), m_filterTypeCombo);

    m_kernelSizeCombo = new QComboBox();
    m_kernelSizeCombo->addItem("3×3", 3);
    m_kernelSizeCombo->addItem("5×5", 5);
    m_kernelSizeCombo->addItem("7×7", 7);
    m_kernelSizeCombo->addItem("9×9", 9);
    formLayout->addRow(tr("Kernel Size:"), m_kernelSizeCombo);

    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0.1, 10.0);
    m_sigmaSpin->setValue(1.0);
    m_sigmaSpin->setPrefix("σ = ");
    m_sigmaLabel = new QLabel(tr("Sigma:"));
    formLayout->addRow(m_sigmaLabel, m_sigmaSpin);

    m_customKernelEdit = new QLineEdit();
    m_customKernelEdit->setPlaceholderText(tr("e.g., 0 -1 0 -1 5 -1 0 -1 0 (3x3 row-major)"));
    m_customKernelLabel = new QLabel(tr("Custom Kernel:"));
    formLayout->addRow(m_customKernelLabel, m_customKernelEdit);

    layout->addLayout(formLayout);

    // Show/hide sigma based on filter type
    connect(m_filterTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_sigmaLabel->setVisible(idx == 1);
        m_sigmaSpin->setVisible(idx == 1);
        m_customKernelLabel->setVisible(idx == 4);
        m_customKernelEdit->setVisible(idx == 4);
        m_kernelSizeCombo->setEnabled(idx != 4);
    });
    m_sigmaLabel->setVisible(false);
    m_sigmaSpin->setVisible(false);
    m_customKernelLabel->setVisible(false);
    m_customKernelEdit->setVisible(false);
}

void ImageEnhancementPanel::setupBandRatioOptions(QVBoxLayout *layout)
{
    auto *formLayout = new QFormLayout();

    m_ratioTypeCombo = new QComboBox();
    m_ratioTypeCombo->addItem(tr("Band Ratio"), 0);
    m_ratioTypeCombo->addItem(tr("IHS Transform"), 1);
    formLayout->addRow(tr("Type:"), m_ratioTypeCombo);

    m_band1Combo = new QComboBox();
    m_band2Combo = new QComboBox();
    m_band1Label = new QLabel(tr("Band 1:"));
    m_band2Label = new QLabel(tr("Band 2:"));
    formLayout->addRow(m_band1Label, m_band1Combo);
    formLayout->addRow(m_band2Label, m_band2Combo);

    layout->addLayout(formLayout);

    // Populate bands when raster layer changes
    connect(this, &QDialog::finished, this, [this]() {
        // Cleanup
    });
}

void ImageEnhancementPanel::setupSpeckleOptions(QVBoxLayout *layout)
{
    auto *formLayout = new QFormLayout();

    m_speckleTypeCombo = new QComboBox();
    m_speckleTypeCombo->addItem(tr("Lee"), 0);
    m_speckleTypeCombo->addItem(tr("Frost"), 1);
    m_speckleTypeCombo->addItem(tr("Kuan"), 2);
    m_speckleTypeCombo->addItem(tr("Gamma MAP"), 3);
    formLayout->addRow(tr("Filter:"), m_speckleTypeCombo);

    m_speckleKernelCombo = new QComboBox();
    m_speckleKernelCombo->addItem("3×3", 3);
    m_speckleKernelCombo->addItem("5×5", 5);
    m_speckleKernelCombo->addItem("7×7", 7);
    formLayout->addRow(tr("Kernel Size:"), m_speckleKernelCombo);

    m_noiseVarSpin = new QDoubleSpinBox();
    m_noiseVarSpin->setRange(0.001, 1.0);
    m_noiseVarSpin->setValue(0.1);
    m_noiseVarSpin->setDecimals(4);
    m_noiseVarLabel = new QLabel(tr("Noise Variance:"));
    formLayout->addRow(m_noiseVarLabel, m_noiseVarSpin);

    m_dampingSpin = new QDoubleSpinBox();
    m_dampingSpin->setRange(0.1, 10.0);
    m_dampingSpin->setValue(1.0);
    m_dampingLabel = new QLabel(tr("Damping (Frost):"));
    formLayout->addRow(m_dampingLabel, m_dampingSpin);

    layout->addLayout(formLayout);

    // Show/hide damping based on filter type
    connect(m_speckleTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_dampingLabel->setVisible(idx == 1);
        m_dampingSpin->setVisible(idx == 1);
        m_noiseVarLabel->setVisible(idx != 1);
        m_noiseVarSpin->setVisible(idx != 1);
    });
    m_dampingLabel->setVisible(false);
    m_dampingSpin->setVisible(false);
}

void ImageEnhancementPanel::onMethodChanged(int index)
{
    m_stackedWidget->setCurrentIndex(index);
}

void ImageEnhancementPanel::onRun()
{
    if (!validateInputs()) return;

    QgsRasterLayer *rl = m_rasterLayer;
    if (!rl) {
        QMessageBox::warning(this, dialogTitle(), tr("Please select a raster layer."));
        return;
    }

    QString sourcePath = rl->source();
    QString outPath = outputPath();

    int method = m_methodCombo->currentIndex();

    // Disable run button
    m_runButton->setEnabled(false);
    m_statusLabel->setText(tr("Processing..."));

    // Create runner if needed
    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &ImageEnhancementPanel::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &ImageEnhancementPanel::onFailed);
    }

    // Capture parameters
    int stretchType = m_stretchTypeCombo->currentIndex();
    double clipPercent = m_clipPercentSpin->value();
    double stddevMult = m_stddevMultSpin->value();
    int filterType = m_filterTypeCombo->currentIndex();
    int kernelSize = m_kernelSizeCombo->currentData().toInt();
    double sigma = m_sigmaSpin->value();
    QString customKernelStr = m_customKernelEdit->text();
    int ratioType = m_ratioTypeCombo->currentIndex();
    int band1 = m_band1Combo->currentData().toInt();
    int band2 = m_band2Combo->currentData().toInt();
    int speckleType = m_speckleTypeCombo->currentIndex();
    int speckleKernel = m_speckleKernelCombo->currentData().toInt();
    double noiseVar = m_noiseVarSpin->value();
    double damping = m_dampingSpin->value();

    m_runner->run([sourcePath, outPath, method, stretchType, clipPercent, stddevMult,
                    filterType, kernelSize, sigma, customKernelStr, ratioType, band1, band2,
                    speckleType, speckleKernel, noiseVar, damping]() -> QString {
    try {
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

        // Process based on method
        std::vector<std::vector<float>> outputBands(bands);
        for (int b = 0; b < bands; ++b) outputBands[b].resize(w * h);

        size_t pixelCount = static_cast<size_t>(w) * h;

        if (method == 0) {
            // Contrast stretch
            for (int b = 0; b < bands; ++b) {
                switch (stretchType) {
                case 0: // Linear
                {
                    float minVal = *std::min_element(inputBands[b].begin(), inputBands[b].end());
                    float maxVal = *std::max_element(inputBands[b].begin(), inputBands[b].end());
                    ImageEnhancement::linearStretch(inputBands[b].data(), outputBands[b].data(), pixelCount, minVal, maxVal);
                    break;
                }
                case 1: // Percentage clip
                    ImageEnhancement::percentClipStretch(inputBands[b].data(), outputBands[b].data(), pixelCount, static_cast<float>(clipPercent));
                    break;
                case 2: // Std dev
                    ImageEnhancement::stddevStretch(inputBands[b].data(), outputBands[b].data(), pixelCount, static_cast<float>(stddevMult));
                    break;
                case 3: // Histogram eq
                    ImageEnhancement::histogramEqualize(inputBands[b].data(), outputBands[b].data(), pixelCount);
                    break;
                }
            }
        } else if (method == 1) {
            // Spatial filter
            for (int b = 0; b < bands; ++b) {
                switch (filterType) {
                case 0: ImageEnhancement::meanFilter(inputBands[b].data(), outputBands[b].data(), w, h, kernelSize); break;
                case 1: ImageEnhancement::gaussianFilter(inputBands[b].data(), outputBands[b].data(), w, h, kernelSize); break;
                case 2: ImageEnhancement::medianFilter(inputBands[b].data(), outputBands[b].data(), w, h, kernelSize); break;
                case 3: ImageEnhancement::sobelFilter(inputBands[b].data(), outputBands[b].data(), w, h); break;
                case 4: ImageEnhancement::laplacianFilter(inputBands[b].data(), outputBands[b].data(), w, h); break;
                }
            }
        } else if (method == 2) {
            // Band ratio / IHS
            if (ratioType == 0 && bands >= 2) {
                // Band ratio
                int b1 = std::min(band1, bands);
                int b2 = std::min(band2, bands);
                ImageEnhancement::bandRatio(inputBands[b1-1].data(), inputBands[b2-1].data(), outputBands[0].data(), pixelCount);
                bands = 1;
            } else if (ratioType == 1 && bands >= 3) {
                // IHS
                int r = std::min(band1, bands) - 1;
                int g = std::min(band2, bands) - 1;
                int b = (r == 0) ? ((g == 1) ? 2 : 1) : 0;
                auto ihsResult = ImageFusion::ihsFusion(inputBands[r].data(), inputBands[g].data(), inputBands[b].data(),
                                                        inputBands[r].data(), w, h, std::numeric_limits<float>::quiet_NaN());
                // Convert QVector<QVector<float>> to std::vector<std::vector<float>>
                outputBands.resize(ihsResult.size());
                for (int i = 0; i < ihsResult.size(); ++i) {
                    outputBands[i].resize(ihsResult[i].size());
                    std::copy(ihsResult[i].begin(), ihsResult[i].end(), outputBands[i].begin());
                }
                bands = static_cast<int>(ihsResult.size());
            }
        } else if (method == 3) {
            // Speckle filter
            for (int b = 0; b < bands; ++b) {
                switch (speckleType) {
                case 0: ImageEnhancement::leeFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, static_cast<float>(noiseVar)); break;
                case 1: ImageEnhancement::frostFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, static_cast<float>(damping)); break;
                case 2: ImageEnhancement::kuanFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, static_cast<float>(noiseVar)); break;
                case 3: ImageEnhancement::gammaMapFilter(inputBands[b].data(), outputBands[b].data(), w, h, speckleKernel, static_cast<float>(noiseVar)); break;
                }
            }
        }

        // Write output using shared utility
        GeoInfo geo = extractGeoInfo(srcDs);
        GDALClose(srcDs);

        QString error;
        if (!writeGdalOutput(outPath, w, h, outputBands, geo.geoTransform, geo.projection, &error))
            return QString();

        return outPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}

void ImageEnhancementPanel::onCompleted(const QString &outputPath)
{
    m_runButton->setEnabled(true);
    m_statusLabel->setText(tr("Completed!"));
    handleCompleted(outputPath);
}

void ImageEnhancementPanel::onFailed(const QString &errorMessage)
{
    m_runButton->setEnabled(true);
    m_statusLabel->setText(tr("Failed!"));
    handleFailed(errorMessage);
}


