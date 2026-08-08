// src/app/dialogs/post_classification_dialog.cpp — Post-Classification Compare
#include "post_classification_dialog.h"
#include "dialog_help_catalog.h"
#include "widgets/raster_layer_combo.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <qgsproject.h>

PostClassificationDialog::PostClassificationDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 480 );
  setupUi();
}

void PostClassificationDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *inputSec = SicnuUi::makeSection(
    this, tr( "双时相分类输入" ),
    tr( "比较两期分类结果：输出逐类转移矩阵（行=前时相类，列=后时相类）、"
        "逐类增益/损失与变化类型图。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_beforeLayerCombo = new RasterLayerCombo( inputSec );
  m_beforeLayerCombo->setObjectName( QStringLiteral( "postClassBeforeCombo" ) );
  m_afterLayerCombo = new RasterLayerCombo( inputSec );
  m_afterLayerCombo->setObjectName( QStringLiteral( "postClassAfterCombo" ) );
  m_beforeBandCombo = new QComboBox( inputSec );
  m_beforeBandCombo->setObjectName( QStringLiteral( "postClassBeforeBandCombo" ) );
  m_afterBandCombo = new QComboBox( inputSec );
  m_afterBandCombo->setObjectName( QStringLiteral( "postClassAfterBandCombo" ) );
  SicnuDialogHelp::tip( m_beforeLayerCombo, tr( "前期分类栅格（主题图）。" ) );
  SicnuDialogHelp::tip( m_afterLayerCombo, tr( "后期分类栅格（主题图）。" ) );
  SicnuDialogHelp::tip( m_beforeBandCombo, tr( "前期分类波段。" ) );
  SicnuDialogHelp::tip( m_afterBandCombo, tr( "后期分类波段。" ) );
  form->addRow( tr( "前期分类" ), m_beforeLayerCombo );
  form->addRow( tr( "前期波段" ), m_beforeBandCombo );
  form->addRow( tr( "后期分类" ), m_afterLayerCombo );
  form->addRow( tr( "后期波段" ), m_afterBandCombo );

  m_classCountSpin = new QSpinBox( inputSec );
  m_classCountSpin->setObjectName( QStringLiteral( "postClassCountSpin" ) );
  m_classCountSpin->setRange( 0, 255 );
  m_classCountSpin->setValue( 0 );
  m_classCountSpin->setSpecialValueText( tr( "自动（按观测最大类 + 1）" ) );
  SicnuDialogHelp::tip( m_classCountSpin, tr(
    "类别总数（变化码 before*classCount+after 须装入 UInt16，故 ≤255）。"
    "0 = 按两期影像中观测到的最大类别自动推断。" ) );
  form->addRow( tr( "类别数" ), m_classCountSpin );

  qobject_cast<QVBoxLayout *>( inputSec->layout() )->addLayout( form );
  mainLayout->addWidget( inputSec );

  setupOutputRow( mainLayout );

  m_summaryLabel = SicnuUi::makeHintLabel( this, tr( "运行后在此显示变化统计摘要。" ) );
  m_summaryLabel->setObjectName( QStringLiteral( "postClassSummaryLabel" ) );
  m_summaryLabel->setWordWrap( true );
  mainLayout->addWidget( m_summaryLabel );

  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_beforeLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, [this] { populateBandCombo( m_beforeLayerCombo, m_beforeBandCombo ); } );
  connect( m_afterLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, [this] { populateBandCombo( m_afterLayerCombo, m_afterBandCombo ); } );

  populateLayers();
}

void PostClassificationDialog::populateBandCombo( RasterLayerCombo *layerCombo, QComboBox *bandCombo )
{
  bandCombo->clear();
  auto *rl = layerCombo->currentRasterLayer();
  if ( !rl )
    return;
  for ( int i = 1; i <= rl->bandCount(); ++i )
    bandCombo->addItem( tr( "波段 %1" ).arg( i ), i );
}

void PostClassificationDialog::populateLayers()
{
  m_beforeLayerCombo->populate();
  m_afterLayerCombo->populate();
  populateBandCombo( m_beforeLayerCombo, m_beforeBandCombo );
  populateBandCombo( m_afterLayerCombo, m_afterBandCombo );
}

Json::Value PostClassificationDialog::buildParams() const
{
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );

  Json::Value params( Json::objectValue );
  params["before"] = before ? before->source().toStdString() : std::string();
  params["after"] = after ? after->source().toStdString() : std::string();
  params["output"] = outputPath().toStdString();
  params["band"] = m_beforeBandCombo->currentData().toInt();
  params["afterBand"] = m_afterBandCombo->currentData().toInt();
  if ( m_classCountSpin->value() > 0 )
    params["class_count"] = m_classCountSpin->value();
  return params;
}

bool PostClassificationDialog::validateInputs()
{
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请指定输出文件。" ) );
    return false;
  }
  if ( m_beforeLayerCombo->currentData().toString().isEmpty()
       || m_afterLayerCombo->currentData().toString().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请选择前期与后期分类栅格。" ) );
    return false;
  }
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "前期或后期分类栅格无效。" ) );
    return false;
  }
  const QString gridMessage = rasterGridCompatibilityMessage(
    before->source(), after->source() );
  if ( !gridMessage.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(),
                          tr( "两张分类栅格的像元网格不兼容，无法比较：\n%1" )
                            .arg( gridMessage ) );
    return false;
  }
  return true;
}

void PostClassificationDialog::onRun()
{
  if ( !validateInputs() )
    return;

  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "所选分类栅格无效。" ) );
    return;
  }

  setRasterLayer( before );
  runOperatorTask( QStringLiteral( "rs:post_classification_change" ), buildParams(),
                   [this]( const Json::Value &result ) { showResultSummary( result ); } );
}

void PostClassificationDialog::showResultSummary( const Json::Value &result )
{
  if ( !m_summaryLabel || !result.isObject() )
    return;

  QStringList lines;
  if ( result.isMember( "changedPixels" ) )
  {
    lines << tr( "变化像元：%1 / %2（%3%）" )
               .arg( result["changedPixels"].asUInt64() )
               .arg( result["totalPixels"].asUInt64() )
               .arg( result["changedPercent"].asDouble(), 0, 'f', 2 );
  }
  if ( result.isMember( "transitionMatrix" ) && result["transitionMatrix"].isArray()
       && result["transitionMatrix"].size() > 0 )
  {
    lines << tr( "转移矩阵（行=前时相，列=后时相；仅列出非零项）：" );
    const int n = static_cast<int>( result["transitionMatrix"].size() );
    for ( int from = 0; from < n; ++from )
    {
      const Json::Value &row = result["transitionMatrix"][from];
      for ( int to = 0; to < n; ++to )
      {
        if ( row[to].asUInt64() > 0 )
          lines << QStringLiteral( "  %1→%2: %3" )
                       .arg( from )
                       .arg( to )
                       .arg( row[to].asUInt64() );
      }
    }
  }
  m_summaryLabel->setText( lines.isEmpty() ? tr( "运行完成（无摘要数据）。" )
                                           : lines.join( QLatin1Char( '\n' ) ) );
}
