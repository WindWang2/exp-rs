// image_enhancement_panel.cpp — Unified Image Enhancement Panel
#include <algorithm>
#include "image_enhancement_panel.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
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
    using namespace ImageEnhancementStreaming;
    try {
        // Open source
        GdalDatasetWrapper src;
        if (!src.open(sourcePath)) return QString();

        const int w = src.width();
        const int h = src.height();
        const int bands = src.bandCount();

        // Band-count guards (worker-side backstop for the panel-side checks).
        if (method == 2 && ratioType == 0 && bands < 2) {
            return RasterProcessingDialogBase::gdalErrorMarker() +
                   QStringLiteral( "Band ratio requires at least 2 bands" );
        }
        if (method == 2 && ratioType == 1 && bands < 3) {
            return RasterProcessingDialogBase::gdalErrorMarker() +
                   QStringLiteral( "IHS transform requires at least 3 bands" );
        }

        // Resolve each band's declared NoData (float-cast; NaN when undeclared)
        // so stretches mask the real sentinel instead of a fabricated -9999 (#445).
        std::vector<float> bandNodata(bands, std::numeric_limits<float>::quiet_NaN());
        for (int b = 0; b < bands; ++b) {
            bool hasNd = false;
            const double nd = src.bandNoDataValue(b + 1, &hasNd);
            if (hasNd && std::isfinite(nd))
                bandNodata[b] = static_cast<float>(nd);
        }

        // Streaming conversion (#691): every path below runs as tile loops over
        // GdalBlockStream / GdalMultibandBlockStream writing through
        // GdalStreamingOutput — O(tile) memory instead of the previous
        // inputBands + outputBands full-raster frames. The former 2 GiB soft
        // cap (which silently rejected large scenes with an empty return) is
        // gone: no path materializes a full frame any more.
        int outBands = bands;
        if (method == 2)
            outBands = (ratioType == 0) ? 1 : 3;
        GdalStreamingOutput dst(outPath, w, h, outBands, GDT_Float32,
                                src.geoTransform(), src.projection());
        if (!dst.isOpen()) return QString();
        QString closeError;

        if (method == 0) {
            // Contrast stretch: streaming statistics pass + streaming apply
            // pass per band (exact replica of the stretch kernels).
            StretchParams params;
            switch (stretchType) {
            case 1: params.kind = StretchKind::PercentClip; break;
            case 2: params.kind = StretchKind::StdDev; break;
            case 3: params.kind = StretchKind::HistogramEqualize; break;
            default: params.kind = StretchKind::Linear; break;
            }
            params.clipPercent = static_cast<float>(clipPercent);
            params.stddevK = static_cast<float>(stddevMult);
            for (int b = 1; b <= bands; ++b) {
                if (!streamBandStretch(src, b, bandNodata[b - 1], params, dst, kTileDim)) {
                    dst.abandon();
                    return QString();
                }
            }
        } else if (method == 1) {
            // Spatial filter: halo tiles per band (halo = kernel radius; the
            // Sobel/Laplacian edge filters use a fixed 3×3 window).
            const int half = (filterType == 3 || filterType == 4) ? 1 : kernelSize / 2;
            for (int b = 1; b <= bands; ++b) {
                WindowedTileFn kernel;
                switch (filterType) {
                case 0: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            convolveTileMean(tile, buf, core, kernelSize); }; break;
                case 1: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            convolveTileGaussian(tile, buf, core, kernelSize, static_cast<float>(sigma)); }; break;
                case 2: {
                    // Full-frame medianFilter clamps the kernel to 7x7 — keep
                    // the streamed path behaviorally identical (review P2).
                    const int medianKernel = std::min(kernelSize, 7);
                    kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            convolveTileMedian(tile, buf, core, medianKernel); }; break;
                }
                case 3: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            convolveTileSobel(tile, buf, core); }; break;
                case 4: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            convolveTileLaplacian(tile, buf, core); }; break;
                }
                if (!streamBandWindowed(src, b, dst, kTileDim, half, kernel)) {
                    dst.abandon();
                    return QString();
                }
            }
        } else if (method == 2) {
            // Band ratio / IHS — stream only the involved bands (band-pair or
            // band-triple BIP tiles), never the whole band stack.
            if (ratioType == 0) {
                const std::vector<int> pair = { std::min(band1, bands), std::min(band2, bands) };
                GdalMultibandBlockStream stream(src, pair, kTileDim, kTileDim);
                std::vector<float> band1Buf(static_cast<size_t>(kTileDim) * kTileDim);
                std::vector<float> band2Buf(static_cast<size_t>(kTileDim) * kTileDim);
                std::vector<float> out(static_cast<size_t>(kTileDim) * kTileDim);
                const bool ok = stream.forEach([&](const GdalBlockStream::Tile &tile, const float *bip) {
                    const size_t n = static_cast<size_t>(tile.width) * tile.height;
                    for (size_t i = 0; i < n; ++i) {
                        band1Buf[i] = bip[i * 2];
                        band2Buf[i] = bip[i * 2 + 1];
                    }
                    bandRatioTile(band1Buf.data(), band2Buf.data(), out.data(), n);
                    return dst.writeTile(1, tile, out.data());
                });
                if (!ok) {
                    dst.abandon();
                    return QString();
                }
            } else {
                // IHS decomposition — true I/H/S components (panel-side fix for #380),
                // applied per band-triple tile with the panel's NaN masking.
                const std::vector<int> triple = { std::min(band1, bands), std::min(band2, bands),
                                                  std::min(band3, bands) };
                const float ndR = bandNodata[triple[0] - 1];
                const float ndG = bandNodata[triple[1] - 1];
                const float ndB = bandNodata[triple[2] - 1];
                GdalMultibandBlockStream stream(src, triple, kTileDim, kTileDim);
                std::vector<float> outI(static_cast<size_t>(kTileDim) * kTileDim);
                std::vector<float> outH(static_cast<size_t>(kTileDim) * kTileDim);
                std::vector<float> outS(static_cast<size_t>(kTileDim) * kTileDim);
                const bool ok = stream.forEach([&](const GdalBlockStream::Tile &tile, const float *bip) {
                    const size_t n = static_cast<size_t>(tile.width) * tile.height;
                    ihsTransformTile(bip, ndR, ndG, ndB, outI.data(), outH.data(), outS.data(), n);
                    return dst.writeTile(1, tile, outI.data())
                        && dst.writeTile(2, tile, outH.data())
                        && dst.writeTile(3, tile, outS.data());
                });
                if (!ok) {
                    dst.abandon();
                    return QString();
                }
            }
        } else if (method == 3) {
            // Speckle filter: the same tile-window kernels as the speckle
            // dialog (halo = kernel radius).
            const int half = speckleKernel / 2;
            for (int b = 1; b <= bands; ++b) {
                WindowedTileFn kernel;
                switch (speckleType) {
                case 0: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            speckleTileLee(tile, buf, core, speckleKernel, static_cast<float>(noiseVar)); }; break;
                case 1: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            speckleTileFrost(tile, buf, core, speckleKernel, static_cast<float>(damping)); }; break;
                case 2: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            speckleTileKuan(tile, buf, core, speckleKernel, static_cast<float>(noiseVar)); }; break;
                case 3: kernel = [&](const GdalBlockStream::Tile &tile, const float *buf, float *core) {
                            speckleTileGammaMap(tile, buf, core, speckleKernel, static_cast<float>(noiseVar)); }; break;
                }
                if (!streamBandWindowed(src, b, dst, kTileDim, half, kernel)) {
                    dst.abandon();
                    return QString();
                }
            }
        }

        if (!dst.closeWithError(&closeError))
            return QString();

        return outPath;
    } catch (const std::exception &) {
        return QString();
    }
    });
}


