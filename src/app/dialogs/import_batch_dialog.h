// src/app/dialogs/import_batch_dialog.h — batch import review over the
// EXISTING import transaction (R28-T04 WP03).
//
// The Add-Raster/Vector loop used to register every file synchronously and
// report only "N succeeded, M failed" in the status bar: no per-item
// reason, no needs-choice handling, no dedup explanation, no resume entry.
// This dialog drives T02's ImportSession (check → continue-import →
// journal) read-only and renders its state: one row per source with the
// ImportPreflight verdict, the first actionable finding, and the dedup
// prediction. Refused and needs-choice sources are listed with typed
// reasons and never registered. No second import database: the catalog
// stays the truth, the journal stays the resume point.
#pragma once

#include <QDialog>
#include <QStringList>

#include "data/import_session.h"

#include <memory>

class QLabel;
class QListWidget;
class QPushButton;

namespace sicnu::app
{

class ImportBatchDialog : public QDialog
{
    Q_OBJECT

  public:
    /// @p journalPath overrides the default app-state journal (tests).
    explicit ImportBatchDialog( sicnu::data::DataManager *manager,
                                const QStringList &sources,
                                QWidget *parent = nullptr,
                                const QString &journalPath = QString() );

    /// Phase 1 — 检查: preflights every source (bounded metadata reads,
    /// ImportPreflight under the session) and populates the rows. Must run
    /// before exec(); public for headless testing.
    void runCheck();

    /// Phase 2 — 继续导入: registers the admissible sources through the
    /// session (journal after each item) and refreshes the rows with the
    /// per-item outcome. Runs on accept(); public for headless testing.
    void runImport();

    /// Pending paths recorded by a previous interrupted batch (published
    /// sicnu.import.session/1 journal schema). Empty when no journal exists
    /// or nothing is pending. The resume entry constructs a new batch from
    /// exactly these paths; the session re-verifies already-registered
    /// items against the catalog (idempotent resume).
    static QStringList pendingPathsFromJournal( const QString &journalPath );

    /// Default journal location (app state, atomic sidecar publish).
    static QString journalPath();

    // Test accessors (headless contract pinning).
    QListWidget *itemsView() const { return m_itemsView; }
    QLabel *summaryLabel() const { return m_summaryLabel; }
    QPushButton *importButton() const { return m_importButton; }
    QStringList registeredPaths() const { return m_registeredPaths; }
    QStringList refusedPaths() const { return m_refusedPaths; }

  private:
    void buildUi();
    void refreshRowsFromSession( bool afterImport );

    sicnu::data::DataManager *m_manager = nullptr;
    QStringList m_sources;
    QString m_journalPath;
    std::unique_ptr<sicnu::data::ImportSession> m_session;

    QListWidget *m_itemsView = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QStringList m_registeredPaths;
    QStringList m_refusedPaths;
};

} // namespace sicnu::app
