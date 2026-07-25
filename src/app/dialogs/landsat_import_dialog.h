// src/app/dialogs/landsat_import_dialog.h
#pragma once

#include <QDialog>

#include "processing/framework/collection_import_service.h"

namespace sicnu::data { class DataManager; }

class QLineEdit;
class QPushButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Landsat complex-product import dialog: the first UI caller of the
 * collection-import probe-preview-commit transaction (#51/#52).
 *
 * Flow: the user picks a Landsat scene directory, clicks Probe (or confirms the
 * path) to run the read-only probe, reviews the discovered child candidates
 * (bands / grid groups) in a checkable preview tree, and clicks Import to run
 * the atomic commit - registering a Data Collection grouping the selected band
 * children (no flattening to one stacked raster). Cancel registers nothing: the
 * probe is read-only, and commit only runs on Import.
 *
 * probe() and commitSelection() are public and callable without exec(), so the
 * dialog's transaction logic is testable headlessly. The widget itself is a
 * thin shell over CollectionImportService.
 */
class LandsatImportDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit LandsatImportDialog( QWidget *parent = nullptr );

    /// Supplies the Data Manager so the import commits into the catalog.
    /// Required: without a Data Manager the Import button is disabled (there is
    /// no catalog to register into). Mirrors SpectralIndexDialog::setDataManager.
    void setDataManager( sicnu::data::DataManager *dataManager );

    /// Sets the source directory path and (optionally) runs the probe.
    void setSourcePath( const QString &path, bool autoProbe = false );

    /// Runs the read-only probe against the current source path and fills the
    /// preview tree. Returns true when the probe succeeded (the catalog is
    /// unchanged regardless). Callable headlessly.
    bool probe();

    /// Commits the currently-checked child candidates as a Data Collection.
    /// Returns the committed CollectionId (null on failure; diagnostics are
    /// available via lastError()). Callable headlessly.
    sicnu::data::CollectionId commitSelection();

    /// Number of child candidates shown in the preview tree (top-level rows).
    int previewCount() const;

    /// True when the preview tree row at `index` is checked.
    bool isChildChecked( int index ) const;

    /// Sets the checked state of preview tree row at `index`.
    void setChildChecked( int index, bool checked );

    /// The last probe/commit failure message, or empty.
    QString lastError() const;

    /// The CollectionId committed by the most recent successful
    /// commitSelection(), or null if none.
    sicnu::data::CollectionId committedCollectionId() const;

  private slots:
    void onBrowse();
    void onProbe();
    void onImport();

  private:
    void setupUi();
    /// Populates the preview tree from `m_preview`, checking all rows.
    void populatePreview();
    /// The indices into `m_preview.children` that are currently checked.
    QVector<int> checkedChildIndices() const;

    sicnu::data::DataManager *m_dataManager = nullptr; // not owned
    sicnu::ImportPreview m_preview;
    QString m_lastError;
    sicnu::data::CollectionId m_committedCollectionId;

    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_probeButton = nullptr;
    QTreeWidget *m_previewTree = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};
