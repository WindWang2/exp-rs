#include "app/pipeline/ir2_pipeline_designer_dock.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QUuid>
#include <QWidget>

namespace sicnu::app::pipeline {

Ir2PipelineDesignerDock::Ir2PipelineDesignerDock( QWidget *parent )
    : QDockWidget( parent )
{
    setObjectName( QStringLiteral( "Ir2PipelineDesignerDock" ) );
    setWindowTitle( tr( "Workflow Designer (IR 2.0)" ) );
    setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea );

    auto *host = new QWidget( this );
    auto *layout = new QVBoxLayout( host );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->setSpacing( 4 );

    m_identityLabel = new QLabel( host );
    m_identityLabel->setObjectName( QStringLiteral( "ir2WorkflowIdentity" ) );
    m_identityLabel->setWordWrap( true );
    m_identityLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    layout->addWidget( m_identityLabel );

    m_canvas = new PipelineCanvasWidget( host );
    layout->addWidget( m_canvas, /*stretch=*/1 );
    setWidget( host );

    newEmptyDocument();

    connect( m_canvas, &PipelineCanvasWidget::nodeSelected, this, [this]( const QString & ) {
        // Geometry edits land on export; keep identity label in sync with export.
        m_document = m_canvas->exportWorkflow();
        refreshIdentityLabel();
        emitIdentity();
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

void Ir2PipelineDesignerDock::loadDocument( const sicnu::workflow::WorkflowDefinition &def )
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
    sicnu::workflow::WorkflowDefinition def;
    def.version = QStringLiteral( "2.0" );
    def.workflowId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    def.name = name;
    loadDocument( def );
}

void Ir2PipelineDesignerDock::refreshIdentityLabel()
{
    const auto ref = activeWorkflowRef();
    m_identityLabel->setText(
        tr( "IR 2.0 · %1 · id=%2 · fp=%3…" )
            .arg( ref.name.isEmpty() ? tr( "(unnamed)" ) : ref.name )
            .arg( ref.workflowId )
            .arg( ref.fingerprint.left( 12 ) ) );
}

void Ir2PipelineDesignerDock::emitIdentity()
{
    emit workflowIdentityChanged( activeWorkflowRef() );
}

} // namespace sicnu::app::pipeline
