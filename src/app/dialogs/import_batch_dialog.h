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

#include "data/asset_types.h"
#include "data/import_session.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QTimer;

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

    void reject() override;

    /// Pending paths recorded by a previous interrupted batch (published
    /// sicnu.import.session/1 journal schema). Empty when no journal exists
    /// or nothing is pending. The resume entry constructs a new batch from
    /// these paths and checks them against the current catalog.
    static QStringList pendingPathsFromJournal( const QString &journalPath );

    /// Default journal location (app state, atomic sidecar publish).
    static QString journalPath();

    // Test accessors (headless contract pinning).
    QListWidget *itemsView() const { return m_itemsView; }
    QLabel *summaryLabel() const { return m_summaryLabel; }
    QPushButton *importButton() const { return m_importButton; }
    QStringList registeredPaths() const { return m_registeredPaths; }
    QStringList refusedPaths() const { return m_refusedPaths; }
    QList<sicnu::data::AssetId> displayableAssets() const { return m_displayableAssets; }

  protected:
    void showEvent( QShowEvent *event ) override;

  private:
    void buildUi( const QStringList &sources );
    void beginImport();
    void advanceSession();
    void showReview( bool results );

    sicnu::data::ImportSession m_session;

    QLabel *m_heading = nullptr;
    QLabel *m_hint = nullptr;
    QListWidget *m_itemsView = nullptr;
    QLineEdit *m_path = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_stepTimer = nullptr;
    bool m_resultsShown = false;

    QStringList m_registeredPaths;
    QStringList m_refusedPaths;
    QList<sicnu::data::AssetId> m_displayableAssets;
};

} // namespace sicnu::app
