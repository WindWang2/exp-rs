#pragma once

#include <optional>

#include <QDockWidget>
#include <QString>

#include "data/asset_types.h"
#include "data/collection_types.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;
class QSplitter;
class QLabel;

namespace sicnu::data
{
class DataManager;
class AssetSnapshot;
}

namespace sicnu
{

/**
 * Project Data Manager panel — catalog projection + metadata inspector.
 *
 * Left: tree of Data Assets and Collections (not Display Layers).
 * Right: dedicated metadata panel for the selection (structure, source, product
 * fields). Shell wires display / unload / promote signals.
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

    sicnu::data::AssetId selectedAssetId() const;

    /// HTML currently shown in the metadata inspector (for tests).
    QString detailHtml() const;

    void selectAsset( sicnu::data::AssetId id );

    void activateAsset( sicnu::data::AssetId id );

    void requestRemove( sicnu::data::AssetId id );

    void requestPromote( sicnu::data::AssetId id );

    void refresh();

  signals:
    void displayRequested( sicnu::data::AssetId id );
    void unloadRequested( sicnu::data::AssetId id );
    void promoteRequested( sicnu::data::AssetId id );

  private slots:
    void onItemActivated( QTreeWidgetItem *item, int column );
    void onContextMenu( const QPoint &pos );
    void onSelectionChanged();

  private:
    sicnu::data::AssetId assetForItem( QTreeWidgetItem *item ) const;
    std::optional<sicnu::data::CollectionId> collectionForItem( QTreeWidgetItem *item ) const;
    int referenceCount( sicnu::data::AssetId id ) const;
    bool isProjectPersistent( sicnu::data::AssetId id ) const;
    bool isPromotable( sicnu::data::AssetId id ) const;

    void addAssetRow( QTreeWidgetItem *parent, const sicnu::data::AssetSnapshot &snapshot );
    void showAssetDetails( const sicnu::data::AssetSnapshot &snapshot );
    void showCollectionDetails( const sicnu::data::CollectionSnapshot &collection );
    void clearDetails( const QString &message = QString() );

    sicnu::data::DataManager *m_dataManager = nullptr; // not owned
    QTreeWidget *m_tree = nullptr;
    QTextBrowser *m_detailView = nullptr;
    QLabel *m_detailTitle = nullptr;
    QSplitter *m_splitter = nullptr;
};

} // namespace sicnu
