#include "app/pipeline/ir2_pipeline_designer_dock.h"
#include "app/pipeline/labspec_workflow_lift.h"

#include "workflow/ir2_registry_node_executor.h"

#include "widgets/lab_spec_loader.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

namespace sicnu::app::pipeline {

Ir2PipelineDesignerDock::Ir2PipelineDesignerDock( QWidget *parent )
    : QDockWidget( parent )
    , m_coordinator( new sicnu::workflow::PipelineRunCoordinator( this ) )
{
    // D18: bind RSOperatorRegistry (typed refusal when unbound). Do not leave
    // the D17 synthetic default active on the production dock path.
    m_coordinator->setExecutor( sicnu::workflow::makeRegistryNodeExecutor() );

    setObjectName( QStringLiteral( "Ir2PipelineDesignerDock" ) );
    setWindowTitle( tr( "Workflow Designer (IR 2.0)" ) );
    setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea );

    auto *host = new QWidget( this );
    auto *layout = new QVBoxLayout( host );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->setSpacing( 4 );

    auto *toolbar = new QHBoxLayout();
    auto *newBtn = new QPushButton( tr( "New" ), host );
    auto *labBtn = new QPushButton( tr( "Load LabSpec…" ), host );
    m_runButton = new QPushButton( tr( "Run" ), host );
    m_cancelButton = new QPushButton( tr( "Cancel" ), host );
    m_cancelButton->setEnabled( false );
    toolbar->addWidget( newBtn );
    toolbar->addWidget( labBtn );
    toolbar->addStretch( 1 );
    toolbar->addWidget( m_runButton );
    toolbar->addWidget( m_cancelButton );
    layout->addLayout( toolbar );

    m_identityLabel = new QLabel( host );
    m_identityLabel->setObjectName( QStringLiteral( "ir2WorkflowIdentity" ) );
    m_identityLabel->setWordWrap( true );
    m_identityLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    layout->addWidget( m_identityLabel );

    m_runStatusLabel = new QLabel( host );
    m_runStatusLabel->setObjectName( QStringLiteral( "ir2RunStatus" ) );
    m_runStatusLabel->setWordWrap( true );
    m_runStatusLabel->setText( tr( "Idle — Run uses PipelineRunCoordinator + operator registry (unbound nodes refuse)." ) );
    layout->addWidget( m_runStatusLabel );

    m_canvas = new PipelineCanvasWidget( host );
    layout->addWidget( m_canvas, /*stretch=*/1 );
    setWidget( host );

    newEmptyDocument();

    connect( newBtn, &QPushButton::clicked, this, &Ir2PipelineDesignerDock::onNewClicked );
    connect( labBtn, &QPushButton::clicked, this, &Ir2PipelineDesignerDock::onLoadLabSpecClicked );
    connect( m_runButton, &QPushButton::clicked, this, &Ir2PipelineDesignerDock::onRunClicked );
    connect( m_cancelButton, &QPushButton::clicked, this, &Ir2PipelineDesignerDock::onCancelClicked );

    connect( m_canvas, &PipelineCanvasWidget::nodeSelected, this, [this]( const QString & ) {
        syncDocumentFromCanvas();
        refreshIdentityLabel();
        emitIdentity();
    } );

    connect( m_coordinator, &sicnu::workflow::PipelineRunCoordinator::pipelineCompleted, this,
             [this]( bool success, const QString &summary ) {
                 m_runButton->setEnabled( true );
                 m_cancelButton->setEnabled( false );
                 m_runStatusLabel->setText(
                     success ? tr( "Completed: %1" ).arg( summary )
                             : tr( "Failed/cancelled: %1" ).arg( summary ) );
                 emit pipelineRunFinished( success, summary, m_coordinator->checkpointPath() );
             } );
}

sicnu::app::ActiveWorkflowRef Ir2PipelineDesignerDock::activeWorkflowRef() const
{
    sicnu::app::ActiveWorkflowRef ref;
    ref.workflowId = m_document.workflowId;
    ref.name = m_document.name;
    ref.schemaVersion = QStringLiteral( "2.0" );
    ref.fingerprint = workflowIr2ContentFingerprint( m_document );
    ref.runner = QStringLiteral( "pipeline_run_coordinator" );
    return ref;
}

void Ir2PipelineDesignerDock::loadDocument( const sicnu::workflow::WorkflowDocument &def )
{
    m_document = def;
    if ( m_document.workflowId.isEmpty() )
        m_document.workflowId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    m_canvas->loadWorkflow( m_document );
    refreshIdentityLabel();
    emitIdentity();
}

void Ir2PipelineDesignerDock::newEmptyDocument( const QString &name )
{
    sicnu::workflow::WorkflowDocument def;
    def.version = QStringLiteral( "2.0" );
    def.workflowId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    def.name = name;
    loadDocument( def );
}

bool Ir2PipelineDesignerDock::loadLabSpecFile( const QString &labSpecJsonPath, QString *error )
{
    lab::LabSpecError loadErr;
    const lab::LabSpec spec = lab::loadLabSpecFile( labSpecJsonPath, &loadErr );
    if ( !loadErr.reason.isEmpty() )
    {
        if ( error )
            *error = loadErr.toString();
        return false;
    }
    if ( spec.id.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "LabSpec load failed: %1" ).arg( labSpecJsonPath );
        return false;
    }

    const auto lifted = liftLabSpecToWorkflow( spec );
    QString semErr;
    if ( !sicnu::workflow::WorkflowIR::validateSemantics( lifted, &semErr ) && !semErr.isEmpty() )
    {
        // Empty graphs (guidance-only labs) may still be useful for identity;
        // only refuse when the lift produced a structurally illegal graph.
        if ( !lifted.nodes.isEmpty() )
        {
            if ( error )
                *error = semErr;
            return false;
        }
    }
    loadDocument( lifted );
    m_runStatusLabel->setText( tr( "Loaded LabSpec → IR 2.0 · %1" ).arg( lifted.name ) );
    return true;
}

bool Ir2PipelineDesignerDock::startPipelineRun( const QString &runDirectory, QString *error )
{
    if ( m_coordinator->isRunning() )
    {
        if ( error )
            *error = QStringLiteral( "pipeline already running" );
        return false;
    }
    syncDocumentFromCanvas();
    refreshIdentityLabel();
    emitIdentity();

    QString semErr;
    if ( !sicnu::workflow::WorkflowIR::validateSemantics( m_document, &semErr ) )
    {
        // Allow empty starter documents: coordinator completes synchronously.
        if ( !m_document.nodes.isEmpty() )
        {
            if ( error )
                *error = semErr.isEmpty() ? QStringLiteral( "semantic validation failed" ) : semErr;
            return false;
        }
    }

    const QString dir = runDirectory.isEmpty() ? defaultRunDirectory() : runDirectory;
    QDir().mkpath( dir );
    // Announce start before startRun: empty documents complete synchronously
    // and would otherwise emit pipelineCompleted before pipelineRunStarted.
    emit pipelineRunStarted( dir );
    m_runButton->setEnabled( false );
    m_cancelButton->setEnabled( true );
    m_runStatusLabel->setText( tr( "Running in %1 …" ).arg( dir ) );

    QString startErr;
    if ( !m_coordinator->startRun( m_document, dir, &startErr ) )
    {
        m_runButton->setEnabled( true );
        m_cancelButton->setEnabled( false );
        m_runStatusLabel->setText( tr( "Start failed: %1" ).arg( startErr ) );
        if ( error )
            *error = startErr;
        return false;
    }
    if ( m_coordinator->hasCompleted() )
    {
        m_runButton->setEnabled( true );
        m_cancelButton->setEnabled( false );
    }
    return true;
}

void Ir2PipelineDesignerDock::requestPipelineCancel()
{
    m_coordinator->requestCancel();
}

void Ir2PipelineDesignerDock::onRunClicked()
{
    QString err;
    if ( !startPipelineRun( {}, &err ) )
    {
        QMessageBox::warning( this, tr( "Pipeline Run" ),
                              tr( "Could not start PipelineRunCoordinator:\n%1" ).arg( err ) );
        m_runStatusLabel->setText( tr( "Start failed: %1" ).arg( err ) );
    }
}

void Ir2PipelineDesignerDock::onCancelClicked()
{
    requestPipelineCancel();
    m_runStatusLabel->setText( tr( "Cancel requested…" ) );
}

void Ir2PipelineDesignerDock::onLoadLabSpecClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "Load LabSpec (JSON)" ), QString(),
        tr( "LabSpec JSON (*.json);;All files (*)" ) );
    if ( path.isEmpty() )
        return;
    QString err;
    if ( !loadLabSpecFile( path, &err ) )
        QMessageBox::warning( this, tr( "LabSpec" ), err );
}

void Ir2PipelineDesignerDock::onNewClicked()
{
    newEmptyDocument();
    m_runStatusLabel->setText( tr( "New empty IR 2.0 document." ) );
}

void Ir2PipelineDesignerDock::refreshIdentityLabel()
{
    const auto ref = activeWorkflowRef();
    m_identityLabel->setText(
        tr( "IR 2.0 · %1 · id=%2 · fp=%3… · runner=pipeline_run_coordinator" )
            .arg( ref.name.isEmpty() ? tr( "(unnamed)" ) : ref.name )
            .arg( ref.workflowId )
            .arg( ref.fingerprint.left( 12 ) ) );
}

void Ir2PipelineDesignerDock::emitIdentity()
{
    emit workflowIdentityChanged( activeWorkflowRef() );
}

void Ir2PipelineDesignerDock::syncDocumentFromCanvas()
{
    m_document = m_canvas->exportWorkflow();
    if ( m_document.workflowId.isEmpty() )
        m_document.workflowId = QUuid::createUuid().toString( QUuid::WithoutBraces );
}

QString Ir2PipelineDesignerDock::defaultRunDirectory() const
{
    const QString base = QStandardPaths::writableLocation( QStandardPaths::TempLocation );
    const QString id = m_document.workflowId.isEmpty() ? QStringLiteral( "anon" ) : m_document.workflowId;
    return QDir( base ).filePath( QStringLiteral( "sicnu-ir2-runs/%1" ).arg( id ) );
}

} // namespace sicnu::app::pipeline
