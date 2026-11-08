#pragma once

#include "experiment_studio/studio_session.h"
#include "study/study_spec.h"

#include <qgsdockwidget.h>

#include <QJsonObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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
struct ReproductionBundleImportReport;
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
/// runner and the real Capsule APIs. Without a store, the designer remains
/// available for validation and synthetic demos are opt-in via SICNU_UI_DEMO_MODE.
/// The session and export bundles keep only REFS
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
    void importReproductionBundleDialog();

  public:
    /// Opens a live experiment store at an explicit path — the testable core
    /// of openLiveStore() (which only adds the file dialog). Refuses with a
    /// typed failure while a live study is in flight.
    bool openLiveStoreAtPath( const QString &path );

    /// D4 (R29-T04): offline reproduction-bundle import against the open
    /// live store — the dialog-free core the "Import Reproduction Bundle…"
    /// button wraps (dry-run precheck → confirm → import). Returns an
    /// ok=false report when no store is open or one is busy; the caller
    /// surfaces report warnings/conflicts verbatim. The engine
    /// (ReproductionBundleImporter) owns every gate: integrity, schema,
    /// identity, capacity, relocation conflicts.
    sicnu::experiment::ReproductionBundleImportReport importReproductionBundleAtPath(
        const QString &bundleDir, const QString &targetExperimentId, bool keepOriginalRunId,
        bool dryRun );

    /// Machine-readable session snapshot — the Agent-facing surface and the
    /// offscreen test seam for the honesty contracts (synthetic markers, run
    /// refs, export path). Capsule bodies never enter the session (refs only);
    /// `lastStudyReport` may carry the full report document by design. UI
    /// thread only: every mutator runs there.
    sicnu::experiment_studio::StudioSessionState sessionState() const { return m_session; }

    /// Persisted-session support (D4, R29-T04). The studio saves its
    /// session (design draft, last report, run refs, tab) at these paths and
    /// restores it on construction; a damaged or foreign-version document
    /// is refused with an explicit note, never partially adopted.
    QString studioSessionPath() const;
    void restorePersistedSession();
    void savePersistedSession();

  private:
    void rebuildMatrixTable();
    void rebuildChart();
    QJsonObject makeSyntheticStudyReport( int pointCount ) const;
    /// The REAL parameter schema of @p algorithmId from the operator
    /// registry (jsoncpp → QJson round-trip). Returns an empty object when
    /// the algorithm is not registered; @p determinismGradeOut (optional)
    /// receives the operator's declared grade on success.
    QJsonObject registryOperatorSchema( const QString &algorithmId,
                                        QString *determinismGradeOut = nullptr ) const;
    /// The ONE normalized spec construction from the designer fields
    /// (R29-T04 D1): design validation and live submission both consume
    /// THIS spec, so the validated design IS the run spec — one identity,
    /// one fingerprint.
    sicnu::study::ParameterStudySpec designerSpecFromFields( const QString &inputRaster ) const;
    /// The one design gate over a normalized spec: registry schema +
    /// real-input existence + policy/core validation. Returns the typed
    /// issue list (empty = valid); @p vmJsonOut (optional) receives the
    /// designer view model on success. The designer summary and the run
    /// refusal share this single truth.
    QStringList validateDesignerSpec( const sicnu::study::ParameterStudySpec &spec,
                                      QString *determinismGradeOut = nullptr,
                                      QJsonObject *vmJsonOut = nullptr ) const;
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
    std::shared_ptr<std::atomic<bool>> m_aliveFlag = std::make_shared<std::atomic<bool>>( true );
    std::unique_ptr<std::thread> m_liveRunThread;

    struct RetiredStudyThread
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex m_retiredStudyMutex;
    std::vector<RetiredStudyThread> m_retiredStudyThreads;
    void pruneRetiredStudyThreadsLocked();

    QPushButton *m_runLiveBtn = nullptr;
    QPushButton *m_openStoreBtn = nullptr;
    QPushButton *m_importBundleBtn = nullptr;

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
    QLabel *m_designerSummary = nullptr;

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
