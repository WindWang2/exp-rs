/***************************************************************************
 * ir2_pipeline_designer_dock.h — D18 production mount for D17 IR 2.0 canvas
 *
 * Hosts PipelineCanvasWidget (Workflow IR 2.0) without including Engine 2.0
 * WorkflowDefinition headers in the same TU (D-W1 name clash). Emits an
 * ActiveWorkflowRef so MissionContext / Agent share one document identity.
 *
 * D18: starts PipelineRunCoordinator against the shared document and can
 * lift Guided LabSpec JSON into the same IR 2.0 identity.
 ***************************************************************************/
#pragma once

#include <QDockWidget>
#include <QCryptographicHash>
#include <QJsonDocument>

#include "app/workbench/mission_context.h"
#include "app/pipeline/pipeline_canvas_widget.h"
#include "workflow/workflow_ir_v2.h"
#include "workflow/pipeline_run_coordinator.h"

class QLabel;
class QPushButton;

namespace sicnu::app::pipeline {

class Ir2PipelineDesignerDock : public QDockWidget
{
    Q_OBJECT

  public:
    explicit Ir2PipelineDesignerDock( QWidget *parent = nullptr );

    PipelineCanvasWidget *canvas() const { return m_canvas; }
    sicnu::workflow::PipelineRunCoordinator *runCoordinator() const { return m_coordinator; }

    /// Current document handle for MissionContext / Agent (id + fingerprint).
    sicnu::app::ActiveWorkflowRef activeWorkflowRef() const;

    /// Current IR 2.0 document (exported from canvas when dirty).
    /// Prefer WorkflowDocument alias in new code (D-W3).
    sicnu::workflow::WorkflowDocument currentDocument() const { return m_document; }

    /// Replace the canvas document and refresh identity.
    void loadDocument( const sicnu::workflow::WorkflowDocument &def );

    /// Empty starter document with a fresh workflow id.
    void newEmptyDocument( const QString &name = QStringLiteral( "Untitled pipeline" ) );

    /// Lift a LabSpec 1.0 JSON file into the shared IR 2.0 document.
    /// Fail-closed: returns false and leaves the prior document untouched.
    bool loadLabSpecFile( const QString &labSpecJsonPath, QString *error = nullptr );

    /// Start PipelineRunCoordinator on the current document (shared ActiveWorkflowRef).
    bool startPipelineRun( const QString &runDirectory = {}, QString *error = nullptr );

    void requestPipelineCancel();

  signals:
    /// Fired whenever the active document identity changes (load / new / export).
    void workflowIdentityChanged( const sicnu::app::ActiveWorkflowRef &ref );
    void pipelineRunStarted( const QString &runDirectory );
    void pipelineRunFinished( bool success, const QString &summary, const QString &checkpointPath );

  private slots:
    void onRunClicked();
    void onCancelClicked();
    void onLoadLabSpecClicked();
    void onNewClicked();

  private:
    void refreshIdentityLabel();
    void emitIdentity();
    void syncDocumentFromCanvas();
    QString defaultRunDirectory() const;

    PipelineCanvasWidget *m_canvas = nullptr;
    QLabel *m_identityLabel = nullptr;
    QLabel *m_runStatusLabel = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    sicnu::workflow::WorkflowDocument m_document;
    sicnu::workflow::PipelineRunCoordinator *m_coordinator = nullptr;
};

inline QString workflowIr2ContentFingerprint( const sicnu::workflow::WorkflowDocument &def )
{
    const QJsonObject obj = sicnu::workflow::WorkflowIR::toJson( def );
    const QByteArray bytes = QJsonDocument( obj ).toJson( QJsonDocument::Compact );
    return QString::fromLatin1(
        QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

} // namespace sicnu::app::pipeline
