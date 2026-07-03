// src/app/dialogs/band_ratio_dialog.cpp
#include "band_ratio_dialog.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>

#include <gdal.h>
#include <cpl_error.h>

BandRatioDialog::BandRatioDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void BandRatioDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Mode selection
    auto *modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(tr("Mode:"), this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({tr("Band Ratio"), tr("IHS Transform")});
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandRatioDialog::onModeChanged);
    modeLayout->addWidget(m_modeCombo);
    mainLayout->addLayout(modeLayout);

    // Band Ratio selectors
    auto *ratioLayout = new QHBoxLayout();
    m_band1Label = new QLabel(tr("Numerator:"), this);
    ratioLayout->addWidget(m_band1Label);
    m_band1Combo = new QComboBox(this);
    ratioLayout->addWidget(m_band1Combo);
    m_band2Label = new QLabel(tr("Denominator:"), this);
    ratioLayout->addWidget(m_band2Label);
    m_band2Combo = new QComboBox(this);
    ratioLayout->addWidget(m_band2Combo);
    mainLayout->addLayout(ratioLayout);

    // IHS selectors
    auto *ihsLayout = new QHBoxLayout();
    m_redLabel = new QLabel(tr("R:"), this);
    ihsLayout->addWidget(m_redLabel);
    m_redCombo = new QComboBox(this);
    ihsLayout->addWidget(m_redCombo);
    m_greenLabel = new QLabel(tr("G:"), this);
    ihsLayout->addWidget(m_greenLabel);
    m_greenCombo = new QComboBox(this);
    ihsLayout->addWidget(m_greenCombo);
    m_blueLabel = new QLabel(tr("B:"), this);
    ihsLayout->addWidget(m_blueLabel);
    m_blueCombo = new QComboBox(this);
    ihsLayout->addWidget(m_blueCombo);
    mainLayout->addLayout(ihsLayout);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);

    // Initialize visibility
    onModeChanged(0);
}

void BandRatioDialog::populateBandCombos()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    int bandCount = m_rasterLayer->bandCount();

    // Clear existing items
    m_band1Combo->clear();
    m_band2Combo->clear();
    m_redCombo->clear();
    m_greenCombo->clear();
    m_blueCombo->clear();

    // Populate with band numbers
    for (int i = 1; i <= bandCount; ++i) {
        QString bandName = tr("Band %1").arg(i);
        m_band1Combo->addItem(bandName, i);
        m_band2Combo->addItem(bandName, i);
        m_redCombo->addItem(bandName, i);
        m_greenCombo->addItem(bandName, i);
        m_blueCombo->addItem(bandName, i);
    }

    // Set defaults
    if (bandCount >= 2) {
        m_band1Combo->setCurrentIndex(0);
        m_band2Combo->setCurrentIndex(1);
    }
    if (bandCount >= 3) {
        m_redCombo->setCurrentIndex(0);
        m_greenCombo->setCurrentIndex(1);
        m_blueCombo->setCurrentIndex(2);
    }
}

void BandRatioDialog::onModeChanged(int index)
{
    // Band Ratio mode: show ratio controls
    // IHS mode: show RGB controls
    bool isRatio = (index == 0);
    m_band1Label->setVisible(isRatio);
    m_band1Combo->setVisible(isRatio);
    m_band2Label->setVisible(isRatio);
    m_band2Combo->setVisible(isRatio);

    m_redLabel->setVisible(!isRatio);
    m_redCombo->setVisible(!isRatio);
    m_greenLabel->setVisible(!isRatio);
    m_greenCombo->setVisible(!isRatio);
    m_blueLabel->setVisible(!isRatio);
    m_blueCombo->setVisible(!isRatio);
}

void BandRatioDialog::onRun()
{
    // Capture parameters for async execution
    QString sourcePath = m_rasterLayer->source();
    int modeIndex = m_modeCombo->currentIndex();
    int band1Num = m_band1Combo->currentData().toInt();
    int band2Num = m_band2Combo->currentData().toInt();
    int redNum = m_redCombo->currentData().toInt();
    int greenNum = m_greenCombo->currentData().toInt();
    int blueNum = m_blueCombo->currentData().toInt();

    runGdalTask([sourcePath, outputPath = outputPath(), modeIndex, band1Num, band2Num,
                   redNum, greenNum, blueNum]() -> QString {
    try {
        // Open source dataset
        GdalDatasetWrapper srcDataset;
        if (!srcDataset.open(sourcePath)) return QString();

        int width = srcDataset.width();
        int height = srcDataset.height();
        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

        int outBandCount = (modeIndex == 0) ? 1 : 3;

        auto readBand = [&](int bandNum) -> std::vector<float> {
            std::vector<float> buffer(pixelCount);
            if (!srcDataset.readBandData(bandNum, buffer.data(), width, height))
                return {};
            return buffer;
        };

        std::vector<std::vector<float>> outputBands(outBandCount, std::vector<float>(pixelCount));

        if (modeIndex == 0) {
            std::vector<float> b1 = readBand(band1Num);
            std::vector<float> b2 = readBand(band2Num);
            if (b1.empty() || b2.empty()) return QString();
            ImageEnhancement::bandRatio(b1.data(), b2.data(), outputBands[0].data(), pixelCount);
        } else {
            std::vector<float> r = readBand(redNum);
            std::vector<float> g = readBand(greenNum);
            std::vector<float> b = readBand(blueNum);
            if (r.empty() || g.empty() || b.empty()) return QString();
            for (size_t i = 0; i < pixelCount; ++i) {
                float ii, h, s;
                ImageEnhancement::rgbToIhs(r[i], g[i], b[i], ii, h, s);
                outputBands[0][i] = ii;
                outputBands[1][i] = h;
                outputBands[2][i] = s;
            }
        }

        // Create output
        QString error;
        GdalDatasetGuard dst(createOutputTiff(outputPath, width, height, outBandCount,
                                              GDT_Float32, srcDataset.geoTransform(),
                                              srcDataset.projection(), &error));
        if (!dst) return QString();

        for (int b = 0; b < outBandCount; ++b) {
            GDALRasterBandH band = GDALGetRasterBand(dst.get(), b + 1);
            if (!band) return QString();
            GDAL_SAFE_CALL( GDALRasterIO(band, GF_Write, 0, 0, width, height,
                            outputBands[b].data(), width, height, GDT_Float32, 0, 0),
                            "Failed to write output band" );
        }

        return outputPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}


