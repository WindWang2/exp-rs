#include "rs_template_match_dialog.h"
#include "dialogs/dialog_utils.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

RsTemplateMatchDialog::RsTemplateMatchDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "模板匹配（基于初始坐标）" ) );
  setObjectName( QStringLiteral( "rsTemplateMatchDialog" ) );
  SicnuUi::polishDialog( this, 420 );

  auto *root = SicnuUi::makeDialogRootLayout( this );

  auto *hint = SicnuUi::makeHintLabel(
    this,
    tr( "适用于源影像已有近似地理坐标的情况：使用 GeoTransform 预测参考影像搜索区，"
        "再做模板相关匹配，比 SIFT 更稳健、更可控。" ) );
  hint->setObjectName( QStringLiteral( "rsDialogHelpSummary" ) );
  root->addWidget( hint );

  QGroupBox *paramGroup = SicnuUi::makeGroup( this, tr( "模板相关匹配参数" ) );
  auto *form = SicnuUi::makeFormLayout( paramGroup );

  m_seedMode = new QComboBox( paramGroup );
  m_seedMode->setObjectName( QStringLiteral( "templateSeedMode" ) );
  m_seedMode->addItem( tr( "规则网格（用 SRC 初始地理参考预测搜索区）" ),
                       int( RsTemplateMatcher::SeedMode::Grid ) );
  m_seedMode->addItem( tr( "现有 GCP 作为种子（精化）" ),
                       int( RsTemplateMatcher::SeedMode::ExistingSeeds ) );
  m_seedMode->setToolTip( tr( "种子生成模式：规则网格在整景均匀分布，或基于现有 GCP 坐标做局部微调精化" ) );
  form->addRow( tr( "种子模式" ), m_seedMode );

  m_templateSize = new QSpinBox( paramGroup );
  m_templateSize->setObjectName( QStringLiteral( "templateSizeSpin" ) );
  m_templateSize->setRange( 17, 257 );
  m_templateSize->setSingleStep( 2 );
  m_templateSize->setValue( 65 );
  m_templateSize->setToolTip( tr( "从源影像裁切的模板边长（像素，建议奇数）" ) );
  form->addRow( tr( "模板大小" ), m_templateSize );

  m_searchRadius = new QSpinBox( paramGroup );
  m_searchRadius->setObjectName( QStringLiteral( "templateSearchRadiusSpin" ) );
  m_searchRadius->setRange( 32, 1024 );
  m_searchRadius->setValue( 96 );
  m_searchRadius->setToolTip(
    tr( "在参考影像上，以初始坐标预测位置为中心的搜索半宽（像素）" ) );
  form->addRow( tr( "搜索半径 (px)" ), m_searchRadius );

  m_minScore = new QDoubleSpinBox( paramGroup );
  m_minScore->setObjectName( QStringLiteral( "templateMinScoreSpin" ) );
  m_minScore->setRange( 0.30, 0.99 );
  m_minScore->setSingleStep( 0.05 );
  m_minScore->setDecimals( 2 );
  m_minScore->setValue( 0.75 );
  m_minScore->setToolTip( tr( "归一化互相关系数下限（TM_CCOEFF_NORMED）" ) );
  form->addRow( tr( "最小相关分" ), m_minScore );

  m_gridRows = new QSpinBox( paramGroup );
  m_gridRows->setObjectName( QStringLiteral( "templateGridRowsSpin" ) );
  m_gridRows->setRange( 2, 20 );
  m_gridRows->setValue( 5 );
  m_gridRows->setToolTip( tr( "规则网格行数（沿垂直方向采样的种子点数量）" ) );
  form->addRow( tr( "网格行数" ), m_gridRows );

  m_gridCols = new QSpinBox( paramGroup );
  m_gridCols->setObjectName( QStringLiteral( "templateGridColsSpin" ) );
  m_gridCols->setRange( 2, 20 );
  m_gridCols->setValue( 5 );
  m_gridCols->setToolTip( tr( "规则网格列数（沿水平方向采样的种子点数量）" ) );
  form->addRow( tr( "网格列数" ), m_gridCols );

  root->addWidget( paramGroup );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "template_match" ) );

  auto *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "确定" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "取消" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Cancel ) );

  auto *helpBtn = buttons->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "打开本对话框的帮助说明。" ) );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "template_match" ), windowTitle() );
  } );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  root->addWidget( buttons );

  const auto syncGrid = [this]() {
    const bool grid = m_seedMode->currentData().toInt()
                      == int( RsTemplateMatcher::SeedMode::Grid );
    m_gridRows->setEnabled( grid );
    m_gridCols->setEnabled( grid );
  };
  connect( m_seedMode, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, [syncGrid]( int ) { syncGrid(); } );
  syncGrid();
}

RsTemplateMatcher::Params RsTemplateMatchDialog::params() const
{
  RsTemplateMatcher::Params p;
  p.seedMode = static_cast<RsTemplateMatcher::SeedMode>( m_seedMode->currentData().toInt() );
  p.templateSize = m_templateSize->value();
  p.searchRadiusPx = m_searchRadius->value();
  p.minScore = m_minScore->value();
  p.gridRows = m_gridRows->value();
  p.gridCols = m_gridCols->value();
  return p;
}
