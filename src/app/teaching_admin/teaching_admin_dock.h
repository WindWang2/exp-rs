// teaching_admin_dock.h — Teacher Authoring & Assessment Console dock (tabs A–I).
#pragma once

#include <QWidget>

class QTabWidget;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;

namespace sicnu::teaching_admin {
struct OperatorCatalog;
}

namespace sicnu::app::teaching_admin {

class TeachingAdminDock : public QWidget
{
    Q_OBJECT
  public:
    explicit TeachingAdminDock( QWidget *parent = nullptr );

  private slots:
    void onValidateCurriculum();
    void onValidateLabSpec();
    void onValidateRubric();
    void onInventoryPacks();
    void onRunPreflight();
    void onVerifyBundle();
    void onRunBatch();
    void onBuildFeedbackAndSummary();

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
};

} // namespace sicnu::app::teaching_admin
