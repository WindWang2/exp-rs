/***************************************************************************
 * asset_catalog_index.cpp — light catalog index implementation
 ***************************************************************************/
#include "asset_catalog_index.h"

#include <QHash>

#include "data/data_asset.h"
#include "data/data_manager.h"

namespace sicnu
{

void AssetCatalogIndex::rebuild( sicnu::data::DataManager *dataManager )
{
  clear();
  if ( !dataManager )
    return;
  const QVector<sicnu::data::AssetSnapshot> snapshots = dataManager->assets();
  m_entries.reserve( snapshots.size() );
  for ( const sicnu::data::AssetSnapshot &snapshot : snapshots )
    addOrUpdateAsset( snapshot );
}

void AssetCatalogIndex::addOrUpdateEntry( const AssetCatalogEntry &entry )
{
  const auto it = m_byId.constFind( entry.id.toString() );
  if ( it != m_byId.constEnd() )
  {
    m_entries[it.value()] = entry;
    return;
  }
  m_byId[entry.id.toString()] = m_entries.size();
  m_entries.append( entry );
}

void AssetCatalogIndex::addOrUpdateAsset( const sicnu::data::AssetSnapshot &snapshot )
{
  AssetCatalogEntry entry;
  entry.id = snapshot.id();
  entry.displayName = snapshot.displayName();
  entry.source = snapshot.source().canonicalSource;
  entry.kind = snapshot.kind();
  entry.state = snapshot.state();
  entry.persistence = snapshot.persistence();
  addOrUpdateEntry( entry );
}

void AssetCatalogIndex::addOrUpdateAsset( const sicnu::data::AssetId &id,
                                          sicnu::data::DataManager *dataManager )
{
  if ( !dataManager )
    return;
  const std::optional<sicnu::data::AssetSnapshot> snapshot = dataManager->asset( id );
  if ( snapshot.has_value() )
    addOrUpdateAsset( *snapshot );
}

void AssetCatalogIndex::removeAsset( const sicnu::data::AssetId &id )
{
  const auto it = m_byId.constFind( id.toString() );
  if ( it == m_byId.constEnd() )
    return; // unknown id: nothing to do (idempotent)
  const int row = it.value();
  m_byId.remove( id.toString() );
  if ( row != m_entries.size() - 1 )
  {
    // Swap-and-pop: rows are unordered for every consumer (rendering uses
    // per-entry lookups; order within one filter pass is the catalog's
    // residual order) — O(1) removal instead of O(tail) per signal, which
    // would make batch unloads O(N²) on the GUI thread (review B-5).
    m_entries[row] = m_entries.last();
    m_byId[m_entries[row].id.toString()] = row;
  }
  m_entries.removeLast();
}

void AssetCatalogIndex::clear()
{
  m_entries.clear();
  m_byId.clear();
}

QVector<int> AssetCatalogIndex::filterIndices( const QString &substring ) const
{
  QVector<int> out;
  if ( substring.isEmpty() )
  {
    out.reserve( m_entries.size() );
    for ( int i = 0; i < m_entries.size(); ++i )
      out.append( i );
    return out;
  }
  for ( int i = 0; i < m_entries.size(); ++i )
  {
    if ( matchesFilter( m_entries[i], substring ) )
      out.append( i );
  }
  return out;
}

bool AssetCatalogIndex::matchesFilter( const AssetCatalogEntry &entry,
                                       const QString &substring )
{
  if ( substring.isEmpty() )
    return true;
  const QString needle = substring.trimmed();
  return entry.displayName.contains( needle, Qt::CaseInsensitive )
         || entry.source.contains( needle, Qt::CaseInsensitive )
         || entry.id.toString().contains( needle, Qt::CaseInsensitive );
}

int AssetCatalogIndex::indexOfAsset( const sicnu::data::AssetId &id ) const
{
  const auto it = m_byId.constFind( id.toString() );
  return it != m_byId.constEnd() ? it.value() : -1;
}

} // namespace sicnu
