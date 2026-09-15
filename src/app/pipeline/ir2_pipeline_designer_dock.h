/***************************************************************************
 * ir2_pipeline_designer_dock.h — D18 production mount for D17 IR 2.0 canvas
 *
 * Hosts PipelineCanvasWidget (Workflow IR 2.0) without including Engine 2.0
 * WorkflowDefinition headers in the same TU (D-W1 name clash). Emits an
 * ActiveWorkflowRef so MissionContext / Agent share one document identity.
 ***************************************************************************/
#pragma once

#include <QDockWidget>
#include <QCryptographicHash>
#include <QJsonDocument>

#include "app/workbench/mission_context.h"
#include "app/pipeline/pipeline_canvas_widget.h"
#include "workflow/workflow_ir_v2.h"

class QLabel;

namespace sicnu::app::pipeline {

class Ir2PipelineDesignerDock : public QDockWidget
{
    Q_OBJECT

  public:
    explicit Ir2PipelineDesignerDock( QWidget *parent = nullptr );

    PipelineCanvasWidget *canvas() const { return m_canvas; }

    /// Current document handle for MissionContext / Agent (id + fingerprint).
    sicnu::app::ActiveWorkflowRef activeWorkflowRef() const;

    /// Replace the canvas document and refresh identity.
    void loadDocument( const sicnu::workflow::WorkflowDefinition &def );

    /// Empty starter document with a fresh workflow id.
    void newEmptyDocument( const QString &name = QStringLiteral( "Untitled pipeline" ) );

  signals:
    /// Fired whenever the active document identity changes (load / new / export).
    void workflowIdentityChanged( const sicnu::app::ActiveWorkflowRef &ref );

  private:
    void refreshIdentityLabel();
    void emitIdentity();

    PipelineCanvasWidget *m_canvas = nullptr;
    QLabel *m_identityLabel = nullptr;
    sicnu::workflow::WorkflowDefinition m_document;
};

inline QString workflowIr2ContentFingerprint( const sicnu::workflow::WorkflowDefinition &def )
{
    const QJsonObject obj = sicnu::workflow::WorkflowIR::toJson( def );
    const QByteArray bytes = QJsonDocument( obj ).toJson( QJsonDocument::Compact );
    return QString::fromLatin1(
        QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

} // namespace sicnu::app::pipeline
