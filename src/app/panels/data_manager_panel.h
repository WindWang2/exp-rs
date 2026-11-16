#pragma once

#include <optional>

#include <QDockWidget>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QString>
#include <QVector>

#include "data/asset_types.h"
#include "data/collection_types.h"
#include "asset_catalog_index.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;
class QSplitter;
QT_BEGIN_NAMESPACE
class QToolButton;
QT_END_NAMESPACE
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
    /// Workbench 9.0 M7: bounded pagination over the filtered standalone
    /// catalog. Page size is the standalone row cap; flipping a page re-slices
    /// the index and re-renders at most that many rows.
    int standalonePage() const { return m_standalonePage; }
    void setStandalonePage( int page );
    int standalonePageCount() const { return m_standalonePageCount; }
    static constexpr int kDefaultStandaloneRowCap = 20000;

    /// R31-T03 WP02: per-collection paging over the authoritative membership
    /// snapshot. Every collection keeps its own page index, so a refresh,
    /// rename, move or filter change never maps an old row index onto a new
    /// asset identity. Page state is keyed by CollectionId, not by tree row.
    /// Defaults to the pre-existing row cap, so current catalogs render
    /// exactly as before; hosts/tests lower it to exercise real paging.
    static constexpr int kDefaultCollectionPageSize = kDefaultStandaloneRowCap;
    void setCollectionPageSize( int pageSize );
    int collectionPageSize() const { return m_collectionPageSize; }
    int collectionPage( const sicnu::data::CollectionId &id ) const;
    int collectionPageCount( const sicnu::data::CollectionId &id ) const;
    void setCollectionPage( const sicnu::data::CollectionId &id, int page );
    /// Reveal a known asset even when it lives beyond the first page.
    /// Returns false when the id is not a member of any current collection.
    bool revealCollectionAsset( const sicnu::data::AssetId &id );
    /// Exact visible range/total/filter state of one collection's page.
    QString collectionPageLabel( const sicnu::data::CollectionId &id ) const;

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
  protected:
    void changeEvent( QEvent *event ) override;

  private:
    sicnu::data::AssetId assetForItem( QTreeWidgetItem *item ) const;
    std::optional<sicnu::data::CollectionId> collectionForItem( QTreeWidgetItem *item ) const;
    int referenceCount( sicnu::data::AssetId id ) const;
    bool isProjectPersistent( sicnu::data::AssetId id ) const;
    bool isPromotable( sicnu::data::AssetId id ) const;
    bool isRelocatable( sicnu::data::AssetId id ) const;

    /// Shared constructor of one asset tree row: creates the item under
    /// @p parent (or the tree root) and applies the cell layout shared by
    /// addAssetRow/addIndexRow — kind icon + name cell, persistence and
    /// reference-count cells, asset-id role, and the missing-source tooltip.
    /// @p kindLabel stays caller-supplied: snapshot rows refine the raster
    /// label with band count, index rows degrade to the plain kind word.
    void createRow( QTreeWidgetItem *parent,
                    const QString &displayName,
                    const QString &kindLabel,
                    sicnu::data::AssetKind kind,
                    sicnu::data::AssetState state,
                    const QString &source,
                    sicnu::data::PersistencePolicy persistence,
                    const sicnu::data::AssetId &id );
    void addAssetRow( QTreeWidgetItem *parent, const sicnu::data::AssetSnapshot &snapshot );
    /// Workbench 8.0: renders one catalog index entry as a tree row (the
    /// light path used by refresh; full snapshots stay a lazy query).
    void addIndexRow( QTreeWidgetItem *parent, const sicnu::AssetCatalogEntry &entry );
    /// Workbench 8.0: truthful truncation row (non-selectable, totals named).
    void addSentinelRow( QTreeWidgetItem *parent, const QString &text );
    /// R31-T03 WP02: one actionable paging row (keyboard + accessible name).
    void addCollectionPagerRow( QTreeWidgetItem *parent,
                                const sicnu::data::CollectionSnapshot &collection,
                                int page, int pageCount, int matched, int first,
                                int last, const QString &filter );
    /// R31-T03 WP02: identity of one matched collection child. `index` is
    /// the light-catalog row, or -1 when the index lags behind membership;
    /// the AssetId is always the authoritative identity, never a row number.
    struct CollectionChildRef
    {
      sicnu::data::AssetId id;
      int index = -1;
    };
    /// R31-T03 WP02: ONE filter rule shared by refresh() and the lazy
    /// populate path. A collection-name hit shows the whole collection;
    /// otherwise only children matching the shared light-entry rule. This
    /// keeps name hits and child hits consistent instead of rendering an
    /// empty collection on a name hit.
    QVector<CollectionChildRef> matchedCollectionChildren(
      const sicnu::data::CollectionSnapshot &collection, const QString &filter,
      bool *nameMatches = nullptr ) const;
    /// R31-T03 WP02: render one bounded page of a collection's children.
    void renderCollectionPage( QTreeWidgetItem *collectionItem,
                               const sicnu::data::CollectionSnapshot &collection,
                               const QVector<CollectionChildRef> &matched,
                               bool nameMatches, const QString &filter );
    /// Workbench 8.0: populates one collection row's children from the index
    /// (filter-aware, capped, called by refresh when small or on expand).
    void populateCollectionChildren( QTreeWidgetItem *collectionItem,
                                     const sicnu::data::CollectionSnapshot &collection );
    void showAssetDetails( const sicnu::data::AssetSnapshot &snapshot );
    void showCollectionDetails( const sicnu::data::CollectionSnapshot &collection );
    void showMultiSelectionDetails( const QList<sicnu::data::AssetId> &ids );
    /// Rebuild the inspector without changing the user's selection intent.
    void refreshDetails( const QList<sicnu::data::AssetId> &ids );
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
    int m_standalonePage = 0;
    QString m_lastSelectedAssetId;
    /// True while refresh() rebuilds the tree — selection changes fired by
    /// the rebuild itself must not overwrite the remembered identity.
    bool m_inRefresh = false;
    /// Hidden panels apply palette changes when their details become visible.
    bool m_paletteRefreshPending = false;
    int m_standalonePageCount = 0;
    /// R31-T03 WP02: page index + exact totals per CollectionId string.
    QHash<QString, int> m_collectionPages;
    QHash<QString, int> m_collectionPageCounts;
    QHash<QString, int> m_collectionMatchedTotals;
    QHash<QString, QString> m_collectionPageLabels;
    /// Filter text used for the last collection page render. Guards page-index
    /// reset in refresh() itself, so programmatic setText()+refresh() (no
    /// textChanged signal, e.g. project restore) cannot leave a stale page.
    QString m_lastCollectionFilter;
    int m_collectionPageSize = kDefaultCollectionPageSize;
    QWidget *m_pagerRow = nullptr;
    QToolButton *m_prevPageBtn = nullptr;
    QToolButton *m_nextPageBtn = nullptr;
    QLabel *m_pageLabel = nullptr;
};

} // namespace sicnu
