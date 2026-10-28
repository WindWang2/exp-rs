/***************************************************************************
 * cartography_dock.h — Workbench 10.0 cartography bridge (goal WP-B, C-1)
 *
 * The desktop surface for the MapSpec pipeline: pick a template from the
 * SAME catalog the agent tools use, seed a draft from the current
 * selection's map layers, then compose → preflight → repair → export.
 *
 * The dock implements nothing itself: every action runs through the
 * cartography:* RSOperator adapters (RSOperatorRegistry), which are the
 * SAME dispatch path a workflow node takes. That keeps GUI and headless
 * semantics identical by construction and honors the no-copied-compiler
 * rule (ui-architecture §2 / §9).
 *
 * Preview renders the compiled QgsPrintLayout through the vendored
 * QgsLayoutExporter at a bounded DPI into a scaled label — a projection,
 * never a second renderer.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include <QPointer>

#include <json/json.h>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QSpinBox;
class QgsPrintLayout;

namespace sicnu::app
{

class CartographyDock : public QgsDockWidget
{
    Q_OBJECT
  public:
    /// Current draft inputs: raster layer source paths (selection first,
    /// canvas order next) the template's map frame should show.
    using LayerSourcesProvider = std::function<QStringList()>;
    /// Project workdir for default export directory (empty when no project).
    using WorkDirProvider = std::function<QString()>;

    explicit CartographyDock( LayerSourcesProvider layers, WorkDirProvider workDir,
                              QWidget *parent = nullptr );

    /// Template id list currently offered (catalog order, for tests).
    QStringList templateIds() const;
    /// The last composed/repaired spec (empty JSON when none).
    Json::Value currentMapSpec() const { return m_currentSpec; }
    /// The last structured report text shown in the dock (for tests).
    QString lastReportText() const;

    /// Re-derives the action buttons' enabled/tooltip state from the live
    /// facts (busy, templates, layers, composed layout). Public so hosts
    /// that mutate the layer stack can ask for a refresh, and tests can pin
    /// the honest-gating contract.
    void refreshActionStates();

    /// Adopts a MapSpec drafted elsewhere (workflow preset, agent tool,
    /// test) as the dock's current draft — the handoff seam into the same
    /// compose → check → fix → export chain a user-driven draft takes.
    void adoptSpec( const Json::Value &spec );

  public slots:
    /// Builds a draft from the current inputs and runs cartography:compose.
    void runCompose();
    /// Emits openLayoutDesignerRequested for the composed layout (no-op with
    /// a status message when none exists — the button gating normally makes
    /// that unreachable).
    void openInLayoutDesigner();
    /// Runs cartography:preflight on the current/drafted spec.
    void runPreflight();
    /// Runs cartography:repair and adopts the repaired spec.
    void runRepair();
    /// Runs cartography:export with the selected format/directory/dpi.
    void runExport();
    /// Production 11.0: runs cartography:produce on the current spec
    /// (compose → bounded repair → export → manifest, atomic publish).
    void runProduce();
    /// Cancels the running cartography job (TaskCenter cooperative cancel).
    void cancelRunningJob();

  signals:
    /// Status-line friendly progress/feedback (the shell routes to the bar).
    void statusMessage( const QString &message );
    /// R25-D: the user asked to hand-tune the composed layout in the layout
    /// designer. The dock never opens windows (no-engine rule) — the shell
    /// owns the QgsLayoutDesignerDialog, exactly like project.newLayout.
    void openLayoutDesignerRequested( const QString &layoutName );

  private:
    void buildUi();
    void reloadTemplates();
    /// Template draft from current inputs (empty Json on unknown template).
    Json::Value buildDraft( QString *error ) const;
    /// Submits one of the cartography:* operators to TaskCenter (the same
    /// registry dispatch a workflow node takes) and reports progress/busy
    /// state. `onDone` runs on the GUI thread with the operator result (or
    /// an empty value and a typed error). Returns false when busy or
    /// rejected.
    bool submitOperatorJob( const QString &operatorId, const Json::Value &params,
                            const std::function<void( const Json::Value &, const QString & )>
                              &onDone );
    /// Disables the action buttons while a job runs.
    void setBusy( bool busy );
    /// Renders page 0 of the named compiled layout into the preview label.
    void updatePreview( const QString &layoutName );
    void showReport( const QString &heading, const Json::Value &payload );

    LayerSourcesProvider m_layers;
    WorkDirProvider m_workDir;

    QComboBox *m_templateCombo = nullptr;
    QLineEdit *m_layoutNameEdit = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_sourceNoteEdit = nullptr;
    QPushButton *m_composeBtn = nullptr;
    QPushButton *m_preflightBtn = nullptr;
    QPushButton *m_repairBtn = nullptr;
    QPushButton *m_designerBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_produceBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QSpinBox *m_dpiSpin = nullptr;
    QLineEdit *m_directoryEdit = nullptr;
    QLabel *m_previewLabel = nullptr;
    QPlainTextEdit *m_reportView = nullptr;

    Json::Value m_currentSpec;
    QString m_composedLayoutName;
    /// TaskCenter task id of the running job (-1 when idle). The dock runs
    /// one cartography job at a time — the operators already guard shared
    /// layout state, and a queue of UI clicks is not production.
    long m_runningTaskId = -1;
};

} // namespace sicnu::app
