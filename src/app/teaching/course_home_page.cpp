#include "course_home_page.h"
#include "status_badge.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace sicnu::app::teaching {

CourseHomePage::CourseHomePage( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "undergradLabCourseHome" ) );
  auto *root = new QVBoxLayout( this );
  m_title = new QLabel( tr( "Undergraduate Lab Teaching Workbench" ), this );
  m_title->setObjectName( QStringLiteral( "courseTitle" ) );
  QFont f = m_title->font();
  f.setPointSize( f.pointSize() + 2 );
  f.setBold( true );
  m_title->setFont( f );
  m_audience = new QLabel( this );
  m_progress = new QLabel( this );
  auto *modeRow = new QHBoxLayout;
  modeRow->addWidget( new QLabel( tr( "Mode:" ), this ) );
  m_modeCombo = new QComboBox( this );
  m_modeCombo->addItem( tr( "Guided mode (beginner)" ), QStringLiteral( "beginner" ) );
  m_modeCombo->addItem( tr( "Expert mode" ), QStringLiteral( "expert" ) );
  modeRow->addWidget( m_modeCombo );
  modeRow->addStretch( 1 );
  connect( m_modeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]( int ) {
    emit modeChanged( m_modeCombo->currentData().toString() );
  } );
  m_continueBtn = new QPushButton( tr( "Continue learning" ), this );
  connect( m_continueBtn, &QPushButton::clicked, this, [this]() {
    if ( !m_vm.continueLabId.empty() )
      emit continueRequested( QString::fromStdString( m_vm.continueModuleId ),
                              QString::fromStdString( m_vm.continueLabId ) );
  } );
  m_moduleList = new QListWidget( this );
  m_moduleList->setObjectName( QStringLiteral( "courseModuleList" ) );
  connect( m_moduleList, &QListWidget::itemActivated, this, [this]( QListWidgetItem *item ) {
    if ( !item ) return;
    emit labSelected( item->data( Qt::UserRole ).toString(),
                      item->data( Qt::UserRole + 1 ).toString() );
  } );
  root->addWidget( m_title );
  root->addWidget( m_audience );
  root->addWidget( m_progress );
  root->addLayout( modeRow );
  root->addWidget( m_continueBtn );
  root->addWidget( m_moduleList, 1 );
}

void CourseHomePage::setViewModel( const sicnu::teaching::CourseHomeViewModel &vm )
{
  m_vm = vm;
  rebuild();
}

void CourseHomePage::rebuild()
{
  m_title->setText( m_vm.titleZh.empty()
                      ? tr( "Undergraduate Lab Teaching Workbench" )
                      : QString::fromStdString( m_vm.titleZh ) );
  m_audience->setText( QString::fromStdString( m_vm.audienceZh ) );
  m_progress->setText( tr( "Overall progress: %1%" ).arg( m_vm.overallPercent ) );
  m_continueBtn->setEnabled( !m_vm.continueLabId.empty() );
  const int modeIdx = m_modeCombo->findData(
    QString::fromUtf8( sicnu::teaching::experienceModeWire( m_vm.mode ) ) );
  if ( modeIdx >= 0 ) m_modeCombo->setCurrentIndex( modeIdx );

  m_moduleList->clear();
  for ( const auto &mc : m_vm.modules ) {
    auto *header = new QListWidgetItem(
      QStringLiteral( "【%1】%2  (%3/%4)  %5" )
        .arg( mc.index )
        .arg( QString::fromStdString( mc.titleZh ) )
        .arg( mc.labsDone )
        .arg( mc.labsTotal )
        .arg( QString::fromUtf8( sicnu::teaching::labUiStatusLabelZh( mc.status ) ) ),
      m_moduleList );
    header->setFlags( Qt::ItemIsEnabled );
    QFont hf = header->font();
    hf.setBold( true );
    header->setFont( hf );
    for ( const auto &lc : mc.labs ) {
      QString blockers;
      for ( const auto &b : lc.blockersZh ) {
        if ( !blockers.isEmpty() ) blockers += QStringLiteral( "; " );
        blockers += QString::fromStdString( b );
      }
      auto *item = new QListWidgetItem(
        QStringLiteral( "  • %1  [%2]%3" )
          .arg( QString::fromStdString( lc.labId ) )
          .arg( QString::fromUtf8( sicnu::teaching::labUiStatusLabelZh( lc.status ) ) )
          .arg( blockers.isEmpty() ? QString() : ( QStringLiteral( " — " ) + blockers ) ),
        m_moduleList );
      item->setData( Qt::UserRole, QString::fromStdString( lc.moduleId ) );
      item->setData( Qt::UserRole + 1, QString::fromStdString( lc.labId ) );
      item->setData( Qt::UserRole + 2, QString::fromUtf8( sicnu::teaching::labUiStatusIconToken( lc.status ) ) );
      item->setToolTip( blockers );
    }
  }
}

} // namespace sicnu::app::teaching
