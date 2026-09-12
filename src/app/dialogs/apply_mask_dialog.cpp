// src/app/dialogs/apply_mask_dialog.cpp — Apply Mask dialog
#include "apply_mask_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <qgsproject.h>

ApplyMaskDialog::ApplyMaskDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 480 );
  setupUi();
}

void ApplyMaskDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "Input Data and Mask Raster" ) );
  inputGroup->setToolTip(
    tr( "Applies the mask (1 = obscured, 0 = valid) to the product raster: obscured pixels are set to "
        tr("NoData, yielding an analysis-ready image.") ) );
  auto *form = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );

  m_inputLayerCombo = new QComboBox( inputGroup );
  m_inputLayerCombo->setObjectName( QStringLiteral( "applyMaskInputCombo" ) );
  SicnuDialogHelp::tip( m_inputLayerCombo, tr( "Product raster to mask (multiband)." ) );
  form->addRow( tr( "Product Raster" ), m_inputLayerCombo );

  m_maskLayerCombo = new QComboBox( inputGroup );
  m_maskLayerCombo->setObjectName( QStringLiteral( "applyMaskMaskCombo" ) );
  SicnuDialogHelp::tip( m_maskLayerCombo, tr(
    tr("A binary mask raster (band 1; > 0 means obscured). Usually the output of the 'QA Mask' dialog;")
    tr("With different grids but the same CRS, nearest-neighbour alignment happens automatically.") ) );
  form->addRow( tr( "Mask Raster" ), m_maskLayerCombo );

  QGroupBox *optGroup = setupAdvancedGroup(
    mainLayout, tr( "Advanced Options and Alignment" ) );
  optGroup->setToolTip(
    tr( "By default the input band's own NoData is reused; specify a value only when the input band has none." ) );
  auto *optForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( optGroup->layout() )->addLayout( optForm );

  m_useNoDataCheck = new QCheckBox( tr( "Specify the output NoData value" ), optGroup );
  m_useNoDataCheck->setObjectName( QStringLiteral( "applyMaskNoDataCheck" ) );
  SicnuDialogHelp::tip( m_useNoDataCheck, tr(
    tr("When ticked, obscured pixels are written with this NoData value (instead of the input band's own NoData).")
    tr("Required when the input bands define no NoData.") ) );

  m_noDataSpin = new QDoubleSpinBox( optGroup );
  m_noDataSpin->setObjectName( QStringLiteral( "applyMaskNoDataSpin" ) );
  m_noDataSpin->setRange( -1e9, 1e9 );
  m_noDataSpin->setDecimals( 2 );
  m_noDataSpin->setValue( -9999.0 );
  m_noDataSpin->setEnabled( false );
  SicnuDialogHelp::tip( m_noDataSpin, tr( "NoData replacement fill value for masked pixels" ) );
  auto *nodataRow = new QHBoxLayout;
  nodataRow->addWidget( m_useNoDataCheck );
  nodataRow->addWidget( m_noDataSpin, 1 );
  optForm->addRow( tr( "NoData Override" ), nodataRow );

  m_alignMaskCheck = new QCheckBox( tr( "Align the mask grid automatically (nearest neighbour, same CRS only)" ), optGroup );
  m_alignMaskCheck->setObjectName( QStringLiteral( "applyMaskAlignCheck" ) );
  m_alignMaskCheck->setChecked( true );
  SicnuDialogHelp::tip( m_alignMaskCheck, tr(
    tr("When the mask grid differs from the product (e.g. a 20 m SCL against a 10 m product), nearest-neighbour sampling aligns the mask to the product grid.")
    tr("A CRS mismatch always raises an error; it is never corrected automatically.") ) );
  optForm->addRow( tr( "Grid Alignment" ), m_alignMaskCheck );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_inputLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ApplyMaskDialog::onInputLayerChanged );
  connect( m_useNoDataCheck, &QCheckBox::toggled, m_noDataSpin, &QDoubleSpinBox::setEnabled );
  connect( m_outputEdit, &QLineEdit::textEdited, this, [this]( const QString & ) {
    m_outputTouched = true;
  } );

  populateLayers();
}

void ApplyMaskDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  preselectInputLayer( layer );
}

void ApplyMaskDialog::preselectInputLayer( QgsRasterLayer *layer )
{
  if ( !layer || !m_inputLayerCombo )
    return;
  const int index = m_inputLayerCombo->findData( layer->id() );
  if ( index >= 0 )
    m_inputLayerCombo->setCurrentIndex( index );
  onInputLayerChanged();
}

void ApplyMaskDialog::populateLayers()
{
  m_inputLayerCombo->blockSignals( true );
  m_maskLayerCombo->blockSignals( true );
  m_inputLayerCombo->clear();
  m_maskLayerCombo->clear();
  const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    auto *rasterLayer = qobject_cast<QgsRasterLayer *>( it.value() );
    if ( rasterLayer && rasterLayer->isValid() )
    {
      m_inputLayerCombo->addItem( rasterLayer->name(), rasterLayer->id() );
      m_maskLayerCombo->addItem( rasterLayer->name(), rasterLayer->id() );
    }
  }
  m_inputLayerCombo->blockSignals( false );
  m_maskLayerCombo->blockSignals( false );
  preselectInputLayer( m_rasterLayer );
}

void ApplyMaskDialog::onInputLayerChanged()
{
  // Suggest a sibling output path only while the user has not chosen one yet.
  if ( m_outputTouched || !m_outputEdit )
    return;
  const QString id = m_inputLayerCombo->currentData().toString();
  auto *rl = qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( id ) );
  if ( !rl )
    return;
  const QFileInfo info( rl->source() );
  m_outputEdit->setText( info.absolutePath() + QLatin1Char( '/' )
                         + info.completeBaseName() + QStringLiteral( "_masked.tif" ) );
}

Json::Value ApplyMaskDialog::buildParams() const
{
  auto *input = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_inputLayerCombo->currentData().toString() ) );
  auto *mask = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_maskLayerCombo->currentData().toString() ) );

  Json::Value params( Json::objectValue );
  params["input"] = input ? input->source().toStdString() : std::string();
  params["mask"] = mask ? mask->source().toStdString() : std::string();
  params["output"] = outputPath().toStdString();
  if ( m_useNoDataCheck->isChecked() )
    params["no_data"] = m_noDataSpin->value();
  params["align_mask"] = m_alignMaskCheck->isChecked();
  return params;
}

bool ApplyMaskDialog::validateInputs()
{
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Specify the output file." ) );
    return false;
  }
  if ( m_inputLayerCombo->currentData().toString().isEmpty()
       || m_maskLayerCombo->currentData().toString().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select the product raster and the mask raster." ) );
    return false;
  }
  return true;
}

void ApplyMaskDialog::onRun()
{
  if ( !validateInputs() )
    return;

  auto *input = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_inputLayerCombo->currentData().toString() ) );
  auto *mask = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_maskLayerCombo->currentData().toString() ) );
  if ( !input || !input->isValid() || !mask || !mask->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The selected raster layer is invalid." ) );
    return;
  }

  setRasterLayer( input );
  runOperatorTask( QStringLiteral( "rs:apply_mask" ), buildParams() );
}
