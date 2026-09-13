// src/app/dialogs/speckle_filter_dialog.cpp
#include "speckle_filter_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "processing/algorithms/image_enhancement_streaming.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

#include <qgsmessagelog.h>
#include <qgis.h>


SpeckleFilterDialog::SpeckleFilterDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void SpeckleFilterDialog::setRasterLayer( QgsRasterLayer *layer )
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

void SpeckleFilterDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void SpeckleFilterDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "speckleFilterInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the SAR raster layer for speckle filtering." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpeckleFilterDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Filter Parameters" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_filterTypeCombo = new QComboBox( paramGroup );
  m_filterTypeCombo->addItems( { tr( "Lee Filter" ), tr( "Frost Filter" ), tr( "Kuan Filter" ), tr( "Gamma-MAP Filter" ) } );
  SicnuDialogHelp::tip( m_filterTypeCombo, tr( "SAR speckle-reduction algorithm: Lee, Frost, Kuan or Gamma-MAP." ) );
  form->addRow( tr( "Filter" ), m_filterTypeCombo );

  m_kernelSizeCombo = new QComboBox( paramGroup );
  m_kernelSizeCombo->addItems( { tr( "3×3" ), tr( "5×5" ), tr( "7×7" ) } );
  m_kernelSizeCombo->setCurrentIndex( 1 );
  SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "Filter window: 3×3 keeps detail and edges; 7×7 smooths and denoises more strongly." ) );
  form->addRow( tr( "Filter Window" ), m_kernelSizeCombo );

  m_noiseVarLabel = new QLabel( tr( "Noise Variance" ), paramGroup );
  m_noiseVarSpin = new QDoubleSpinBox( paramGroup );
  m_noiseVarSpin->setRange( 0.001, 10.0 );
  m_noiseVarSpin->setValue( 1.0 );
  m_noiseVarSpin->setSingleStep( 0.1 );
  m_noiseVarSpin->setDecimals( 3 );
  SicnuDialogHelp::tip( m_noiseVarSpin, tr( "Estimated relative noise variance for the Lee / Kuan / Gamma-MAP models." ) );
  form->addRow( m_noiseVarLabel, m_noiseVarSpin );

  m_dampingLabel = new QLabel( tr( "Damping Factor" ), paramGroup );
  m_dampingSpin = new QDoubleSpinBox( paramGroup );
  m_dampingSpin->setRange( 0.1, 10.0 );
  m_dampingSpin->setValue( 2.0 );
  m_dampingSpin->setSingleStep( 0.5 );
  m_dampingSpin->setDecimals( 1 );
  SicnuDialogHelp::tip( m_dampingSpin, tr( "Exponential damping factor of the Frost filter: larger values give smoother output." ) );
  form->addRow( m_dampingLabel, m_dampingSpin );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_filterTypeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &SpeckleFilterDialog::onFilterTypeChanged );
  onFilterTypeChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void SpeckleFilterDialog::onFilterTypeChanged( int index )
{
  bool isFrost = ( index == 1 );
  m_dampingLabel->setVisible( isFrost );
  m_dampingSpin->setVisible( isFrost );
  m_noiseVarSpin->setVisible( !isFrost );
  if ( m_noiseVarLabel )
    m_noiseVarLabel->setVisible( !isFrost );
}

void SpeckleFilterDialog::onRun()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
    return;

  int kernelSize = 3;
  switch ( m_kernelSizeCombo->currentIndex() )
  {
    case 0: kernelSize = 3; break;
    case 1: kernelSize = 5; break;
    case 2: kernelSize = 7; break;
  }
  const int filterIndex = m_filterTypeCombo->currentIndex();
  const double noiseVar = m_noiseVarSpin->value();
  const double damping = m_dampingSpin->value();

  // Thin client (Platform 3.0): the dialog only serializes parameters; the
  // execution flows through the unified rs:sar_speckle operator (TaskCenter →
  // JobEngine → RSOperator → the same streaming kernels the dialog used to
  // embed). Method names follow the operator's schema vocabulary.
  QString method;
  switch ( filterIndex )
  {
    case 1: method = QStringLiteral( "frost" ); break;
    case 2: method = QStringLiteral( "kuan" ); break;
    case 3: method = QStringLiteral( "gamma_map" ); break;
    default: method = QStringLiteral( "lee" ); break;
  }

  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["method"] = method.toStdString();
  params["kernelSize"] = kernelSize;
  params["noiseVariance"] = noiseVar;
  params["dampingFactor"] = damping;
  params["band"] = 0; // every band, matching the legacy dialog behavior

  runOperatorTask( QStringLiteral( "rs:sar_speckle" ), params );
}
