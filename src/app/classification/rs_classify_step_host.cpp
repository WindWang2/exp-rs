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
  { "1 · Define the Class Scheme", "Done when: at least 2 classes (name and color)" },
  { "2 · Collect Samples", "Done when: at least 2 classes have training pixels" },
  { "3 · Evaluate Samples", "Done when: marked as reviewed (JM / spectral)" },
  { "4 · Train and Classify", "Done when: the full-image Apply has run (previews don't count)" },
  { "5 · Accuracy Assessment", "Done when: valid accuracy metrics exist" },
  { "6 · Post-Classification", "Done when: post-processing is skipped or produced" },
  { "7 · Output", "Done when: exported or loaded to the main view" },
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

  auto *title = new QLabel(  kMeta[idx].title , panel );
  title->setObjectName( QStringLiteral( "classifyStepTitle" ) );
  QFont tf = title->font();
  tf.setBold( true );
  tf.setPointSizeF( tf.pointSizeF() + 1.0 );
  title->setFont( tf );
  layout->addWidget( title );

  auto *tip = new QLabel(  kMeta[idx].tip , panel );
  tip->setObjectName( QStringLiteral( "classifyStepTip" ) );
  tip->setWordWrap( true );
  tip->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
  layout->addWidget( tip );

  auto *gate = new QLabel( panel );
  gate->setObjectName( QStringLiteral( "classifyStepGate" ) );
  gate->setWordWrap( true );
  gate->setStyleSheet( QStringLiteral( "color: #9a6700;" ) );
  layout->addWidget( gate );

  // Empty body for mainwindow to parent step-specific controls into.
  auto *body = new QWidget( panel );
  body->setObjectName( QStringLiteral( "classifyStepBody" ) );
  auto *bodyLayout = new QVBoxLayout( body );
  bodyLayout->setContentsMargins( 0, 4, 0, 4 );
  bodyLayout->setSpacing( 8 );
  layout->addWidget( body, /*stretch=*/1 );

  auto *nav = new QHBoxLayout;
  auto *prev = new QPushButton(  "Previous Step" , panel );
  prev->setObjectName( QStringLiteral( "classifyStepPrev" ) );
  prev->setToolTip(  "Returns to the previous step."  );
  auto *next = new QPushButton(  "Next Step" , panel );
  next->setObjectName( QStringLiteral( "classifyStepNext" ) );
  next->setToolTip(  "Continue to the next step after finishing the current one."  );
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

QWidget *RsClassifyStepHost::body( RsClassifyStep s ) const
{
  QWidget *p = panel( s );
  if ( !p )
    return nullptr;
  return p->findChild<QWidget *>( QStringLiteral( "classifyStepBody" ) );
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
