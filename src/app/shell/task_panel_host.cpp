/***************************************************************************
 * task_panel_host.cpp  —  right-side task panel host for atomic RS tools
 ***************************************************************************/
#include "task_panel_host.h"

#include "widgets/rs_result_summary.h"

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

  // Workbench 9.0 M6: production enum resolution — x-ui-enum-source
  // parameters resolve against canvas layers, DataManager assets and the
  // ModelCatalog instead of degrading to free text.
  m_enumProvider = new sicnu::app::WorkbenchEnumProvider( this );
  m_form->setEnumProvider( m_enumProvider );

  m_estimate = new QLabel( this );
  m_estimate->setObjectName( QStringLiteral( "rsTaskPanelEstimate" ) );
  m_estimate->setWordWrap( true );
  m_estimate->hide();
  root->addWidget( m_estimate );

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

  m_resultSummary = new RsResultSummary( this );
  m_resultSummary->hide();
  root->addWidget( m_resultSummary );
  connect( m_resultSummary, &RsResultSummary::openPathRequested,
           this, &TaskPanelHost::resultOpenRequested );

  auto *actions = new QHBoxLayout();
  actions->setSpacing( 8 );

  m_helpBtn = new QPushButton( tr( "Help" ), this );
  m_helpBtn->setObjectName( QStringLiteral( "rsTaskPanelHelpBtn" ) );
  actions->addWidget( m_helpBtn );

  m_loadToMap = new QCheckBox( tr( "Load Results to Map" ), this );
  m_loadToMap->setObjectName( QStringLiteral( "rsTaskPanelLoadToMap" ) );
  m_loadToMap->setChecked( true );
  actions->addWidget( m_loadToMap );

  actions->addStretch( 1 );

  m_runBtn = new QPushButton( tr( "Run" ), this );
  m_runBtn->setObjectName( QStringLiteral( "rsTaskPanelRun" ) );
  m_runBtn->setProperty( "primary", true );
  m_runBtn->setAccessibleName( tr( "Run Current Tool" ) );
  actions->addWidget( m_runBtn );

  m_closeBtn = new QPushButton( tr( "Close" ), this );
  m_closeBtn->setObjectName( QStringLiteral( "rsTaskPanelClose" ) );
  actions->addWidget( m_closeBtn );

  root->addLayout( actions );

  // #704: one affordance for both directions — Run while idle, Stop while
  // the task is in flight (cancel used to be reachable only via the job panel).
  connect( m_runBtn, &QPushButton::clicked, this, &TaskPanelHost::onActionButtonClicked );
  connect( m_helpBtn, &QPushButton::clicked, this, &TaskPanelHost::helpClicked );
  connect( m_closeBtn, &QPushButton::clicked, this, &TaskPanelHost::closeClicked );

  // Validation gating: blocking schema errors disable Run until fixed.
  connect( m_form, &SchemaFormBuilder::validationChanged, this,
           [this]( bool blocked ) {
             m_validationBlocked = blocked;
             updateRunButtonState();
           } );
}

void TaskPanelHost::showTool( const QString &title, const QString &helpSummary,
                              const Json::Value &schema, const QString &operatorHelpContext )
{
  m_title->setText( title );
  m_help->setText( helpSummary );
  m_form->setHelpContext( operatorHelpContext );
  m_form->rebuild( schema );
  m_progress->setVisible( false );
  m_estimate->hide();
  m_resultSummary->clear();
  m_resultSummary->hide();
  m_validationBlocked = m_form->hasErrors();
  m_runBtn->setEnabled( true );
  updateRunButtonState();
  m_form->setEnabled( true );
  m_hint->clear();
  applyHintStyle( false );
}

void TaskPanelHost::setRasterLayerChoices( const QStringList &ids, const QStringList &names )
{
  m_enumProvider->setRasterLayers( ids, names );
  m_form->setRasterLayerChoices( ids, names );
}

void TaskPanelHost::setVectorLayerChoices( const QStringList &ids, const QStringList &names )
{
  m_enumProvider->setVectorLayers( ids, names );
  m_form->setVectorLayerChoices( ids, names );
}

void TaskPanelHost::setAssetChoices( const QStringList &ids, const QStringList &names )
{
  m_form->setAssetChoices( ids, names );
}

void TaskPanelHost::setModelChoices( const QStringList &names )
{
  m_form->setModelChoices( names );
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

void TaskPanelHost::setEstimate( const QString &estimate )
{
  if ( estimate.isEmpty() )
  {
    m_estimate->hide();
    return;
  }
  m_estimate->setText( estimate );
  m_estimate->show();
}

void TaskPanelHost::showResult( const Json::Value &result, const QString &operatorId,
                                qint64 elapsedMs, bool fromCache )
{
  m_resultSummary->setContext( operatorId, elapsedMs, fromCache );
  m_resultSummary->setResult( result );
  m_resultSummary->show();
}

void TaskPanelHost::onActionButtonClicked()
{
  if ( m_running )
    emit stopClicked();
  else
    emit runClicked();
}

void TaskPanelHost::updateRunButtonState()
{
  if ( m_running )
  {
    // Stop stays clickable while running.
    m_runBtn->setEnabled( true );
    return;
  }
  m_runBtn->setEnabled( !m_validationBlocked );
  m_runBtn->setToolTip( m_validationBlocked
                          ? tr( "Fix the parameters highlighted in red first" )
                          : QString() );
}

void TaskPanelHost::setRunning( bool running )
{
  m_running = running;
  m_progress->setVisible( running );
  if ( running )
  {
    m_progress->setRange( 0, 0 );
    m_hint->clear();
    applyHintStyle( false );
    m_runBtn->setText( tr( "Stop" ) );
    m_runBtn->setEnabled( true ); // Stop stays clickable while running
  }
  else
  {
    m_runBtn->setText( tr( "Run" ) );
    m_validationBlocked = m_form->hasErrors();
    updateRunButtonState();
  }
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
  // setValues is signal-silent, so the debounced async-check pass never
  // fires for restored parameters — force one so path checks stay current.
  m_form->runAsyncChecksNow();
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
