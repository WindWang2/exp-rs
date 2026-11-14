// teaching_admin_dock.h — Teacher Authoring & Assessment Console dock (tabs A–I).
#pragma once

#include <QWidget>

#include <QJsonObject>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class QTabWidget;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;

namespace sicnu::teaching_admin {
struct OperatorCatalog;
}

namespace sicnu::app::teaching_admin {

class TeachingAdminDock : public QWidget
{
    Q_OBJECT
  public:
    explicit TeachingAdminDock( QWidget *parent = nullptr );
    ~TeachingAdminDock() override;

  private slots:
    void onValidateCurriculum();
    void onValidateLabSpec();
    void onValidateRubric();
    void onInventoryPacks();
    void onCheckPackDrift();
    void onRunPreflight();
    void onVerifyBundle();
    void onRunBatch();
    void onCancelBatch();
    void onBuildFeedbackAndSummary();
    void onChooseFeedbackDestination();
    void onDeliverFeedbackLocally();
  public:
    QJsonObject lastFeedbackDelivery() const { return m_lastFeedbackDelivery; }

  private:

  private:
    void appendLog( const QString &text );
    QString repoRoot() const;
    sicnu::teaching_admin::OperatorCatalog loadOperatorCatalog() const;
    void refreshOperatorRegistryLabel();

    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_log = nullptr;

    // Course builder
    QPlainTextEdit *m_curriculumEdit = nullptr;
    QLineEdit *m_labsDirEdit = nullptr;

    // Lab authoring
    QPlainTextEdit *m_labSpecEdit = nullptr;
    QLabel *m_operatorRegistryLabel = nullptr;

    // Rubric
    QPlainTextEdit *m_rubricEdit = nullptr;
    QLabel *m_rubricKindHint = nullptr;

    // Packs
    QLineEdit *m_packsDirEdit = nullptr;

    // Preflight / release
    QLineEdit *m_softwareVersionEdit = nullptr;

    // Offline
    QLineEdit *m_bundleDirEdit = nullptr;

    // Batch
    QLineEdit *m_submissionsDirEdit = nullptr;
    QLineEdit *m_labIdEdit = nullptr;
    QLineEdit *m_batchOutEdit = nullptr;
    /// R30-T07 WP11: explicit LOCAL destination for guarded feedback packs.
    QLineEdit *m_feedbackOutEdit = nullptr;
    QPushButton *m_deliverFeedbackButton = nullptr;
    /// Test seam: last delivery result (never used to fabricate success).
    QJsonObject m_lastFeedbackDelivery;
    /// Cached guard-passed emission + the REAL batch evidence it came from.
    QVector<QJsonObject> m_lastFeedbackPacks;
    QJsonObject m_lastFeedbackSummary;
    QStringList m_lastFeedbackBlocked;
    QStringList m_lastFeedbackRefusals;
    QStringList m_lastFeedbackCohort;
    QString m_lastFeedbackBatchPath;
    QString m_lastFeedbackConfigDigest;
    // The batch runs on a worker thread: the UI stays responsive, the Cancel
    // button flips a flag the orchestrator checks before each item (completed
    // rows are kept, never-started rows are typed cancelled — never disguised
    // failures), and progress is the durable checkpoint's row count.
    // Run button + out-prefix edit disable for the duration of a run (#1453/
    // WP02): the run is armed exactly while a batch can actually start, and
    // the summary reads the prefix the run STARTED with, never a mid-run edit.
    QPushButton *m_batchRunButton = nullptr;
    QPushButton *m_batchCancelButton = nullptr;
    QLabel *m_batchProgressLabel = nullptr;
    std::atomic<bool> m_batchCancel{ false };
    std::thread m_batchThread;
    // Identity of the grader the live batch shells out to (gradeViaCli →
    // runScript: `program lab --grade <artifact under submissionsDir>`).
    // Destruction SIGKILLs only that direct child so the join cannot sit out
    // the CLI timeout. Empty when no batch has been armed.
    QString m_batchGraderProgram;
    QString m_batchSubmissionsDir;

    struct RetiredThread
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex m_retiredMutex;
    std::vector<RetiredThread> m_retiredThreads;
    void pruneRetiredThreadsLocked();

    void finishBatchOnUiThread( QJsonObject report, const QString &outPrefix );
    /// Joins + re-arms the console after a failed batch. The worker's
    /// exception path must land here too: joinable() staying true after a
    /// failure used to wedge every later click behind a false "already
    /// running" refusal (#1453/WP02).
    void failBatchOnUiThread( const QString &why );
};

} // namespace sicnu::app::teaching_admin
