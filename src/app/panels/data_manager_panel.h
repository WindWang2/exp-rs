#pragma once

#include <optional>

#include <QDockWidget>
#include <QTimer>
#include <QList>
#include <QString>

#include "data/asset_types.h"
#include "data/collection_types.h"
#include "asset_catalog_index.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;
class QSplitter;
class QLabel;
class QLineEdit;
class QStackedWidget;

namespace sicnu
{
class RsEmptyStateWidget;
}

namespace sicnu::data
{
class DataManager;
class AssetSnapshot;
}

namespace sicnu::app
{
class AssetPreviewService;
}

namespace sicnu
{

/**
 * Project Data Manager panel — catalog projection + metadata inspector.
 *
 * Top: tree of Data Assets and Collections (multi-select for batch actions).
 *   Name cell: status color bar + kind icon/prefix + display name
 *   (no separate kind/status columns).
 * Bottom: metadata inspector for the current selection, with a lazy bounded
 * preview (Professional Workbench 8.0, package E): raster thumbnails /
 * vector previews render through AssetPreviewService on the bounded scan
 * pool — the GUI never blocks on GDAL reads, a selection change supersedes
 * an in-flight preview, and panel teardown drops pending results.
 * Shell wires display / unload / promote signals.
 */
class DataManagerPanel : public QDockWidget
{
    Q_OBJECT

  public:
    explicit DataManagerPanel( sicnu::data::DataManager *dataManager,
                               QWidget *parent = nullptr );
    ~DataManagerPanel() override = default;

    int rowCount() const;

    QString rowText( sicnu::data::AssetId id, int column ) const;

    /// First selected asset (if any). Prefer selectedAssetIds() for multi-select.
    sicnu::data::AssetId selectedAssetId() const;

    /// All selected asset rows (collection parent rows are skipped).
    QList<sicnu::data::AssetId> selectedAssetIds() const;

    /// HTML currently shown in the metadata inspector (for tests).
    QString detailHtml() const;

    void selectAsset( sicnu::data::AssetId id );

    void activateAsset( sicnu::data::AssetId id );

    void requestRemove( sicnu::data::AssetId id );

    void requestPromote( sicnu::data::AssetId id );

    void refresh();

    /// Workbench 8.0: standalone-asset render cap (truthful truncation past
    /// it). Hosts may lower it for constrained displays; never silently —
    /// the sentinel row always names the exact totals.
    void setStandaloneRowCap( int maxRows );
    static constexpr int kDefaultStandaloneRowCap = 20000;

  signals:
    void importRequested();
    void displayRequested( sicnu::data::AssetId id );
    void unloadRequested( sicnu::data::AssetId id );
    /// Batch unload (multi-select). Shell should confirm once then unload each.
    void unloadRequestedMany( const QList<sicnu::data::AssetId> &ids );
    void promoteRequested( sicnu::data::AssetId id );
    /// Re-resolve a Missing/Unavailable asset at a new source location.
    void relocateRequested( sicnu::data::AssetId id );
    /// Asset-row selection changed (Workbench 5.0 SelectionContext source).
    void assetSelectionChanged( const QList<sicnu::data::AssetId> &ids );

  private slots:
    void onItemActivated( QTreeWidgetItem *item, int column );
    void onContextMenu( const QPoint &pos );
    void onSelectionChanged();
    /// Coalesced tree refresh (#704): a burst of N asset signals (batch
    /// import) used to trigger N FULL tree rebuilds on the GUI thread; a
    /// 250 ms trailing timer collapses the burst into one rebuild.
    void scheduleCoalescedRefresh();
    /// Workbench 8.0: collection children populate on first expand (lazy
    /// detail loading — huge temporal collections stay bounded).
    void onItemExpanded( QTreeWidgetItem *item );
  private:
    sicnu::data::AssetId assetForItem( QTreeWidgetItem *item ) const;
    std::optional<sicnu::data::CollectionId> collectionForItem( QTreeWidgetItem *item ) const;
    int referenceCount( sicnu::data::AssetId id ) const;
    bool isProjectPersistent( sicnu::data::AssetId id ) const;
    bool isPromotable( sicnu::data::AssetId id ) const;
    bool isRelocatable( sicnu::data::AssetId id ) const;

    void addAssetRow( QTreeWidgetItem *parent, const sicnu::data::AssetSnapshot &snapshot );
    /// Workbench 8.0: renders one catalog index entry as a tree row (the
    /// light path used by refresh; full snapshots stay a lazy query).
    void addIndexRow( QTreeWidgetItem *parent, const sicnu::AssetCatalogEntry &entry );
    /// Workbench 8.0: truthful truncation row (non-selectable, totals named).
    void addSentinelRow( QTreeWidgetItem *parent, const QString &text );
    /// Workbench 8.0: populates one collection row's children from the index
    /// (filter-aware, capped, called by refresh when small or on expand).
    void populateCollectionChildren( QTreeWidgetItem *collectionItem,
                                     const sicnu::data::CollectionSnapshot &collection );
    void showAssetDetails( const sicnu::data::AssetSnapshot &snapshot );
    void showCollectionDetails( const sicnu::data::CollectionSnapshot &collection );
    void showMultiSelectionDetails( const QList<sicnu::data::AssetId> &ids );
    void clearDetails( const QString &message = QString() );
    void applyHelpTips();
    /// Workbench 8.0: lazy bounded preview for local raster/vector assets.
    void requestDetailPreview( const sicnu::data::AssetSnapshot &snapshot );

    sicnu::data::DataManager *m_dataManager = nullptr; // not owned
    QTimer *m_refreshCoalesceTimer = nullptr; // not owned (child of this)
    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_treeStack = nullptr;
    RsEmptyStateWidget *m_emptyState = nullptr;
    QTextBrowser *m_detailView = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_previewLabel = nullptr; // bounded async preview (may stay hidden)
    sicnu::app::AssetPreviewService *m_previewService = nullptr; // owned (child)
    QString m_previewSource; // source of the pane's current/last preview request
    QSplitter *m_splitter = nullptr;
    // Workbench 8.0: large-metadata support (filter + light catalog index).
    QLineEdit *m_filterEdit = nullptr;
    QTimer *m_filterDebounce = nullptr; // filter coalescing (child of this)
    sicnu::AssetCatalogIndex m_catalogIndex;
    bool m_indexBuilt = false; ///< first refresh builds the index once
    int m_standaloneRowCap = kDefaultStandaloneRowCap;
};

} // namespace sicnu
