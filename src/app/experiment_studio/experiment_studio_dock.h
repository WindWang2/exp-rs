#pragma once

#include "experiment_studio/studio_session.h"

#include <qgsdockwidget.h>

#include <QJsonObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{
class ExperimentStore;
class MatrixLedger;
}

namespace sicnu::app::experiment_studio
{
class SensitivityChartWidget;
}

namespace sicnu::app
{

/// Experiment Exploration Studio dock — teaching projection over study /
/// faultlab / debugger. Owns no compute; machine-readable VMs for Agent later.
///
/// LIVE EXECUTION (completion/experiment-studio-live-execution): when a live
/// experiment store is opened, the dock drives the REAL authorities through
/// the studio_live bridge — StudyRunner on the ExecutionPlane → TaskCenter
/// spine (this dock creates NO scheduler; its single worker thread only
/// WAITS on StudyRunner and raises the cooperative cancel flag), the GDAL
/// spatial summarizer, the debugger evidence source, the faultlab sandbox
/// runner and the real Capsule APIs. Without a store the tabs stay honest
/// demos, labeled as such. The session and export bundles keep only REFS
/// (run ids, capsule paths) — never capsule bodies or report data.
class ExperimentStudioDock : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit ExperimentStudioDock( QWidget *parent = nullptr );
    ~ExperimentStudioDock() override;

  public slots:
    void loadDemoThresholdStudy();
    void validateDesigner();
    void applySyntheticRunMatrix();
    void compareSelectedSpatial();
    void refreshSensitivity();
    void runFaultTeachingDemo();
    void loadFirstDivergenceDemo();
    void exportBundle();
    void cancelLiveStudy();
    void openLiveStore();
    void runLiveStudy();

  public:
    /// Opens a live experiment store at an explicit path — the testable core
    /// of openLiveStore() (which only adds the file dialog). Refuses with a
    /// typed failure while a live study is in flight.
    bool openLiveStoreAtPath( const QString &path );

    /// Machine-readable session snapshot — the Agent-facing surface and the
    /// offscreen test seam for the honesty contracts (synthetic markers, run
    /// refs, export path). Capsule bodies never enter the session (refs only);
    /// `lastStudyReport` may carry the full report document by design. UI
    /// thread only: every mutator runs there.
    sicnu::experiment_studio::StudioSessionState sessionState() const { return m_session; }

  private:
    void rebuildMatrixTable();
    void rebuildChart();
    QJsonObject makeDemoOperatorSchema() const;
    QJsonObject makeSyntheticStudyReport( int pointCount ) const;
    void applyLiveStudyResult( const QJsonObject &reportJson, int recordedCount, int failedCount,
                               int cancelledCount, const QString &error );
    void logTypedFailure( QPlainTextEdit *log, const QString &context, const QString &code,
                          const QString &message );
    bool liveBusy() const;

    QTabWidget *m_tabs = nullptr;

    // Live execution state. The store/ledger/datasets are shared with the
    // worker thread (kept alive by shared_ptr even if the dock closes during
    // a run); the cancel flag is the StudyRunner's cooperative cancel.
    std::shared_ptr<sicnu::experiment::ExperimentStore> m_liveStore;
    std::shared_ptr<sicnu::experiment::MatrixLedger> m_liveLedger;
    std::unique_ptr<sicnu::dataset::DatasetStore> m_liveDatasets;
    QString m_liveStudyOutputDir;
    double m_liveSpatialEpsilon = 0.0;
    std::shared_ptr<std::atomic<bool>> m_liveCancel;
    std::unique_ptr<std::thread> m_liveRunThread;
    QPushButton *m_runLiveBtn = nullptr;
    QPushButton *m_openStoreBtn = nullptr;

    // A Designer
    QLineEdit *m_algorithmEdit = nullptr;
    QComboBox *m_strategyCombo = nullptr;
    QLineEdit *m_paramPathEdit = nullptr;
    QDoubleSpinBox *m_minSpin = nullptr;
    QDoubleSpinBox *m_maxSpin = nullptr;
    QSpinBox *m_stepsSpin = nullptr;
    QSpinBox *m_maxRunsSpin = nullptr;
    QSpinBox *m_replicatesSpin = nullptr;
    QLineEdit *m_metricEdit = nullptr;
    QLineEdit *m_inputRasterEdit = nullptr;
    QPlainTextEdit *m_designerLog = nullptr;

    // B Matrix
    QTableWidget *m_matrixTable = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_matrixStatus = nullptr;

    // C Spatial
    QPlainTextEdit *m_spatialLog = nullptr;

    // D Sensitivity
    sicnu::app::experiment_studio::SensitivityChartWidget *m_chart = nullptr;
    QPlainTextEdit *m_sensitivityLog = nullptr;

    // E Fault
    QPlainTextEdit *m_faultLog = nullptr;
    QLineEdit *m_faultPrediction = nullptr;

    // F Divergence
    QPlainTextEdit *m_divergenceLog = nullptr;

    // G Export
    QPlainTextEdit *m_exportLog = nullptr;

    sicnu::experiment_studio::StudioSessionState m_session;
    QJsonObject m_lastDesignerVm;
    QJsonObject m_lastStudyReport;
    QJsonObject m_lastFaultVm;
    QJsonObject m_lastDivergenceVm;
    QJsonObject m_lastSpatialVm;
    QJsonObject m_lastMatrixVm;
    QJsonObject m_lastSensitivityVm;
};

} // namespace sicnu::app
