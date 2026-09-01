// src/app/dialogs/change_detection_dialog.cpp
#include "change_detection_dialog.h"
#include "comparison_dialog.h"
#include "dialog_help_catalog.h"
#include "widgets/raster_layer_combo.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
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

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "双时相输入数据" ) );
  inputGroup->setToolTip(
    tr( "前后时相须完成高精度几何配准与辐射归一化。" ) );
  auto *form = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );

  m_beforeLayerCombo = new RasterLayerCombo( inputGroup );
  m_beforeLayerCombo->setObjectName( QStringLiteral( "cdBeforeCombo" ) );
  m_afterLayerCombo = new RasterLayerCombo( inputGroup );
  m_afterLayerCombo->setObjectName( QStringLiteral( "cdAfterCombo" ) );
  m_beforeBandCombo = new QComboBox( inputGroup );
  m_afterBandCombo = new QComboBox( inputGroup );
  SicnuDialogHelp::tip( m_beforeLayerCombo, tr( "变化前（较早）时相栅格影像。" ) );
  SicnuDialogHelp::tip( m_afterLayerCombo, tr( "变化后（较晚）时相栅格影像。" ) );
  SicnuDialogHelp::tip( m_beforeBandCombo, tr( "前期影像参与比较的波段。" ) );
  SicnuDialogHelp::tip( m_afterBandCombo, tr( "后期影像参与比较的波段。" ) );
  form->addRow( tr( "前期影像" ), m_beforeLayerCombo );
  form->addRow( tr( "前期波段" ), m_beforeBandCombo );
  form->addRow( tr( "后期影像" ), m_afterLayerCombo );
  form->addRow( tr( "后期波段" ), m_afterBandCombo );

  // Dual-view interpretation aid (DoD: synchronized viewports / swipe where
  // they improve interpretation): open the comparison dialog prefilled with
  // the selected before/after rasters.
  auto *compareButton = new QPushButton( tr( "双视图对比…" ), inputGroup );
  compareButton->setObjectName( QStringLiteral( "changeCompareButton" ) );
  SicnuUi::markSecondary( compareButton );
  SicnuDialogHelp::tip( compareButton, tr(
    "打开并排对比视图（分割线/Swipe + 闪烁），目视检查配准与变化。" ) );
  connect( compareButton, &QPushButton::clicked,
           this, &ChangeDetectionDialog::openComparisonPreview );
  form->addRow( QString(), compareButton );

  QGroupBox *methodGroup = setupParamGroup(
    mainLayout, tr( "检测方法与掩膜选项" ) );
  methodGroup->setToolTip(
    tr( "支持差值法、归一化差值法、比值法、CVA 变化向量分析与 MAD 多变量变化检测。" ) );
  auto *methodForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( methodGroup->layout() )->addLayout( methodForm );

  m_methodCombo = new QComboBox( methodGroup );
  m_methodCombo->setObjectName( QStringLiteral( "cdMethodCombo" ) );
  m_methodCombo->addItem( tr( "差值 Difference" ), QStringLiteral( "difference" ) );
  m_methodCombo->addItem( tr( "归一化差值" ), QStringLiteral( "normalized_difference" ) );
  m_methodCombo->addItem( tr( "比值 Ratio" ), QStringLiteral( "ratio" ) );
  m_methodCombo->addItem( tr( "变化向量分析 CVA" ), QStringLiteral( "cva" ) );
  m_methodCombo->addItem( tr( "多变量变化检测 MAD" ), QStringLiteral( "mad" ) );
  m_methodCombo->addItem( tr( "变化掩膜（手动阈值）" ), QStringLiteral( "change_mask" ) );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "• 差值：后−前\n• 归一化差值：(后−前)/(后+前)\n• 比值：后/前\n"
    "• CVA：多波段变化向量幅值（用全部波段）\n• MAD：多变量变化检测（CCA 典型相关分析）\n• 掩膜：|差值|≥阈值" ) );
  methodForm->addRow( tr( "变化算法" ), m_methodCombo );

  m_makeMaskCheck = new QCheckBox( tr( "同时输出二值变化掩膜" ), methodGroup );
  m_makeMaskCheck->setObjectName( QStringLiteral( "cdMakeMaskCheck" ) );
  SicnuDialogHelp::tip( m_makeMaskCheck, tr(
    "除方法栅格外，再输出 0/1 变化掩膜（可配阈值策略、形态学清理与最小制图单元）。" ) );
  methodForm->addRow( QString(), m_makeMaskCheck );

  // Mask parameter section: threshold strategy + cleanup + minimum mapping unit.
  m_maskParamFrame = new QFrame( methodGroup );
  auto *maskForm = new QFormLayout( m_maskParamFrame );
  maskForm->setContentsMargins( 0, 0, 0, 0 );
  maskForm->setHorizontalSpacing( 10 );
  maskForm->setVerticalSpacing( 8 );

  m_thresholdMethodCombo = new QComboBox( m_maskParamFrame );
  m_thresholdMethodCombo->setObjectName( QStringLiteral( "cdThresholdMethodCombo" ) );
  m_thresholdMethodCombo->addItem( tr( "手动阈值" ), QStringLiteral( "manual" ) );
  m_thresholdMethodCombo->addItem( tr( "Otsu 大津法" ), QStringLiteral( "otsu" ) );
  m_thresholdMethodCombo->addItem( tr( "分位数阈值" ), QStringLiteral( "percentile" ) );
  m_thresholdMethodCombo->addItem( tr( "统计阈值（均值+kσ）" ), QStringLiteral( "statistical" ) );
  SicnuDialogHelp::tip( m_thresholdMethodCombo, tr( "二值变化掩膜阈值提取策略。" ) );
  maskForm->addRow( tr( "阈值策略" ), m_thresholdMethodCombo );

  m_thresholdLabel = new QLabel( tr( "阈值" ), m_maskParamFrame );
  m_thresholdSpin = new QDoubleSpinBox( m_maskParamFrame );
  m_thresholdSpin->setObjectName( QStringLiteral( "cdThresholdSpin" ) );
  m_thresholdSpin->setRange( 0.0, 10000.0 );
  m_thresholdSpin->setDecimals( 2 );
  m_thresholdSpin->setValue( 10.0 );
  SicnuDialogHelp::tip( m_thresholdSpin, tr( "手动指定绝对变化阈值。" ) );
  maskForm->addRow( m_thresholdLabel, m_thresholdSpin );

  m_percentileSpin = new QDoubleSpinBox( m_maskParamFrame );
  m_percentileSpin->setObjectName( QStringLiteral( "cdPercentileSpin" ) );
  m_percentileSpin->setRange( 0.0, 100.0 );
  m_percentileSpin->setDecimals( 1 );
  m_percentileSpin->setValue( 90.0 );
  SicnuDialogHelp::tip( m_percentileSpin, tr( "按变化强度百分位提取变化区域（0~100）。" ) );
  maskForm->addRow( tr( "分位数值 (%)" ), m_percentileSpin );

  m_statisticalKSpin = new QDoubleSpinBox( m_maskParamFrame );
  m_statisticalKSpin->setObjectName( QStringLiteral( "cdStatisticalKSpin" ) );
  m_statisticalKSpin->setRange( 0.0, 10.0 );
  m_statisticalKSpin->setDecimals( 2 );
  m_statisticalKSpin->setValue( 2.0 );
  SicnuDialogHelp::tip( m_statisticalKSpin, tr( "统计阈值 = 变化均值 + k × 标准差。" ) );
  maskForm->addRow( tr( "k (标准差倍数)" ), m_statisticalKSpin );

  m_cleanupCombo = new QComboBox( m_maskParamFrame );
  m_cleanupCombo->setObjectName( QStringLiteral( "cdCleanupCombo" ) );
  m_cleanupCombo->addItem( tr( "无操作" ), QStringLiteral( "none" ) );
  m_cleanupCombo->addItem( tr( "形态学腐蚀" ), QStringLiteral( "erode" ) );
  m_cleanupCombo->addItem( tr( "形态学膨胀" ), QStringLiteral( "dilate" ) );
  m_cleanupCombo->addItem( tr( "开运算 (去孤立斑)" ), QStringLiteral( "open" ) );
  m_cleanupCombo->addItem( tr( "闭运算 (填孔洞)" ), QStringLiteral( "close" ) );
  SicnuDialogHelp::tip( m_cleanupCombo, tr( "二值变化掩膜形态学后处理操作。" ) );
  maskForm->addRow( tr( "形态学清理" ), m_cleanupCombo );

  m_cleanupIterSpin = new QSpinBox( m_maskParamFrame );
  m_cleanupIterSpin->setObjectName( QStringLiteral( "cdCleanupIterSpin" ) );
  m_cleanupIterSpin->setRange( 1, 20 );
  m_cleanupIterSpin->setValue( 1 );
  SicnuDialogHelp::tip( m_cleanupIterSpin, tr( "形态学运算迭代次数" ) );
  maskForm->addRow( tr( "迭代次数" ), m_cleanupIterSpin );

  m_minAreaSpin = new QSpinBox( m_maskParamFrame );
  m_minAreaSpin->setObjectName( QStringLiteral( "cdMinAreaSpin" ) );
  m_minAreaSpin->setRange( 0, 100000000 );
  m_minAreaSpin->setValue( 0 );
  SicnuDialogHelp::tip( m_minAreaSpin, tr( "最小制图单元（像元）：移除小于该面积的碎小连通斑块；0 = 关闭。" ) );
  maskForm->addRow( tr( "最小制图单元 (像元)" ), m_minAreaSpin );

  methodForm->addRow( m_maskParamFrame );

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
  connect( m_makeMaskCheck, &QCheckBox::toggled,
           this, &ChangeDetectionDialog::onMakeMaskToggled );
  connect( m_thresholdMethodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::onThresholdMethodChanged );

  updateMaskParamVisibility();
  populateLayers();
}

void ChangeDetectionDialog::populateLayers()
{
  m_beforeLayerCombo->populate();
  m_afterLayerCombo->populate();
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
  Q_UNUSED( index );
  updateMaskParamVisibility();
}

void ChangeDetectionDialog::onMakeMaskToggled()
{
  updateMaskParamVisibility();
}

void ChangeDetectionDialog::onThresholdMethodChanged( int index )
{
  Q_UNUSED( index );
  updateMaskParamVisibility();
}

void ChangeDetectionDialog::updateMaskParamVisibility()
{
  if ( !m_maskParamFrame || !m_thresholdMethodCombo )
    return;

  const QString method = m_methodCombo->currentData().toString();
  const bool maskRequested = m_makeMaskCheck->isChecked()
                             || method == QStringLiteral( "change_mask" );
  m_maskParamFrame->setVisible( maskRequested );
  if ( !maskRequested )
    return;

  const QString strategy = m_thresholdMethodCombo->currentData().toString();
  const bool manual = ( strategy == QStringLiteral( "manual" ) );
  const bool percentile = ( strategy == QStringLiteral( "percentile" ) );
  const bool statistical = ( strategy == QStringLiteral( "statistical" ) );
  m_thresholdLabel->setVisible( manual );
  m_thresholdSpin->setVisible( manual );
  m_percentileSpin->setVisible( percentile );
  m_statisticalKSpin->setVisible( statistical );

  // The legacy change_mask method only supports the manual threshold; its
  // backend path also ignores cleanup and the MMU, so those controls are
  // disabled (not just the strategy combo).
  const bool legacy = ( method == QStringLiteral( "change_mask" ) );
  m_thresholdMethodCombo->setEnabled( !legacy );
  m_cleanupCombo->setEnabled( !legacy );
  m_cleanupIterSpin->setEnabled( !legacy );
  m_minAreaSpin->setEnabled( !legacy );
  if ( legacy && strategy != QStringLiteral( "manual" ) )
    m_thresholdMethodCombo->setCurrentIndex(
      m_thresholdMethodCombo->findData( QStringLiteral( "manual" ) ) );
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
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "前期或后期影像无效。" ) );
    return false;
  }
  const QString gridMessage = rasterGridCompatibilityMessage(
    before->source(), after->source() );
  if ( !gridMessage.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(),
                          tr( "两张影像的像元网格不兼容，无法逐像元比较：\n%1" )
                            .arg( gridMessage ) );
    return false;
  }
  return true;
}

Json::Value ChangeDetectionDialog::buildParams() const
{
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );

  Json::Value params( Json::objectValue );
  params["before"] = before ? before->source().toStdString() : std::string();
  params["after"] = after ? after->source().toStdString() : std::string();
  params["beforeBand"] = m_beforeBandCombo->currentData().toInt();
  params["afterBand"] = m_afterBandCombo->currentData().toInt();
  params["method"] = m_methodCombo->currentData().toString().toStdString();
  params["output"] = outputPath().toStdString();

  // Mask parameters surface when the user requests a mask output (the legacy
  // change_mask method always writes one; for the other methods the checkbox
  // opts in).
  const bool maskRequested = m_makeMaskCheck->isChecked()
                             || m_methodCombo->currentData().toString()
                                  == QStringLiteral( "change_mask" );
  if ( maskRequested )
  {
    params["makeMask"] = true;
    params["threshold"] = m_thresholdSpin->value();
    const QString strategy = m_thresholdMethodCombo->currentData().toString();
    if ( strategy != QStringLiteral( "manual" ) )
      params["thresholdMethod"] = strategy.toStdString();
    if ( strategy == QStringLiteral( "percentile" ) )
      params["percentile"] = m_percentileSpin->value();
    if ( strategy == QStringLiteral( "statistical" ) )
      params["statisticalK"] = m_statisticalKSpin->value();
    if ( m_minAreaSpin->value() > 0 )
      params["minAreaPixels"] = m_minAreaSpin->value();
    const QString cleanup = m_cleanupCombo->currentData().toString();
    if ( cleanup != QStringLiteral( "none" ) )
    {
      params["cleanup"] = cleanup.toStdString();
      params["cleanupIterations"] = m_cleanupIterSpin->value();
    }
  }

  return params;
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
  const Json::Value params = buildParams();
  m_statusLabel->setText( tr( "运行中…" ) );
  runOperatorTask( QStringLiteral( "rs:change_detection" ), params,
                   [this]( const Json::Value &result ) {
                     if ( !m_statusLabel )
                       return;
                     if ( result.isMember( "mean" ) )
                     {
                       QString text = tr( "变化均值 %1，标准差 %2" )
                                        .arg( result["mean"].asDouble(), 0, 'f', 4 )
                                        .arg( result["stddev"].asDouble(), 0, 'f', 4 );
                       if ( result.isMember( "changedPercent" ) )
                         text += tr( "；变化像元 %1 / %2（%3%）" )
                                   .arg( result["changedPixels"].asUInt64() )
                                   .arg( result["totalPixels"].asUInt64() )
                                   .arg( result["changedPercent"].asDouble(), 0, 'f', 2 );
                       m_statusLabel->setText( text );
                     }
                   } );
}
