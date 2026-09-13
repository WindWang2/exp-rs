// src/app/dialogs/spatial_filter_dialog.cpp
#include "spatial_filter_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

SpatialFilterDialog::SpatialFilterDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void SpatialFilterDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  if ( m_layerCombo && layer )
  {
    const int idx = m_layerCombo->findData( layer->id() );
    if ( idx >= 0 && m_layerCombo->currentIndex() != idx )
    {
      m_layerCombo->blockSignals( true );
      m_layerCombo->setCurrentIndex( idx );
      m_layerCombo->blockSignals( false );
    }
  }
}

void SpatialFilterDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void SpatialFilterDialog::onFilterTypeChanged( int /*index*/ )
{
  const QString opId = m_filterTypeCombo->currentData().toString();
  const bool isGaussian = ( opId == QStringLiteral( "opencv:gaussian_blur" ) );
  if ( m_sigmaLabel && m_sigmaSpin )
  {
    m_sigmaLabel->setVisible( isGaussian );
    m_sigmaSpin->setVisible( isGaussian );
  }
}

void SpatialFilterDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "spatialFilterInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the raster layer for spatial filtering." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpatialFilterDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Filter Parameters" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_filterTypeCombo = new QComboBox( paramGroup );
  m_filterTypeCombo->addItem( tr( "Mean Filter" ), QStringLiteral( "opencv:mean_blur" ) );
  m_filterTypeCombo->addItem( tr( "Gaussian Filter" ), QStringLiteral( "opencv:gaussian_blur" ) );
  m_filterTypeCombo->addItem( tr( "Median Filter" ), QStringLiteral( "opencv:median_blur" ) );
  m_filterTypeCombo->addItem( tr( "Sobel Edge Detection" ), QStringLiteral( "opencv:sobel" ) );
  m_filterTypeCombo->addItem( tr( "Laplacian Edge Enhancement" ), QStringLiteral( "opencv:laplacian" ) );
  SicnuDialogHelp::tip( m_filterTypeCombo, tr(
    "• Mean / Gaussian / median: smoothing and denoising\n"
    "• Sobel / Laplacian: edge detection and sharpening\n"
    "The median filter suppresses salt-and-pepper noise while preserving edges remarkably well.")  );
  connect( m_filterTypeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpatialFilterDialog::onFilterTypeChanged );
  form->addRow( tr( "Filter Type" ), m_filterTypeCombo );

  m_kernelSizeCombo = new QComboBox( paramGroup );
  m_kernelSizeCombo->addItem( tr( "3×3" ), 3 );
  m_kernelSizeCombo->addItem( tr( "5×5" ), 5 );
  m_kernelSizeCombo->addItem( tr( "7×7" ), 7 );
  SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "Convolution filter window size; larger windows smooth more or respond over a wider range." ) );
  form->addRow( tr( "Window Size" ), m_kernelSizeCombo );

  m_sigmaLabel = new QLabel( tr( "Gaussian Std Dev Sigma" ), paramGroup );
  m_sigmaSpin = new QDoubleSpinBox( paramGroup );
  m_sigmaSpin->setRange( 0.1, 50.0 );
  m_sigmaSpin->setValue( 1.0 );
  m_sigmaSpin->setSingleStep( 0.5 );
  m_sigmaSpin->setDecimals( 2 );
  SicnuDialogHelp::tip( m_sigmaSpin, tr( "Spatial std dev (Sigma) of the Gaussian kernel; defaults to 1.0." ) );
  form->addRow( m_sigmaLabel, m_sigmaSpin );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  onFilterTypeChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void SpatialFilterDialog::onRun()
{
  const QString operatorId = m_filterTypeCombo->currentData().toString();
  const int kernelSize = m_kernelSizeCombo->currentData().toInt();

  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["kernelSize"] = kernelSize;
  if ( operatorId == QLatin1String( "opencv:gaussian_blur" ) )
  {
    params["sigma"] = m_sigmaSpin ? m_sigmaSpin->value() : 1.0;
  }
  if ( operatorId == QLatin1String( "opencv:sobel" ) )
  {
    params["dx"] = 1;
    params["dy"] = 1;
  }
  runOperatorTask( operatorId, params );
}
