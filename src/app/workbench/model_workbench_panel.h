/***************************************************************************
 * model_workbench_panel.h — Workbench 7.0 model workbench (§F)
 *
 * Catalog / manifest / backend-device / compatibility / health view over the
 * authoritative ModelCatalog + ModelRuntime seams (goal §F). Read-only
 * projection: readiness is EVALUATED by the runtime layer
 * (evaluateRuntimeReadiness), never guessed here; unknown memory stays
 * unknown; test inference routes through TaskCenter's rs:infer operator (the
 * same seam as every other execution surface), so failures surface in the
 * processing history like any other run.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include <QPointer>
#include <QVector>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QLineEdit;

namespace sicnu::app
{

class ModelWorkbenchPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit ModelWorkbenchPanel( QWidget *parent = nullptr );

    /// Re-reads the catalog (models + issues) and re-evaluates readiness.
    void refreshCatalog();

  signals:
    /// Emitted after a test-inference task was submitted through TaskCenter.
    void inferenceSubmitted( long taskId );

  private slots:
    void reloadCatalog();
    void runTestInference();

  private:
    void rebuildModelTable();
    void showModelDetail();

    QTableWidget *m_modelTable = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_manifestView = nullptr;
    QPlainTextEdit *m_issuesView = nullptr;
    QPushButton *m_reloadBtn = nullptr;
    QPushButton *m_testInferenceBtn = nullptr;
    QComboBox *m_backendCombo = nullptr;
    QVector<std::string> m_modelNames; ///< filtered row → catalog name
    class QTimer *m_refreshTimer = nullptr;
};

} // namespace sicnu::app
