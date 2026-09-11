/***************************************************************************
 * asset_catalog_index.h — Professional Workbench 8.0 (package D)
 *
 * Light, incrementally-maintained index over the DataManager catalog for
 * large-metadata UI. The Data Manager panel used to re-fetch every full
 * AssetSnapshot on every refresh burst (O(all assets) per refresh, with
 * full per-asset snapshot copies); 100k-asset catalogs made each coalesced
 * rebuild expensive. The index keeps one lightweight entry per asset —
 * (id, display name, source, kind, state, persistence, parent collection) —
 * rebuilt once when attached and maintained from DataManager's per-asset
 * signals afterwards. Renderers consume the index (filter + cap) instead of
 * re-querying snapshots; heavy per-asset data stays an on-demand
 * DataManager::asset(id) query (lazy detail loading).
 *
 * This is NOT a second catalog authority: DataManager remains the only
 * owner of asset truth. The index is a disposable projection — a missed
 * signal is healed by rebuild().
 *
 * Pure value class (no QObject): fully unit-testable against a hermetic
 * DataManager fixture; the panel drives it.
 ***************************************************************************/
#pragma once

#include <QHash>
#include <QVector>
#include <optional>

#include "data/asset_types.h"
#include "data/collection_types.h"

class QString;

namespace sicnu::data
{
class DataManager;
class AssetSnapshot;
} // namespace sicnu::data

namespace sicnu
{

struct AssetCatalogEntry
{
    sicnu::data::AssetId id;
    QString displayName;
    QString source;
    sicnu::data::AssetKind kind = sicnu::data::AssetKind::Raster;
    sicnu::data::AssetState state = sicnu::data::AssetState::Ready;
    sicnu::data::PersistencePolicy persistence = sicnu::data::PersistencePolicy::ProjectPersistent;
    std::optional<sicnu::data::CollectionId> parentCollectionId;
};

class AssetCatalogIndex
{
  public:
    /// Drops everything and re-reads the catalog from @p dataManager
    /// (null manager → empty index).
    void rebuild( sicnu::data::DataManager *dataManager );

    /// Incremental maintenance (driven by DataManager signals).
    void addOrUpdateAsset( const sicnu::data::AssetSnapshot &snapshot );
    void addOrUpdateAsset( const sicnu::data::AssetId &id,
                           sicnu::data::DataManager *dataManager );
    /// Direct entry insert (hosts composing light entries without a full
    /// snapshot fetch; also the benchmark seam for logical-scale evidence).
    void addOrUpdateEntry( const AssetCatalogEntry &entry );
    void removeAsset( const sicnu::data::AssetId &id );
    void clear();

    const QVector<AssetCatalogEntry> &entries() const { return m_entries; }
    int totalAssets() const { return m_entries.size(); }

    /// Case-insensitive substring filter over display name, source and id
    /// text. Empty filter matches everything. Results keep catalog order.
    /// The return holds indices into entries() — no per-row copies.
    QVector<int> filterIndices( const QString &substring ) const;

    /// Single pass over @p indices splitting entries into per-collection
    /// buckets and the standalone set (both in catalog order) — the renderer
    /// builds its tree in O(rows) instead of O(collections × assets).
    /// Buckets are keyed by CollectionId::toString() (Qt6 has no qHash for
    /// the id types, and the data headers stay untouched).
    void groupIndices( const QVector<int> &indices,
                       QHash<QString, QVector<int>> &byCollection,
                       QVector<int> &standalone ) const;

    int indexOfAsset( const sicnu::data::AssetId &id ) const;

  private:
    QVector<AssetCatalogEntry> m_entries;
    QHash<QString, int> m_byId; // AssetId::toString() → row (no qHash on ids)
};

} // namespace sicnu
