// src/app/dialogs/change_detection_dialog.cpp
#include "change_detection_dialog.h"
#include "comparison_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>

#include <qgsproject.h>

ChangeDetectionDialog::ChangeDetectionDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "变化检测" ) );
  setMinimumWidth( 480 );
  setupUi();
}

void ChangeDetectionDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *inputSec = SicnuUi::makeSection(
    this, tr( "双时相输入" ),
    tr( "前后时相须几何对齐。可先做影像配准。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_beforeLayerCombo = new QComboBox( inputSec );
  m_afterLayerCombo = new QComboBox( inputSec );
  m_beforeBandCombo = new QComboBox( inputSec );
  m_afterBandCombo = new QComboBox( inputSec );
  SicnuDialogHelp::tip( m_beforeLayerCombo, tr( "变化前（较早）影像。" ) );
  SicnuDialogHelp::tip( m_afterLayerCombo, tr( "变化后（较晚）影像。" ) );
  SicnuDialogHelp::tip( m_beforeBandCombo, tr( "前期波段。" ) );
  SicnuDialogHelp::tip( m_afterBandCombo, tr( "后期波段。" ) );
  form->addRow( tr( "前期影像" ), m_beforeLayerCombo );
  form->addRow( tr( "前期波段" ), m_beforeBandCombo );
  form->addRow( tr( "后期影像" ), m_afterLayerCombo );
  form->addRow( tr( "后期波段" ), m_afterBandCombo );

  // Dual-view interpretation aid (DoD: synchronized viewports / swipe where
  // they improve interpretation): open the comparison dialog prefilled with
  // the selected before/after rasters.
  auto *compareButton = new QPushButton( tr( "双视图对比..." ), inputSec );
  compareButton->setObjectName( QStringLiteral( "changeCompareButton" ) );
  SicnuDialogHelp::tip( compareButton, tr(
    "打开并排对比视图（分割线/Swipe + 闪烁），目视检查配准与变化。" ) );
  connect( compareButton, &QPushButton::clicked,
           this, &ChangeDetectionDialog::openComparisonPreview );
  qobject_cast<QVBoxLayout *>( inputSec->layout() )->addWidget( compareButton );

  qobject_cast<QVBoxLayout *>( inputSec->layout() )->addLayout( form );
  mainLayout->addWidget( inputSec );

  QFrame *methodSec = SicnuUi::makeSection(
    this, tr( "检测方法" ),
    tr( "差值 / 归一化差值 / 变化掩膜。" ) );
  auto *methodForm = new QFormLayout();
  methodForm->setContentsMargins( 0, 0, 0, 0 );
  methodForm->setHorizontalSpacing( 12 );

  m_methodCombo = new QComboBox( methodSec );
  m_methodCombo->addItem( tr( "差值 Difference" ), QStringLiteral( "difference" ) );
  m_methodCombo->addItem( tr( "归一化差值" ), QStringLiteral( "normalized_difference" ) );
  m_methodCombo->addItem( tr( "变化掩膜" ), QStringLiteral( "change_mask" ) );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "• 差值：后−前\n• 归一化差值：(后−前)/(后+前)\n• 掩膜：|差值|≥阈值" ) );
  methodForm->addRow( tr( "方法" ), m_methodCombo );

  m_thresholdLabel = new QLabel( tr( "阈值" ), methodSec );
  m_thresholdSpin = new QDoubleSpinBox( methodSec );
  m_thresholdSpin->setRange( 0.0, 10000.0 );
  m_thresholdSpin->setDecimals( 2 );
  m_thresholdSpin->setValue( 10.0 );
  m_thresholdSpin->setVisible( false );
  m_thresholdLabel->setVisible( false );
  SicnuDialogHelp::tip( m_thresholdSpin, tr( "变化掩膜阈值。" ) );
  methodForm->addRow( m_thresholdLabel, m_thresholdSpin );
  qobject_cast<QVBoxLayout *>( methodSec->layout() )->addLayout( methodForm );
  mainLayout->addWidget( methodSec );

  setupOutputRow( mainLayout );
  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
  mainLayout->addWidget( m_statusLabel );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_beforeLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::updateBandSelectors );
  connect( m_afterLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::updateBandSelectors );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::onMethodChanged );

  populateLayers();
}

void ChangeDetectionDialog::populateLayers()
{
  m_beforeLayerCombo->clear();
  m_afterLayerCombo->clear();
  const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer *>( it.value() );
    if ( rasterLayer && rasterLayer->isValid() )
    {
      m_beforeLayerCombo->addItem( rasterLayer->name(), rasterLayer->id() );
      m_afterLayerCombo->addItem( rasterLayer->name(), rasterLayer->id() );
    }
  }
  updateBandSelectors();
}

void ChangeDetectionDialog::updateBandSelectors()
{
  auto fillBands = []( QComboBox *layerCombo, QComboBox *bandCombo ) {
    bandCombo->clear();
    const QString id = layerCombo->currentData().toString();
    auto *rl = qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( id ) );
    if ( !rl )
      return;
    for ( int i = 1; i <= rl->bandCount(); ++i )
      bandCombo->addItem( tr( "波段 %1" ).arg( i ), i );
  };
  fillBands( m_beforeLayerCombo, m_beforeBandCombo );
  fillBands( m_afterLayerCombo, m_afterBandCombo );
}

void ChangeDetectionDialog::onMethodChanged( int index )
{
  const bool showTh = ( index == 2 );
  m_thresholdSpin->setVisible( showTh );
  m_thresholdLabel->setVisible( showTh );
}

void ChangeDetectionDialog::openComparisonPreview()
{
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请先选择前后时相影像。" ) );
    return;
  }

  ComparisonDialog dialog( this );
  dialog.setLeftLayer( before );
  dialog.setRightLayer( after );
  dialog.exec();
}

bool ChangeDetectionDialog::validateInputs()
{
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请指定输出文件。" ) );
    return false;
  }
  if ( m_beforeLayerCombo->currentData().toString().isEmpty()
       || m_afterLayerCombo->currentData().toString().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择前期与后期影像。" ) );
    return false;
  }
  return true;
}

void ChangeDetectionDialog::onRun()
{
  if ( !validateInputs() )
    return;

  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "前期影像无效。" ) );
    return;
  }
  if ( !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "后期影像无效。" ) );
    return;
  }

  setRasterLayer( before );
  Json::Value params( Json::objectValue );
  params["before"] = before->source().toStdString();
  params["after"] = after->source().toStdString();
  params["beforeBand"] = m_beforeBandCombo->currentData().toInt();
  params["afterBand"] = m_afterBandCombo->currentData().toInt();
  params["method"] = m_methodCombo->currentData().toString().toStdString();
  params["threshold"] = m_thresholdSpin->value();
  params["output"] = outputPath().toStdString();
  m_statusLabel->setText( tr( "运行中…" ) );
  runOperatorTask( QStringLiteral( "rs:change_detection" ), params );
}
