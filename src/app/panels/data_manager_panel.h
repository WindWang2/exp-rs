#pragma once

#include <QDockWidget>
#include <QString>

#include "data/asset_types.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace sicnu::data
{
class DataManager;
class AssetSnapshot;
}

namespace sicnu
{

/**
 * Project Data Manager panel — a read-only projection of the Data Manager's
 * immutable asset snapshots and Data Collections, kept semantically separate
 * from the layer tree.
 *
 * Standalone Data Assets appear as top-level rows; a Data Collection appears
 * as a top-level parent row with its child assets nested beneath it. One row
 * per Data Asset (never per Display Layer); collection rows are organizational
 * and carry no AssetId. The panel holds no business logic: double-click,
 * remove, and promote intents are emitted as signals for the UI shell to wire
 * to the Display Manager, unload planning, and promotion respectively.
 */
class DataManagerPanel : public QDockWidget
{
    Q_OBJECT

  public:
    explicit DataManagerPanel( sicnu::data::DataManager *dataManager,
                               QWidget *parent = nullptr );
    ~DataManagerPanel() override = default;

    /// Number of top-level rows (one per standalone Data Asset + one per
    /// Data Collection; collection children are nested, not top-level).
    int rowCount() const;

    /// Text of a column for the row presenting `id`, or empty if absent.
    /// Columns: 0 name, 1 kind, 2 status, 3 persistence, 4 references.
    /// Finds the asset row whether it is top-level (standalone) or nested
    /// under a collection.
    QString rowText( sicnu::data::AssetId id, int column ) const;

    /// The Asset ID of the currently selected row, if any. A collection
    /// parent row carries no AssetId and returns a null id.
    sicnu::data::AssetId selectedAssetId() const;

    /// Selects the row presenting `id` without triggering display/unload intents.
    void selectAsset( sicnu::data::AssetId id );

    /// Triggers the activation (double-click) intent for the row presenting `id`.
    void activateAsset( sicnu::data::AssetId id );

    /// Triggers the remove intent for the row presenting `id` (context action).
    void requestRemove( sicnu::data::AssetId id );

    /// Triggers the promote intent for a temporary asset (context action). The
    /// shell calls DataManager::promote; the panel refreshes via assetChanged.
    void requestPromote( sicnu::data::AssetId id );

    /// Re-projects the Data Manager snapshots. Called automatically on asset
    /// add/change/remove and collection add/remove; call it after Display Layer
    /// (lease) changes, which do not emit asset signals.
    void refresh();

  signals:
    /// Double-click / activation intent: the UI shell should add a Display Layer.
    void displayRequested( sicnu::data::AssetId id );
    /// Remove intent: the UI shell should run unload planning (with confirmation).
    void unloadRequested( sicnu::data::AssetId id );
    /// Promote intent: the UI shell should call DataManager::promote so a
    /// temporary asset survives the session and is saved into the .qgz.
    void promoteRequested( sicnu::data::AssetId id );

  private slots:
    void onItemActivated( QTreeWidgetItem *item, int column );
    void onContextMenu( const QPoint &pos );

  private:
    sicnu::data::AssetId assetForItem( QTreeWidgetItem *item ) const;
    int referenceCount( sicnu::data::AssetId id ) const;
    /// True when `id` is a registered ProjectPersistent asset. Shared by the
    /// promote request guard and the context-menu enable/disable gate.
    bool isProjectPersistent( sicnu::data::AssetId id ) const;
    /// True when `id` is a registered temporary asset eligible for promotion.
    bool isPromotable( sicnu::data::AssetId id ) const;

    /// Populates one asset row (top-level or a collection child) from `snapshot`.
    void addAssetRow( QTreeWidgetItem *parent, const sicnu::data::AssetSnapshot &snapshot );

    sicnu::data::DataManager *m_dataManager = nullptr; // not owned
    QTreeWidget *m_tree = nullptr;
};

} // namespace sicnu
