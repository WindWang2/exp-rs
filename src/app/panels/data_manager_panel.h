#pragma once

#include <QDockWidget>
#include <QString>

#include "data/asset_types.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu
{

/**
 * Project Data Manager panel — a read-only projection of the Data Manager's
 * immutable asset snapshots, kept semantically separate from the layer tree.
 *
 * One row per Data Asset (never per Display Layer). The panel holds no business
 * logic: double-click and remove intents are emitted as signals for the UI
 * shell to wire to the Display Manager and to unload planning respectively.
 */
class DataManagerPanel : public QDockWidget
{
    Q_OBJECT

  public:
    explicit DataManagerPanel( sicnu::data::DataManager *dataManager,
                               QWidget *parent = nullptr );
    ~DataManagerPanel() override = default;

    /// Number of asset rows currently shown (one per Data Asset).
    int rowCount() const;

    /// Text of a column for the row presenting `id`, or empty if absent.
    /// Columns: 0 name, 1 kind, 2 status, 3 persistence, 4 references.
    QString rowText( sicnu::data::AssetId id, int column ) const;

    /// The Asset ID of the currently selected row, if any.
    sicnu::data::AssetId selectedAssetId() const;

    /// Selects the row presenting `id` without triggering display/unload intents.
    void selectAsset( sicnu::data::AssetId id );

    /// Triggers the activation (double-click) intent for the row presenting `id`.
    void activateAsset( sicnu::data::AssetId id );

    /// Triggers the remove intent for the row presenting `id` (context action).
    void requestRemove( sicnu::data::AssetId id );

    /// Re-projects the Data Manager snapshots. Called automatically on asset
    /// add/change/remove; call it after Display Layer (lease) changes, which do
    /// not emit asset signals.
    void refresh();

  signals:
    /// Double-click / activation intent: the UI shell should add a Display Layer.
    void displayRequested( sicnu::data::AssetId id );
    /// Remove intent: the UI shell should run unload planning (with confirmation).
    void unloadRequested( sicnu::data::AssetId id );

  private slots:
    void onItemActivated( QTreeWidgetItem *item, int column );
    void onContextMenu( const QPoint &pos );

  private:
    sicnu::data::AssetId assetForItem( QTreeWidgetItem *item ) const;
    int referenceCount( sicnu::data::AssetId id ) const;

    sicnu::data::DataManager *m_dataManager = nullptr; // not owned
    QTreeWidget *m_tree = nullptr;
};

} // namespace sicnu
