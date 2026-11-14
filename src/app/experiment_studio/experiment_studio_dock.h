#pragma once

#include "experiment_studio/studio_session.h"
#include "experiment_studio/studio_submission.h"
#include "experiment_studio/run_matrix_metric.h"
#include "study/study_spec.h"

#include <qgsdockwidget.h>

#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
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
/// LIVE EXECUTION: when a live experiment store is opened, the dock drives
/// the REAL authorities through the studio_live bridge — StudyRunner on the
/// ExecutionPlane → TaskCenter spine (this dock creates NO scheduler), the
/// GDAL spatial summarizer, the real Capsule APIs. The session and export
/// bundles keep only REFS (run ids, capsule paths) — never capsule bodies.
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
    /// R30-T07 WP03: apply / round-trip a FULL ParameterStudySpec document.
    void applyAdvancedSpec();
    void loadAdvancedSpecFromFields();
    /// R30-T07 WP05: bounded matrix paging.
    void nextMatrixPage();
    void previousMatrixPage();
    /// R30-T07 WP08: bounded recorded-run browser (dialog wrapper).
    void openRecordedRunDialog();
    /// R30-T07 WP04: production delivery seam (also the offscreen test seam).
    /// A result carrying an expired submission id never overwrites a newer
    /// session. Returns true when the report was applied.
    bool deliverStudyResult( quint64 submissionId, const QJsonObject &reportJson,
                             int recordedCount, int failedCount, int cancelledCount,
                             const QString &error );

  public:
    /// Opens a live experiment store at an explicit path — the testable core
    /// of openLiveStore(). Refuses while a live study is in flight.
    bool openLiveStoreAtPath( const QString &path );

    /// D4 (R29-T04): offline reproduction-bundle import against the open
    /// live store. R30-T07 adds an options-carrying overload below.
    sicnu::experiment::ReproductionBundleImportReport importReproductionBundleAtPath(
        const QString &bundleDir, const QString &targetExperimentId, bool keepOriginalRunId,
        bool dryRun );

    /// R30-T07 WP07: the same engine, with relocation / conflict / capacity
    /// options exposed. The 4-arg overload delegates with safe defaults.
    struct BundleImportOverrides
    {
        QString relocateDataDir;
        bool overwriteExistingData = false;
        qint64 maxRelocateMemberBytes = 20LL * 1024 * 1024 * 1024;
    };
    sicnu::experiment::ReproductionBundleImportReport importReproductionBundleWithOptions(
        const QString &bundleDir, const QString &targetExperimentId, bool keepOriginalRunId,
        bool dryRun, const BundleImportOverrides &overrides );

    /// Machine-readable session snapshot — the Agent-facing surface and the
    /// offscreen test seam. UI thread only: every mutator runs there.
    sicnu::experiment_studio::StudioSessionState sessionState() const { return m_session; }

    QString studioSessionPath() const;
    /// Construction-time only: called mid-lifetime it would graft a foreign
    /// session onto whatever store/dock state is live.
    void restorePersistedSession();
    void savePersistedSession();
    /// Last typed persistence refusal (empty on success). Lets the UI and
    /// tests prove a failed save did NOT corrupt the previous document.
    QString lastSessionSaveError() const { return m_lastSessionSaveError; }
    QString lastSessionLoadCode() const { return m_lastSessionLoadCode; }

    /// R30-T07 test seams over otherwise-GUI-only projections.
    std::optional<sicnu::experiment_studio::StudioSubmission> activeSubmission() const
    {
        return m_activeSubmission;
    }
    sicnu::experiment_studio::RunMatrixPage matrixPage() const { return m_lastMatrixPage; }
    QJsonObject lastMetricCell( int row ) const;
    /// R30-T07 WP08: browse an already-recorded run in the open store and
    /// restore its refs without erasing the user's current selection.
    QJsonObject recordedRunReadiness( const QString &runId ) const;

  private:
    void rebuildMatrixTable();
    void rebuildChart();
    QJsonObject makeSyntheticStudyReport( int pointCount ) const;
    QJsonObject registryOperatorSchema( const QString &algorithmId,
                                        QString *determinismGradeOut = nullptr ) const;
    sicnu::study::ParameterStudySpec designerSpecFromFields( const QString &inputRaster ) const;
    QStringList validateDesignerSpec( const sicnu::study::ParameterStudySpec &spec,
                                      QString *determinismGradeOut = nullptr,
                                      QJsonObject *vmJsonOut = nullptr ) const;
    void applyLiveStudyResult( const QJsonObject &reportJson, int recordedCount, int failedCount,
                               int cancelledCount, const QString &error );
    void logTypedFailure( QPlainTextEdit *log, const QString &context, const QString &code,
                          const QString &message );
    bool liveBusy() const;
    /// Any designer field edit invalidates the last validated draft: a stale
    /// "valid" marker must never bless a changed design.
    void invalidateDesignerDraft();
    QString designerMetricProtocol() const;
    void refreshMatrixPageControls();

    QTabWidget *m_tabs = nullptr;

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
    QPlainTextEdit *m_advancedSpecEdit = nullptr;
    QLabel *m_designerSummary = nullptr;

    // B Matrix
    QTableWidget *m_matrixTable = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_matrixStatus = nullptr;
    QLabel *m_matrixPageLabel = nullptr;
    QPushButton *m_matrixPrevPage = nullptr;
    QPushButton *m_matrixNextPage = nullptr;
    int m_matrixPage = 0;
    QStringList m_matrixSelectedKeys;
    QVector<QJsonObject> m_lastMetricCells;
    sicnu::experiment_studio::RunMatrixPage m_lastMatrixPage;

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

    /// WP03: the full normalized spec from the advanced editor. Empty when
    /// the plain single-dimension form is in use.
    std::optional<sicnu::study::ParameterStudySpec> m_advancedSpec;
    /// WP04: one immutable identity per submitted run.
    quint64 m_nextSubmissionId = 1;
    std::optional<sicnu::experiment_studio::StudioSubmission> m_activeSubmission;
    /// WP02: typed persistence feedback.
    QString m_lastSessionSaveError;
    QString m_lastSessionLoadCode;
    /// Guards invalidation while restorePersistedSession() writes widgets.
    bool m_restoringSession = false;
};

} // namespace sicnu::app
