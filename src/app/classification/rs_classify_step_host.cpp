#include "rs_classify_step_host.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace
{

struct StepMeta
{
  const char *title;
  const char *tip;
};

const StepMeta kMeta[] = {
  { "1 · 建立分类体系", "完成条件：至少 2 个类别（名称与颜色）" },
  { "2 · 选择样本", "完成条件：至少 2 个类别有训练像元" },
  { "3 · 样本评价", "完成条件：标记已审阅（JM / 光谱）" },
  { "4 · 训练-分类", "完成条件：完成全图 Apply（预览不计）" },
  { "5 · 精度评定", "完成条件：存在有效精度指标" },
  { "6 · 分类后处理", "完成条件：跳过或生成后处理结果" },
  { "7 · 输出", "完成条件：导出或加载到主图" },
};

static_assert( sizeof( kMeta ) / sizeof( kMeta[0] )
                 == static_cast<int>( RsClassifyStep::Count ),
               "step meta count must match RsClassifyStep::Count" );

} // namespace

RsClassifyStepHost::RsClassifyStepHost( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsClassifyStepHost" ) );

  auto *outer = new QVBoxLayout( this );
  outer->setContentsMargins( 0, 0, 0, 0 );

  mStack = new QStackedWidget( this );
  mStack->setObjectName( QStringLiteral( "rsClassifyStepStack" ) );
  outer->addWidget( mStack );

  mPanels.resize( static_cast<int>( RsClassifyStep::Count ) );
  for ( int i = 0; i < static_cast<int>( RsClassifyStep::Count ); ++i )
  {
    QWidget *p = buildPanel( static_cast<RsClassifyStep>( i ) );
    mPanels[i] = p;
    mStack->addWidget( p );
  }
}

QWidget *RsClassifyStepHost::buildPanel( RsClassifyStep s )
{
  const int idx = static_cast<int>( s );
  auto *panel = new QWidget( this );
  panel->setObjectName( QStringLiteral( "classifyStep%1" ).arg( idx ) );

  auto *layout = new QVBoxLayout( panel );
  layout->setContentsMargins( 12, 12, 12, 12 );
  layout->setSpacing( 8 );

  auto *title = new QLabel( tr( kMeta[idx].title ), panel );
  title->setObjectName( QStringLiteral( "classifyStepTitle" ) );
  QFont tf = title->font();
  tf.setBold( true );
  tf.setPointSizeF( tf.pointSizeF() + 1.0 );
  title->setFont( tf );
  layout->addWidget( title );

  auto *tip = new QLabel( tr( kMeta[idx].tip ), panel );
  tip->setObjectName( QStringLiteral( "classifyStepTip" ) );
  tip->setWordWrap( true );
  tip->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
  layout->addWidget( tip );

  auto *gate = new QLabel( panel );
  gate->setObjectName( QStringLiteral( "classifyStepGate" ) );
  gate->setWordWrap( true );
  gate->setStyleSheet( QStringLiteral( "color: #9a6700;" ) );
  layout->addWidget( gate );

  // Placeholder body for Task 6 real controls.
  auto *placeholder = new QLabel(
    tr( "（步骤内容将在后续任务接入）" ), panel );
  placeholder->setObjectName( QStringLiteral( "classifyStepPlaceholder" ) );
  placeholder->setStyleSheet( QStringLiteral( "color: #8c959f;" ) );
  layout->addWidget( placeholder );

  layout->addStretch( 1 );

  auto *nav = new QHBoxLayout;
  auto *prev = new QPushButton( tr( "上一步" ), panel );
  prev->setObjectName( QStringLiteral( "classifyStepPrev" ) );
  auto *next = new QPushButton( tr( "下一步" ), panel );
  next->setObjectName( QStringLiteral( "classifyStepNext" ) );
  nav->addWidget( prev );
  nav->addStretch( 1 );
  nav->addWidget( next );
  layout->addLayout( nav );

  prev->setEnabled( idx > 0 );
  next->setEnabled( idx + 1 < static_cast<int>( RsClassifyStep::Count ) );

  connect( prev, &QPushButton::clicked, this, &RsClassifyStepHost::prevClicked );
  connect( next, &QPushButton::clicked, this, &RsClassifyStepHost::nextClicked );

  return panel;
}

void RsClassifyStepHost::setCurrentStep( RsClassifyStep s )
{
  const int idx = static_cast<int>( s );
  if ( !mStack || idx < 0 || idx >= mPanels.size() )
    return;
  mStack->setCurrentIndex( idx );
}

QWidget *RsClassifyStepHost::panel( RsClassifyStep s ) const
{
  const int idx = static_cast<int>( s );
  if ( idx < 0 || idx >= mPanels.size() )
    return nullptr;
  return mPanels[idx];
}

QLabel *RsClassifyStepHost::gateLabel( RsClassifyStep s ) const
{
  QWidget *p = panel( s );
  if ( !p )
    return nullptr;
  return p->findChild<QLabel *>( QStringLiteral( "classifyStepGate" ) );
}

QPushButton *RsClassifyStepHost::prevButton( RsClassifyStep s ) const
{
  QWidget *p = panel( s );
  if ( !p )
    return nullptr;
  return p->findChild<QPushButton *>( QStringLiteral( "classifyStepPrev" ) );
}

QPushButton *RsClassifyStepHost::nextButton( RsClassifyStep s ) const
{
  QWidget *p = panel( s );
  if ( !p )
    return nullptr;
  return p->findChild<QPushButton *>( QStringLiteral( "classifyStepNext" ) );
}
