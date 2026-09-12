// image_enhancement_panel.cpp — Unified Image Enhancement Panel
#include <algorithm>
#include "image_enhancement_panel.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

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

#include <cmath>

ImageEnhancementPanel::ImageEnhancementPanel( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "Image Enhancement" ) );
  resize( 520, 640 );
  setupUi();
}

void ImageEnhancementPanel::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *methodSec = SicnuUi::makeSection(
    this, tr( "Enhancement Type" ),
    tr( "Choose an enhancement type; the parameter pages below follow the selection." ) );
  auto *methodForm = new QFormLayout();
  methodForm->setContentsMargins( 0, 0, 0, 0 );
  m_methodCombo = new QComboBox( methodSec );
  m_methodCombo->addItem( tr( "Contrast Stretch" ), 0 );
  m_methodCombo->addItem( tr( "Spatial Filtering" ), 1 );
  m_methodCombo->addItem( tr( "Band Ratio / IHS" ), 2 );
  m_methodCombo->addItem( tr( "Speckle Filtering (SAR)" ), 3 );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    tr("Contrast stretch / spatial filtering / band ratio · IHS / SAR speckle filtering.") ) );
  methodForm->addRow( tr( "Type" ), m_methodCombo );
  qobject_cast<QVBoxLayout *>( methodSec->layout() )->addLayout( methodForm );
  mainLayout->addWidget( methodSec );

  m_stackedWidget = new QStackedWidget( this );

  auto *stretchGroup = SicnuUi::makeGroup( this, tr( "Contrast Stretch Parameters" ) );
  setupStretchOptions( new QVBoxLayout( stretchGroup ) );
  m_stackedWidget->addWidget( stretchGroup );

  auto *filterGroup = SicnuUi::makeGroup( this, tr( "Spatial Filtering Parameters" ) );
  setupFilterOptions( new QVBoxLayout( filterGroup ) );
  m_stackedWidget->addWidget( filterGroup );

  auto *ratioGroup = SicnuUi::makeGroup( this, tr( "Band Ratio / IHS Parameters" ) );
  setupBandRatioOptions( new QVBoxLayout( ratioGroup ) );
  m_stackedWidget->addWidget( ratioGroup );

  auto *speckleGroup = SicnuUi::makeGroup( this, tr( "Speckle Filtering Parameters" ) );
  setupSpeckleOptions( new QVBoxLayout( speckleGroup ) );
  m_stackedWidget->addWidget( speckleGroup );

  mainLayout->addWidget( m_stackedWidget, 1 );
  setupOutputRow( mainLayout );
  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "Ready" ) );
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
      tr("Linear / percent clip / std dev / histogram equalization.") ) );
    formLayout->addRow(tr("Type:"), m_stretchTypeCombo);

    m_clipPercentSpin = new QDoubleSpinBox();
    m_clipPercentSpin->setRange(0.1, 10.0);
    m_clipPercentSpin->setValue(2.0);
    m_clipPercentSpin->setSuffix("%");
    SicnuDialogHelp::tip( m_clipPercentSpin, tr( "Clip percentage at both tails; 1–2% is typical." ) );
    m_clipLabel = new QLabel(tr("Clip %:"));
    formLayout->addRow(m_clipLabel, m_clipPercentSpin);

    m_stddevMultSpin = new QDoubleSpinBox();
    m_stddevMultSpin->setRange(0.5, 5.0);
    m_stddevMultSpin->setValue(2.0);
    SicnuDialogHelp::tip( m_stddevMultSpin, tr( "Std-dev multiplier K; 2 is typical." ) );
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
    SicnuDialogHelp::tip( m_filterTypeCombo, tr( "Smoothing (mean/Gaussian/median) or edge (Sobel/Laplacian)." ) );
    formLayout->addRow(tr("Filter:"), m_filterTypeCombo);

    m_kernelSizeCombo = new QComboBox();
    m_kernelSizeCombo->addItem("3×3", 3);
    m_kernelSizeCombo->addItem("5×5", 5);
    m_kernelSizeCombo->addItem("7×7", 7);
    m_kernelSizeCombo->addItem("9×9", 9);
    SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "Convolution kernel size." ) );
    formLayout->addRow(tr("Kernel Size:"), m_kernelSizeCombo);

    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0.1, 10.0);
    m_sigmaSpin->setValue(1.0);
    m_sigmaSpin->setPrefix("σ = ");
    SicnuDialogHelp::tip( m_sigmaSpin, tr( "Gaussian filter std dev σ." ) );
    m_sigmaLabel = new QLabel(tr("Sigma:"));
    formLayout->addRow(m_sigmaLabel, m_sigmaSpin);

    m_customKernelEdit = new QLineEdit();
    m_customKernelEdit->setPlaceholderText(tr("e.g., 0 -1 0 -1 5 -1 0 -1 0 (3x3 row-major)"));
    SicnuDialogHelp::tip( m_customKernelEdit, tr( "Custom kernel: coefficients separated by spaces in row-major order." ) );
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
    SicnuDialogHelp::tip( m_ratioTypeCombo, tr( "Band ratio or IHS transform." ) );
    formLayout->addRow(tr("Type:"), m_ratioTypeCombo);

    m_band1Combo = new QComboBox();
    m_band2Combo = new QComboBox();
    m_band3Combo = new QComboBox();
    SicnuDialogHelp::tip( m_band1Combo, tr( "Ratio numerator or IHS red band." ) );
    SicnuDialogHelp::tip( m_band2Combo, tr( "Ratio denominator or IHS green band." ) );
    SicnuDialogHelp::tip( m_band3Combo, tr( "IHS blue band." ) );
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
    SicnuDialogHelp::tip( m_speckleTypeCombo, tr( "SAR speckle filtering: Lee / Frost / Kuan / Gamma-MAP." ) );
    formLayout->addRow(tr("Filter:"), m_speckleTypeCombo);

    m_speckleKernelCombo = new QComboBox();
    m_speckleKernelCombo->addItem("3×3", 3);
    m_speckleKernelCombo->addItem("5×5", 5);
    m_speckleKernelCombo->addItem("7×7", 7);
    SicnuDialogHelp::tip( m_speckleKernelCombo, tr( "Filter window size." ) );
    formLayout->addRow(tr("Kernel Size:"), m_speckleKernelCombo);

    m_noiseVarSpin = new QDoubleSpinBox();
    m_noiseVarSpin->setRange(0.001, 1.0);
    m_noiseVarSpin->setValue(0.1);
    m_noiseVarSpin->setDecimals(4);
    SicnuDialogHelp::tip( m_noiseVarSpin, tr( "Noise variance (non-Frost)." ) );
    m_noiseVarLabel = new QLabel(tr("Noise Variance:"));
    formLayout->addRow(m_noiseVarLabel, m_noiseVarSpin);

    m_dampingSpin = new QDoubleSpinBox();
    m_dampingSpin->setRange(0.1, 10.0);
    m_dampingSpin->setValue(1.0);
    SicnuDialogHelp::tip( m_dampingSpin, tr( "Frost damping factor." ) );
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

    // Thin client: the streaming enhancement dispatch runs as the
    // rs:image_enhancement operator through the Task Center — the same
    // execution path as CLI/MCP. The panel keeps only parameter collection.
    Json::Value params(Json::objectValue);
    params["input"] = sourcePath.toStdString();
    params["output"] = outPath.toStdString();
    switch (method) {
    case 1: {
        params["method"] = "filter";
        switch (filterType) {
        case 1: params["filterType"] = "gaussian"; break;
        case 2: params["filterType"] = "median"; break;
        case 3: params["filterType"] = "sobel"; break;
        case 4: params["filterType"] = "laplacian"; break;
        default: params["filterType"] = "mean"; break;
        }
        params["kernelSize"] = kernelSize;
        params["sigma"] = sigma;
        break;
    }
    case 2: {
        params["method"] = "ratio_ihs";
        params["transform"] = ratioType == 0 ? "ratio" : "ihs";
        params["band1"] = band1;
        params["band2"] = band2;
        params["band3"] = band3;
        break;
    }
    case 3: {
        params["method"] = "speckle";
        switch (speckleType) {
        case 1: params["speckleType"] = "frost"; break;
        case 2: params["speckleType"] = "kuan"; break;
        case 3: params["speckleType"] = "gamma_map"; break;
        default: params["speckleType"] = "lee"; break;
        }
        params["kernelSize"] = speckleKernel;
        params["noiseVariance"] = noiseVar;
        params["damping"] = damping;
        break;
    }
    default: {
        params["method"] = "stretch";
        switch (stretchType) {
        case 1: params["stretchType"] = "percent_clip"; break;
        case 2: params["stretchType"] = "stddev"; break;
        case 3: params["stretchType"] = "histogram_equalize"; break;
        default: params["stretchType"] = "linear"; break;
        }
        params["clipPercent"] = clipPercent;
        params["stddevK"] = stddevMult;
        break;
    }
    }
    runOperatorTask(QStringLiteral("rs:image_enhancement"), params);
}


