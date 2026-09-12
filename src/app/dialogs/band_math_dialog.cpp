// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>

BandMathDialog::BandMathDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 520 );
  setupUi();
}

void BandMathDialog::setRasterLayer( QgsRasterLayer *layer )
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
  updateBandInfo();
}

void BandMathDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void BandMathDialog::updateBandInfo()
{
  if ( !m_bandInfoLabel )
    return;
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    m_bandInfoLabel->setText( tr( "No raster layer selected." ) );
    return;
  }
  const int count = m_rasterLayer->bandCount();
  QStringList bandNames;
  for ( int i = 1; i <= std::min( count, 8 ); ++i )
  {
    const QString name = m_rasterLayer->bandName( i );
    bandNames.append( QStringLiteral( "b%1 (%2)" ).arg( i ).arg( name.isEmpty() ? tr( "Band %1" ).arg( i ) : name ) );
  }
  if ( count > 8 )
    bandNames.append( QStringLiteral( "..." ) );
  m_bandInfoLabel->setText( tr( "Valid bands (%1): %2" ).arg( count ).arg( bandNames.join( QStringLiteral( "，" ) ) ) );
}

void BandMathDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "bandMathInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the raster layers for the band math operation." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BandMathDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );

  m_bandInfoLabel = SicnuUi::makeHintLabel( inputGroup, QString() );
  m_bandInfoLabel->setWordWrap( true );
  inputForm->addRow( tr( "Available Variables" ), m_bandInfoLabel );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Math Expression Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Mathematical Expression" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_expressionEdit = new QLineEdit( paramGroup );
  m_expressionEdit->setObjectName( QStringLiteral( "bandMathExpressionEdit" ) );
  m_expressionEdit->setPlaceholderText( tr( "e.g. (b1 - b2) / (b1 + b2) or b1 * 0.0001" ) );
  m_expressionEdit->setMinimumHeight( 32 );
  SicnuDialogHelp::tip( m_expressionEdit, tr(
    tr("Band math expression. Bands are written b1, b2, ... (starting at 1).\n")
    tr("Examples: (b1 - b2) / (b1 + b2); b1 * 0.0001; sqrt(b1*b1 + b2*b2); b1 > 0.4 ? 1 : 0\n")
    tr("Supports: + - * /, parentheses, comparisons (< > <= >= == !=), logic (&& ||),\n")
    tr("ternary conditionals (b1 > x ? true : false) and math functions (sin/cos/exp/ln/sqrt/abs/pow/min/max/pi...)") ) );
  form->addRow( tr( "Formula" ), m_expressionEdit );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );
  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addWidget( SicnuUi::makeHintLabel(
    paramGroup, tr( "Tip: the common vegetation index NDVI ≈ (b_nir − b_red) / (b_nir + b_red); avoid division by zero." ) ) );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
  else
    updateBandInfo();
}

void BandMathDialog::onRun()
{
  const QString expression = m_expressionEdit->text().trimmed();
  if ( expression.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Enter a mathematical expression." ) );
    m_expressionEdit->setFocus();
    return;
  }

  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["expression"] = expression.toStdString();

  runOperatorTask( QStringLiteral( "rs:band_math" ), params );
}
