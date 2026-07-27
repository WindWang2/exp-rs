/***************************************************************************
 * task_panel_host.cpp  —  right-side task panel host for atomic RS tools
 ***************************************************************************/
#include "task_panel_host.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

TaskPanelHost::TaskPanelHost( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsTaskPanel" ) );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 12, 12, 12, 12 );
  root->setSpacing( 8 );

  m_title = new QLabel( this );
  m_title->setObjectName( QStringLiteral( "rsTaskPanelTitle" ) );
  m_title->setWordWrap( true );
  root->addWidget( m_title );

  m_help = new QLabel( this );
  m_help->setObjectName( QStringLiteral( "rsTaskPanelHelp" ) );
  m_help->setWordWrap( true );
  m_help->setObjectName( QStringLiteral( "rsDialogHint" ) );
  root->addWidget( m_help );

  m_form = new SchemaFormBuilder( this );
  m_form->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
  root->addWidget( m_form, /*stretch=*/1 );

  m_progress = new QProgressBar( this );
  m_progress->setObjectName( QStringLiteral( "rsTaskPanelProgress" ) );
  m_progress->setRange( 0, 0 ); // indeterminate while running
  m_progress->setTextVisible( false );
  m_progress->setVisible( false );
  root->addWidget( m_progress );

  m_hint = new QLabel( this );
  m_hint->setObjectName( QStringLiteral( "rsTaskPanelHint" ) );
  m_hint->setWordWrap( true );
  m_hint->setProperty( "error", false );
  root->addWidget( m_hint );

  auto *actions = new QHBoxLayout();
  actions->setSpacing( 8 );

  m_helpBtn = new QPushButton( tr( "帮助" ), this );
  m_helpBtn->setObjectName( QStringLiteral( "rsTaskPanelHelpBtn" ) );
  actions->addWidget( m_helpBtn );

  m_loadToMap = new QCheckBox( tr( "加载结果到地图" ), this );
  m_loadToMap->setObjectName( QStringLiteral( "rsTaskPanelLoadToMap" ) );
  m_loadToMap->setChecked( true );
  actions->addWidget( m_loadToMap );

  actions->addStretch( 1 );

  m_runBtn = new QPushButton( tr( "运行" ), this );
  m_runBtn->setObjectName( QStringLiteral( "rsTaskPanelRun" ) );
  m_runBtn->setProperty( "primary", true );
  actions->addWidget( m_runBtn );

  m_closeBtn = new QPushButton( tr( "关闭" ), this );
  m_closeBtn->setObjectName( QStringLiteral( "rsTaskPanelClose" ) );
  actions->addWidget( m_closeBtn );

  root->addLayout( actions );

  connect( m_runBtn, &QPushButton::clicked, this, &TaskPanelHost::runClicked );
  connect( m_helpBtn, &QPushButton::clicked, this, &TaskPanelHost::helpClicked );
  connect( m_closeBtn, &QPushButton::clicked, this, &TaskPanelHost::closeClicked );
}

void TaskPanelHost::showTool( const QString &title, const QString &helpSummary, const Json::Value &schema )
{
  m_title->setText( title );
  m_help->setText( helpSummary );
  m_form->rebuild( schema );
  m_progress->setVisible( false );
  m_runBtn->setEnabled( true );
  m_form->setEnabled( true );
  m_hint->clear();
  applyHintStyle( false );
}

void TaskPanelHost::setRasterLayerChoices( const QStringList &ids, const QStringList &names )
{
  m_form->setRasterLayerChoices( ids, names );
}

void TaskPanelHost::setHints( const QStringList &hints )
{
  if ( hints.isEmpty() )
  {
    m_hint->clear();
    applyHintStyle( false );
    return;
  }
  m_hint->setText( hints.join( QLatin1Char( '\n' ) ) );
  applyHintStyle( true );
}

void TaskPanelHost::setRunning( bool running )
{
  m_progress->setVisible( running );
  if ( running )
  {
    m_progress->setRange( 0, 0 );
    m_hint->clear();
    applyHintStyle( false );
  }
  m_runBtn->setEnabled( !running );
  m_form->setEnabled( !running );
  m_helpBtn->setEnabled( !running );
  m_loadToMap->setEnabled( !running );
  // Allow Close during run so the user can hide the panel; run continues.
}

void TaskPanelHost::setSuccess( const QString &message )
{
  m_hint->setText( message );
  applyHintStyle( false );
}

void TaskPanelHost::setFailed( const QString &message )
{
  m_hint->setText( message );
  applyHintStyle( true );
}

Json::Value TaskPanelHost::formValues() const
{
  return m_form->values();
}

void TaskPanelHost::setFormValues( const Json::Value &v )
{
  m_form->setValues( v );
}

bool TaskPanelHost::loadResultToMap() const
{
  return m_loadToMap && m_loadToMap->isChecked();
}

SchemaFormBuilder *TaskPanelHost::form() const
{
  return m_form;
}

void TaskPanelHost::applyHintStyle( bool isError )
{
  m_hint->setProperty( "error", isError );
  if ( QStyle *s = m_hint->style() )
  {
    s->unpolish( m_hint );
    s->polish( m_hint );
  }
  m_hint->update();
}
