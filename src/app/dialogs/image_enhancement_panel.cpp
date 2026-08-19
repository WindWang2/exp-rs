// image_enhancement_panel.cpp — Unified Image Enhancement Panel
#include "image_enhancement_panel.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>
#include <qgsproject.h>
#include <qgsmessagelog.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
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

ImageEnhancementPanel::ImageEnhancementPanel( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "影像增强" ) );
  resize( 520, 640 );
  setupUi();
}

void ImageEnhancementPanel::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *methodSec = SicnuUi::makeSection(
    this, tr( "增强类型" ),
    tr( "选择一类增强；下方参数页随类型切换。" ) );
  auto *methodForm = new QFormLayout();
  methodForm->setContentsMargins( 0, 0, 0, 0 );
  m_methodCombo = new QComboBox( methodSec );
  m_methodCombo->addItem( tr( "对比度拉伸" ), 0 );
  m_methodCombo->addItem( tr( "空间滤波" ), 1 );
  m_methodCombo->addItem( tr( "波段比值 / IHS" ), 2 );
  m_methodCombo->addItem( tr( "斑点滤波 (SAR)" ), 3 );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "对比度拉伸 / 空间滤波 / 波段比值·IHS / SAR 斑点滤波。" ) );
  methodForm->addRow( tr( "类型" ), m_methodCombo );
  qobject_cast<QVBoxLayout *>( methodSec->layout() )->addLayout( methodForm );
  mainLayout->addWidget( methodSec );

  m_stackedWidget = new QStackedWidget( this );

  auto *stretchGroup = SicnuUi::makeGroup( this, tr( "对比度拉伸参数" ) );
  setupStretchOptions( new QVBoxLayout( stretchGroup ) );
  m_stackedWidget->addWidget( stretchGroup );

  auto *filterGroup = SicnuUi::makeGroup( this, tr( "空间滤波参数" ) );
  setupFilterOptions( new QVBoxLayout( filterGroup ) );
  m_stackedWidget->addWidget( filterGroup );

  auto *ratioGroup = SicnuUi::makeGroup( this, tr( "波段比值 / IHS 参数" ) );
  setupBandRatioOptions( new QVBoxLayout( ratioGroup ) );
  m_stackedWidget->addWidget( ratioGroup );

  auto *speckleGroup = SicnuUi::makeGroup( this, tr( "斑点滤波参数" ) );
  setupSpeckleOptions( new QVBoxLayout( speckleGroup ) );
  m_stackedWidget->addWidget( speckleGroup );

  mainLayout->addWidget( m_stackedWidget, 1 );
  setupOutputRow( mainLayout );
  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
  mainLayout->addWidget( m_statusLabel );
  setupButtonBar( mainLayout );

  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ImageEnhancementPanel::onMethodChanged );
  onMethodChanged( 0 );
}

void ImageEnhancementPanel::setupStretchOptions(QVBoxLayout *layout)
{
    auto *formLayout = new QFormLayout();

    m_stretchTypeCombo = new QComboBox();
    m_stretchTypeCombo->addItem(tr("Linear Min-Max"), 0);
    m_stretchTypeCombo->addItem(tr("Percentage Clip"), 1);
    m_stretchTypeCombo->addItem(tr("Standard Deviation"), 2);
    m_stretchTypeCombo->addItem(tr("Histogram Equalization"), 3);
    SicnuDialogHelp::tip( m_stretchTypeCombo, tr(
      "线性 / 百分比裁剪 / 标准差 / 直方图均衡。" ) );
    formLayout->addRow(tr("Type:"), m_stretchTypeCombo);

    m_clipPercentSpin = new QDoubleSpinBox();
    m_clipPercentSpin->setRange(0.1, 10.0);
    m_clipPercentSpin->setValue(2.0);
    m_clipPercentSpin->setSuffix("%");
    SicnuDialogHelp::tip( m_clipPercentSpin, tr( "两端裁剪百分比。常用 1–2%。" ) );
    m_clipLabel = new QLabel(tr("Clip %:"));
    formLayout->addRow(m_clipLabel, m_clipPercentSpin);

    m_stddevMultSpin = new QDoubleSpinBox();
    m_stddevMultSpin->setRange(0.5, 5.0);
    m_stddevMultSpin->setValue(2.0);
    SicnuDialogHelp::tip( m_stddevMultSpin, tr( "标准差倍数 K。常用 2。" ) );
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
    SicnuDialogHelp::tip( m_filterTypeCombo, tr( "平滑（均值/高斯/中值）或边缘（Sobel/Laplacian）。" ) );
    formLayout->addRow(tr("Filter:"), m_filterTypeCombo);

    m_kernelSizeCombo = new QComboBox();
    m_kernelSizeCombo->addItem("3×3", 3);
    m_kernelSizeCombo->addItem("5×5", 5);
    m_kernelSizeCombo->addItem("7×7", 7);
    m_kernelSizeCombo->addItem("9×9", 9);
    SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "卷积核大小。" ) );
    formLayout->addRow(tr("Kernel Size:"), m_kernelSizeCombo);

    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0.1, 10.0);
    m_sigmaSpin->setValue(1.0);
    m_sigmaSpin->setPrefix("σ = ");
    SicnuDialogHelp::tip( m_sigmaSpin, tr( "高斯滤波标准差 σ。" ) );
    m_sigmaLabel = new QLabel(tr("Sigma:"));
    formLayout->addRow(m_sigmaLabel, m_sigmaSpin);

    m_customKernelEdit = new QLineEdit();
    m_customKernelEdit->setPlaceholderText(tr("e.g., 0 -1 0 -1 5 -1 0 -1 0 (3x3 row-major)"));
    SicnuDialogHelp::tip( m_customKernelEdit, tr( "自定义核：按行主序空格分隔系数。" ) );
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
    SicnuDialogHelp::tip( m_ratioTypeCombo, tr( "波段比值或 IHS 变换。" ) );
    formLayout->addRow(tr("Type:"), m_ratioTypeCombo);

    m_band1Combo = new QComboBox();
    m_band2Combo = new QComboBox();
    m_band3Combo = new QComboBox();
    SicnuDialogHelp::tip( m_band1Combo, tr( "比值分子或 IHS 红色波段。" ) );
    SicnuDialogHelp::tip( m_band2Combo, tr( "比值分母或 IHS 绿色波段。" ) );
    SicnuDialogHelp::tip( m_band3Combo, tr( "IHS 蓝色波段。" ) );
    m_band1Label = new QLabel(tr("Band 1 / R:"));
    m_band2Label = new QLabel(tr("Band 2 / G:"));
    m_band3Label = new QLabel(tr("Band 3 / B:"));
    formLayout->addRow(m_band1Label, m_band1Combo);
    formLayout->addRow(m_band2Label, m_band2Combo);
    formLayout->addRow(m_band3Label, m_band3Combo);

    layout->addLayout(formLayout);

    auto updateBandVisibility = [this]() {
        bool isIhs = ( m_ratioTypeCombo->currentIndex() == 1 );
        m_band1Label->setText( isIhs ? tr( "Red (R):" ) : tr( "Band 1:" ) );
        m_band2Label->setText( isIhs ? tr( "Green (G):" ) : tr( "Band 2:" ) );
        m_band3Label->setVisible( isIhs );
        m_band3Combo->setVisible( isIhs );
    };
    connect( m_ratioTypeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [updateBandVisibility]( int ) { updateBandVisibility(); } );
    updateBandVisibility();

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
    SicnuDialogHelp::tip( m_speckleTypeCombo, tr( "SAR 斑点滤波：Lee/Frost/Kuan/Gamma-MAP。" ) );
    formLayout->addRow(tr("Filter:"), m_speckleTypeCombo);

    m_speckleKernelCombo = new QComboBox();
    m_speckleKernelCombo->addItem("3×3", 3);
    m_speckleKernelCombo->addItem("5×5", 5);
    m_speckleKernelCombo->addItem("7×7", 7);
    SicnuDialogHelp::tip( m_speckleKernelCombo, tr( "滤波窗口大小。" ) );
    formLayout->addRow(tr("Kernel Size:"), m_speckleKernelCombo);

    m_noiseVarSpin = new QDoubleSpinBox();
    m_noiseVarSpin->setRange(0.001, 1.0);
    m_noiseVarSpin->setValue(0.1);
    m_noiseVarSpin->setDecimals(4);
    SicnuDialogHelp::tip( m_noiseVarSpin, tr( "噪声方差（非 Frost）。" ) );
    m_noiseVarLabel = new QLabel(tr("Noise Variance:"));
    formLayout->addRow(m_noiseVarLabel, m_noiseVarSpin);

    m_dampingSpin = new QDoubleSpinBox();
    m_dampingSpin->setRange(0.1, 10.0);
    m_dampingSpin->setValue(1.0);
    SicnuDialogHelp::tip( m_dampingSpin, tr( "Frost 阻尼因子。" ) );
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

void ImageEnhancementPanel::setRasterLayer(QgsRasterLayer *layer)
{
    RasterProcessingDialogBase::setRasterLayer(layer);
    populateBandCombos();
}

void ImageEnhancementPanel::populateBandCombos()
{
    if (!m_band1Combo || !m_band2Combo || !m_band3Combo)
        return;
    m_band1Combo->clear();
    m_band2Combo->clear();
    m_band3Combo->clear();
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;
    int bands = m_rasterLayer->bandCount();
    for (int i = 1; i <= bands; ++i) {
        QString name = tr("Band %1").arg(i);
        m_band1Combo->addItem(name, i);
        m_band2Combo->addItem(name, i);
        m_band3Combo->addItem(name, i);
    }
    if (bands >= 2) {
        m_band1Combo->setCurrentIndex(0);
        m_band2Combo->setCurrentIndex(1);
    }
    if (bands >= 3) {
        m_band3Combo->setCurrentIndex(2);
    }
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

    m_statusLabel->setText(tr("Processing..."));

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
    int band3 = m_band3Combo ? m_band3Combo->currentData().toInt() : 0;
    int speckleType = m_speckleTypeCombo->currentIndex();
    int speckleKernel = m_speckleKernelCombo->currentData().toInt();
    double noiseVar = m_noiseVarSpin->value();
    double damping = m_dampingSpin->value();

    if (method == 2) {
        if (ratioType == 0) {
            if (band1 < 1 || band2 < 1 || band1 == band2) {
                QMessageBox::warning(this, dialogTitle(), tr("Please select valid distinct bands."));
                return;
            }
        } else {
            if (band1 < 1 || band2 < 1 || band3 < 1) {
                QMessageBox::warning(this, dialogTitle(), tr("Please select valid RGB bands for IHS transform."));
                return;
            }
            if (band1 == band2 || band1 == band3 || band2 == band3) {
                QMessageBox::warning(this, dialogTitle(), tr("Please select distinct bands for IHS transform."));
                return;
            }
            if (m_band1Combo->count() < 3) {
                QMessageBox::warning(this, dialogTitle(), tr("IHS transform requires an image with at least 3 bands."));
                return;
            }
        }
    }

    runGdalTask([sourcePath, outPath, method, stretchType, clipPercent, stddevMult,
                    filterType, kernelSize, sigma, customKernelStr, ratioType, band1, band2, band3,
                    speckleType, speckleKernel, noiseVar, damping]() -> QString {
    try {
        // Open source
        GDALDatasetH srcDs = GDALOpen(sourcePath.toUtf8().constData(), GA_ReadOnly);
        if (!srcDs) return QString();

        int w = GDALGetRasterXSize(srcDs);
        int h = GDALGetRasterYSize(srcDs);
        int bands = GDALGetRasterCount(srcDs);

        // Guard full-scene float buffers (input + output) against OOM.
        constexpr qint64 kMaxBytes = 2LL * 1024 * 1024 * 1024; // 2 GiB soft cap
        const qint64 estBytes = static_cast<qint64>( w ) * h * bands * static_cast<qint64>( sizeof( float ) ) * 2;
        if ( estBytes > kMaxBytes || estBytes < 0 )
        {
            GDALClose( srcDs );
            return QString();
        }

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
                outputBands.resize(1);
                bands = 1;
            } else if (ratioType == 1 && bands >= 3) {
                // IHS decomposition — true I/H/S components (panel-side fix for #380)
                // Mirrors BandRatioDialog which uses ImageEnhancement::rgbToIhs per pixel.
                int rIdx = std::min(band1, bands) - 1;
                int gIdx = std::min(band2, bands) - 1;
                int bIdx = std::min(band3, bands) - 1;
                outputBands.resize(3);
                for (int i = 0; i < 3; ++i) outputBands[i].resize(pixelCount);
                for (size_t i = 0; i < pixelCount; ++i) {
                    float rv = inputBands[rIdx][i];
                    float gv = inputBands[gIdx][i];
                    float bv = inputBands[bIdx][i];
                    // Mask invalid / NoData pixels: NaN or sentinel -9999
                    if (std::isnan(rv) || std::isnan(gv) || std::isnan(bv) ||
                        rv == -9999.f || gv == -9999.f || bv == -9999.f) {
                        outputBands[0][i] = std::numeric_limits<float>::quiet_NaN();
                        outputBands[1][i] = std::numeric_limits<float>::quiet_NaN();
                        outputBands[2][i] = std::numeric_limits<float>::quiet_NaN();
                        continue;
                    }
                    float ii, h, s;
                    ImageEnhancement::rgbToIhs(rv, gv, bv, ii, h, s);
                    outputBands[0][i] = ii;
                    outputBands[1][i] = h;
                    outputBands[2][i] = s;
                }
                bands = 3;
            } else {
                GDALClose(srcDs);
                return RasterProcessingDialogBase::gdalErrorMarker() +
                       ( ratioType == 0 ? QStringLiteral( "Band ratio requires at least 2 bands" )
                                        : QStringLiteral( "IHS transform requires at least 3 bands" ) );
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
    } catch (const std::exception &) {
        return QString();
    }
    });
}


