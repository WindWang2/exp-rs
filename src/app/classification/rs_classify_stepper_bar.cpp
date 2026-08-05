#include "rs_classify_stepper_bar.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QToolButton>

namespace
{

const char *const kStepLabels[] = {
  "1 体系",
  "2 样本",
  "3 评价",
  "4 训练",
  "5 精度",
  "6 后处理",
  "7 输出",
};

static_assert( sizeof( kStepLabels ) / sizeof( kStepLabels[0] )
                 == static_cast<int>( RsClassifyStep::Count ),
               "step label count must match RsClassifyStep::Count" );

} // namespace

RsClassifyStepperBar::RsClassifyStepperBar( QWidget *parent )
  : QWidget( parent )
  , mComplete( static_cast<int>( RsClassifyStep::Count ), false )
{
  setObjectName( QStringLiteral( "rsClassifyStepperBar" ) );

  auto *row = new QHBoxLayout( this );
  row->setContentsMargins( 4, 2, 4, 2 );
  row->setSpacing( 4 );

  mGroup = new QButtonGroup( this );
  mGroup->setExclusive( true );

  mButtons.resize( static_cast<int>( RsClassifyStep::Count ) );
  for ( int i = 0; i < static_cast<int>( RsClassifyStep::Count ); ++i )
  {
    auto *btn = new QToolButton( this );
    btn->setObjectName( QStringLiteral( "rsClassifyStepBtn%1" ).arg( i ) );
    btn->setText( QString::fromUtf8( kStepLabels[i] ) );
    btn->setCheckable( true );
    btn->setAutoRaise( true );
    btn->setToolButtonStyle( Qt::ToolButtonTextOnly );
    mGroup->addButton( btn, i );
    mButtons[i] = btn;
    btn->setToolTip( tr( "点击切换到步骤：%1" ).arg( QString::fromUtf8( kStepLabels[i] ) ) );
    row->addWidget( btn );
    rebuildStyle( i );
  }

  row->addStretch( 1 );

  mExpertCheck = new QCheckBox( tr( "专家模式" ), this );
  mExpertCheck->setObjectName( QStringLiteral( "rsClassifyExpertMode" ) );
  mExpertCheck->setToolTip( tr( "勾选后解锁全部步骤（默认向导模式逐步引导）。" ) );
  row->addWidget( mExpertCheck );

  if ( !mButtons.isEmpty() )
    mButtons[0]->setChecked( true );

  connect( mGroup, &QButtonGroup::idClicked, this, [this]( int id ) {
    if ( id < 0 || id >= static_cast<int>( RsClassifyStep::Count ) )
      return;
    emit stepClicked( static_cast<RsClassifyStep>( id ) );
  } );

  connect( mExpertCheck, &QCheckBox::toggled, this, [this]( bool on ) {
    emit modeToggled( on ? RsClassifyUiMode::Expert : RsClassifyUiMode::Wizard );
  } );
}

void RsClassifyStepperBar::setCurrentStep( RsClassifyStep s )
{
  const int id = static_cast<int>( s );
  if ( id < 0 || id >= mButtons.size() )
    return;
  if ( QToolButton *btn = mButtons[id] )
  {
    const QSignalBlocker blocker( mGroup );
    btn->setChecked( true );
  }
  for ( int i = 0; i < mButtons.size(); ++i )
    rebuildStyle( i );
}

void RsClassifyStepperBar::setStepComplete( RsClassifyStep s, bool complete )
{
  const int id = static_cast<int>( s );
  if ( id < 0 || id >= mComplete.size() )
    return;
  if ( mComplete[id] == complete )
    return;
  mComplete[id] = complete;
  rebuildStyle( id );
}

void RsClassifyStepperBar::setMode( RsClassifyUiMode m )
{
  if ( !mExpertCheck )
    return;
  const QSignalBlocker blocker( mExpertCheck );
  mExpertCheck->setChecked( m == RsClassifyUiMode::Expert );
}

void RsClassifyStepperBar::rebuildStyle( int index )
{
  if ( index < 0 || index >= mButtons.size() )
    return;
  QToolButton *btn = mButtons[index];
  if ( !btn )
    return;

  QString text = QString::fromUtf8( kStepLabels[index] );
  if ( mComplete.value( index ) )
    text = QStringLiteral( "✓ " ) + text;
  btn->setText( text );

  QFont f = btn->font();
  f.setBold( mComplete.value( index ) || btn->isChecked() );
  btn->setFont( f );

  // Soft green tint when complete so progress is visible at a glance.
  if ( mComplete.value( index ) )
  {
    btn->setStyleSheet(
      QStringLiteral( "QToolButton { color: #1a7f37; }"
                      "QToolButton:checked { background: #dafbe1; }" ) );
  }
  else
  {
    btn->setStyleSheet( QString() );
  }
}
