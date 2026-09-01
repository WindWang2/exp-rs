// src/app/dialogs/atmospheric_dialog.cpp — Atmospheric correction dialog
#include "atmospheric_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QFileInfo>
#include <QMap>

#include "processing/algorithms/radiometric_calibration.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

AtmosphericDialog::AtmosphericDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "大气校正" ) );
  setupUi();
}

void AtmosphericDialog::setRasterLayer( QgsRasterLayer *layer )
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
  populateBandCombo();
  refreshMetadata();
}

void AtmosphericDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void AtmosphericDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Data Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "输入数据" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "atmosphericInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "选择待执行大气校正的栅格图层。" ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &AtmosphericDialog::onLayerChanged );
  inputForm->addRow( tr( "输入栅格" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "校正参数" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_methodCombo = new QComboBox( paramGroup );
  m_methodCombo->addItem( tr( "DN → 辐射亮度" ), QStringLiteral( "dn_to_radiance" ) );
  m_methodCombo->addItem( tr( "DOS1 暗目标减法" ), QStringLiteral( "dos1" ) );
  m_methodCombo->addItem( tr( "DOS2（含透过率）" ), QStringLiteral( "dos2" ) );
  m_methodCombo->addItem( tr( "QUAC 快速大气校正" ), QStringLiteral( "quac" ) );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "• DN->辐射：L=gain×DN+bias\n• DOS1：暗目标减法\n• DOS2：DOS1 + 透过率\n• QUAC：基于图像统计的全波段快速校正" ) );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &AtmosphericDialog::onMethodChanged );
  form->addRow( tr( "校正方法" ), m_methodCombo );

  m_bandCombo = new QComboBox( paramGroup );
  SicnuDialogHelp::tip( m_bandCombo, tr( "要校正的波段号。" ) );
  connect( m_bandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &AtmosphericDialog::refreshMetadata );
  form->addRow( tr( "目标波段" ), m_bandCombo );
  m_bandLabel = qobject_cast<QLabel *>( form->labelForField( m_bandCombo ) );

  m_gainSpin = new QDoubleSpinBox( paramGroup );
  m_gainSpin->setRange( 0.0001, 1000.0 );
  m_gainSpin->setDecimals( 6 );
  m_gainSpin->setValue( 0.01 );
  SicnuDialogHelp::tip( m_gainSpin, tr( "辐射定标增益 gain。" ) );
  form->addRow( tr( "增益 Gain" ), m_gainSpin );
  m_gainLabel = qobject_cast<QLabel *>( form->labelForField( m_gainSpin ) );

  m_biasSpin = new QDoubleSpinBox( paramGroup );
  m_biasSpin->setRange( -1000.0, 1000.0 );
  m_biasSpin->setDecimals( 6 );
  m_biasSpin->setValue( 0.0 );
  SicnuDialogHelp::tip( m_biasSpin, tr( "辐射定标偏置 bias。" ) );
  form->addRow( tr( "偏置 Bias" ), m_biasSpin );
  m_biasLabel = qobject_cast<QLabel *>( form->labelForField( m_biasSpin ) );

  m_airmassLabel = new QLabel( tr( "气团 Airmass" ), paramGroup );
  m_airmassSpin = new QDoubleSpinBox( paramGroup );
  m_airmassSpin->setRange( 1.0, 10.0 );
  m_airmassSpin->setDecimals( 2 );
  m_airmassSpin->setValue( 1.0 );
  m_airmassSpin->setVisible( false );
  m_airmassLabel->setVisible( false );
  SicnuDialogHelp::tip( m_airmassSpin, tr( "气团（仅 DOS2），通常≥1。" ) );
  form->addRow( m_airmassLabel, m_airmassSpin );

  m_metadataStatusLabel = SicnuUi::makeHintLabel( paramGroup, QString() );
  m_metadataStatusLabel->setWordWrap( true );
  form->addRow( QString(), m_metadataStatusLabel );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  // Manual coefficient edits override the auto-resolved values.
  connect( m_gainSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
           this, &AtmosphericDialog::onCoefficientChanged );
  connect( m_biasSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
           this, &AtmosphericDialog::onCoefficientChanged );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void AtmosphericDialog::populateBandCombo()
{
  m_bandCombo->blockSignals( true );
  m_bandCombo->clear();
  if ( m_rasterLayer && m_rasterLayer->isValid() )
  {
    const int bandCount = m_rasterLayer->bandCount();
    for ( int i = 1; i <= bandCount; ++i )
      m_bandCombo->addItem( tr( "波段 %1" ).arg( i ), i );
  }
  m_bandCombo->blockSignals( false );
  refreshMetadata();
}

void AtmosphericDialog::refreshMetadata()
{
  m_coefficientsModified = false;
  m_resolvedMetadataPath.clear();
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    m_metadataStatusLabel->clear();
    return;
  }

  const QString metadataPath =
    RadiometricCalibration::autoDetectMetadataFile( m_rasterLayer->source() );
  if ( metadataPath.isEmpty() )
  {
    m_metadataStatusLabel->setText(
      tr( "未找到传感器元数据文件；请手动输入 gain/bias。" ) );
    return;
  }

  const int band = m_bandCombo->currentData().toInt();
  if ( band <= 0 )
    return;

  // Full band-name map (descriptions first, synthetic B%1 fallback), matching
  // RadiometricCalibration::processFile: a partial map previously degraded to
  // identity auto-discovery for the other bands and could silently apply
  // wrong-band coefficients on non-identity stacks (#448).
  QMap<int, QString> bandNames;
  GdalDatasetWrapper ds;
  if ( ds.open( m_rasterLayer->source() ) )
  {
    for ( int b = 1; b <= ds.bandCount(); ++b )
    {
      const QString desc = ds.bandDescription( b );
      bandNames.insert( b, desc.isEmpty() ? QStringLiteral( "B%1" ).arg( b ) : desc );
    }
  }

  RadiometricCalibration::CalibrationMetadata meta;
  QString error;
  if ( !RadiometricCalibration::loadMetadata( m_rasterLayer->source(), metadataPath,
                                              bandNames, &meta, &error )
       || !meta.bands.contains( band ) )
  {
    m_metadataStatusLabel->setText(
      tr( "已探测到 %1，但波段 %2 无系数：%3" )
        .arg( QFileInfo( metadataPath ).fileName() )
        .arg( band )
        .arg( error.isEmpty() ? tr( "请手动输入 gain/bias。" ) : error ) );
    return;
  }

  const auto &c = meta.bands.value( band );
  m_gainSpin->blockSignals( true );
  m_biasSpin->blockSignals( true );
  m_gainSpin->setValue( c.radianceGain );
  m_biasSpin->setValue( c.radianceBias );
  m_gainSpin->blockSignals( false );
  m_biasSpin->blockSignals( false );

  m_resolvedMetadataPath = metadataPath;
  m_metadataStatusLabel->setText(
    tr( "已从 %1 自动填充 gain/bias（可手动修改）。" )
      .arg( QFileInfo( metadataPath ).fileName() ) );
}

void AtmosphericDialog::onCoefficientChanged()
{
  m_coefficientsModified = true;
  if ( !m_resolvedMetadataPath.isEmpty() )
    m_metadataStatusLabel->setText(
      tr( "使用手动 gain/bias（元数据 %1 仍可用于其他波段）。" )
        .arg( QFileInfo( m_resolvedMetadataPath ).fileName() ) );
}

void AtmosphericDialog::onMethodChanged( int index )
{
  const bool showAirmass = ( index == 2 );
  m_airmassSpin->setVisible( showAirmass );
  m_airmassLabel->setVisible( showAirmass );

  // QUAC (index 3) processes all bands jointly and needs no gain/bias/airmass.
  const bool isQuac = ( index == 3 );
  m_bandCombo->setVisible( !isQuac );
  if ( m_bandLabel ) m_bandLabel->setVisible( !isQuac );
  m_gainSpin->setVisible( !isQuac );
  if ( m_gainLabel ) m_gainLabel->setVisible( !isQuac );
  m_biasSpin->setVisible( !isQuac );
  if ( m_biasLabel ) m_biasLabel->setVisible( !isQuac );
}

void AtmosphericDialog::onRun()
{
  const int bandNum = m_bandCombo->currentData().toInt();
  const QString method = m_methodCombo->currentData().toString();
  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["band"] = bandNum;
  params["method"] = method.toStdString();
  params["airmass"] = m_airmassSpin->value();
  if ( m_coefficientsModified )
  {
    // Explicit values win; the operator resolves from metadata otherwise.
    params["gain"] = m_gainSpin->value();
    params["bias"] = m_biasSpin->value();
  }
  else if ( !m_resolvedMetadataPath.isEmpty() )
  {
    params["metadata_path"] = m_resolvedMetadataPath.toStdString();
  }
  runOperatorTask( QStringLiteral( "rs:atmospheric_correction" ), params );
}
