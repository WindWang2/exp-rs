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
    m_bandInfoLabel->setText( tr( "未选择栅格图层。" ) );
    return;
  }
  const int count = m_rasterLayer->bandCount();
  QStringList bandNames;
  for ( int i = 1; i <= std::min( count, 8 ); ++i )
  {
    const QString name = m_rasterLayer->bandName( i );
    bandNames.append( QStringLiteral( "b%1 (%2)" ).arg( i ).arg( name.isEmpty() ? tr( "波段 %1" ).arg( i ) : name ) );
  }
  if ( count > 8 )
    bandNames.append( QStringLiteral( "..." ) );
  m_bandInfoLabel->setText( tr( "有效波段 (%1 个)：%2" ).arg( count ).arg( bandNames.join( QStringLiteral( "，" ) ) ) );
}

void BandMathDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "bandMathInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待参与波段数学运算的栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BandMathDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );

  m_bandInfoLabel = SicnuUi::makeHintLabel( inputGroup, QString() );
  m_bandInfoLabel->setWordWrap( true );
  inputForm->addRow( tr( "可用变量" ), m_bandInfoLabel );

  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Math Expression Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "数学表达式" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_expressionEdit = new QLineEdit( paramGroup );
  m_expressionEdit->setObjectName( QStringLiteral( "bandMathExpressionEdit" ) );
  m_expressionEdit->setPlaceholderText( tr( "例如：(b1 - b2) / (b1 + b2) 或 b1 * 0.0001" ) );
  m_expressionEdit->setMinimumHeight( 32 );
  SicnuDialogHelp::tip( m_expressionEdit, tr(
    "波段运算表达式。波段写作 b1, b2…（从 1 起）。\n"
    "示例：(b1 - b2) / (b1 + b2)；b1 * 0.0001；sqrt(b1*b1 + b2*b2)；b1 > 0.4 ? 1 : 0\n"
    "支持：+ - * /、括号、比较(< > <= >= == !=)、逻辑(&& ||)、\n"
    "三元条件(b1 > x ? true : false)、数学函数(sin/cos/exp/ln/sqrt/abs/pow/min/max/pi…)" ) );
  form->addRow( tr( "运算公式" ), m_expressionEdit );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );
  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addWidget( SicnuUi::makeHintLabel(
    paramGroup, tr( "提示：常用植被指数 NDVI ≈ (b_nir − b_red) / (b_nir + b_red)；请注意避免除以零。" ) ) );

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
    QMessageBox::warning( this, dialogTitle(), tr( "请输入数学表达式。" ) );
    m_expressionEdit->setFocus();
    return;
  }

  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["expression"] = expression.toStdString();

  runOperatorTask( QStringLiteral( "rs:band_math" ), params );
}
