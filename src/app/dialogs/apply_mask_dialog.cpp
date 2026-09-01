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
    mainLayout, tr( "输入数据与掩膜栅格" ) );
  inputGroup->setToolTip(
    tr( "将掩膜（1 = 被遮挡，0 = 有效）应用到产品栅格：被遮挡像元在所有波段置为 "
        "NoData，得到分析就绪影像。" ) );
  auto *form = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );

  m_inputLayerCombo = new QComboBox( inputGroup );
  m_inputLayerCombo->setObjectName( QStringLiteral( "applyMaskInputCombo" ) );
  SicnuDialogHelp::tip( m_inputLayerCombo, tr( "待掩膜的产品栅格（多波段）。" ) );
  form->addRow( tr( "产品栅格" ), m_inputLayerCombo );

  m_maskLayerCombo = new QComboBox( inputGroup );
  m_maskLayerCombo->setObjectName( QStringLiteral( "applyMaskMaskCombo" ) );
  SicnuDialogHelp::tip( m_maskLayerCombo, tr(
    "二值掩膜栅格（第 1 波段，>0 视为被遮挡）。通常是“QA 掩膜”对话框的输出；"
    "网格不同且 CRS 相同时会自动最近邻对齐。" ) );
  form->addRow( tr( "掩膜栅格" ), m_maskLayerCombo );

  QGroupBox *optGroup = setupAdvancedGroup(
    mainLayout, tr( "高级选项与对齐" ) );
  optGroup->setToolTip(
    tr( "默认复用输入波段自身的 NoData；仅在输入波段未定义 NoData 时才需要指定。" ) );
  auto *optForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( optGroup->layout() )->addLayout( optForm );

  m_useNoDataCheck = new QCheckBox( tr( "指定输出 NoData 值" ), optGroup );
  m_useNoDataCheck->setObjectName( QStringLiteral( "applyMaskNoDataCheck" ) );
  SicnuDialogHelp::tip( m_useNoDataCheck, tr(
    "勾选后，被遮挡像元写入该 NoData 值（替代输入波段自带 NoData）。"
    "输入波段未定义 NoData 时此项必填。" ) );

  m_noDataSpin = new QDoubleSpinBox( optGroup );
  m_noDataSpin->setObjectName( QStringLiteral( "applyMaskNoDataSpin" ) );
  m_noDataSpin->setRange( -1e9, 1e9 );
  m_noDataSpin->setDecimals( 2 );
  m_noDataSpin->setValue( -9999.0 );
  m_noDataSpin->setEnabled( false );
  SicnuDialogHelp::tip( m_noDataSpin, tr( "被掩膜遮挡像元的 NoData 替换填充值" ) );
  auto *nodataRow = new QHBoxLayout;
  nodataRow->addWidget( m_useNoDataCheck );
  nodataRow->addWidget( m_noDataSpin, 1 );
  optForm->addRow( tr( "NoData 覆盖" ), nodataRow );

  m_alignMaskCheck = new QCheckBox( tr( "自动对齐掩膜网格（最近邻，仅限相同 CRS）" ), optGroup );
  m_alignMaskCheck->setObjectName( QStringLiteral( "applyMaskAlignCheck" ) );
  m_alignMaskCheck->setChecked( true );
  SicnuDialogHelp::tip( m_alignMaskCheck, tr(
    "掩膜网格与产品不同（如 20 m SCL 对 10 m 产品）时，用最近邻采样把掩膜对齐到产品网格。"
    "CRS 不一致始终报错，不会自动纠正。" ) );
  optForm->addRow( tr( "网格对齐" ), m_alignMaskCheck );

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
    QMessageBox::warning( this, dialogTitle(), tr( "请指定输出文件。" ) );
    return false;
  }
  if ( m_inputLayerCombo->currentData().toString().isEmpty()
       || m_maskLayerCombo->currentData().toString().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择产品栅格与掩膜栅格。" ) );
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
    QMessageBox::warning( this, dialogTitle(), tr( "所选栅格图层无效。" ) );
    return;
  }

  setRasterLayer( input );
  runOperatorTask( QStringLiteral( "rs:apply_mask" ), buildParams() );
}
