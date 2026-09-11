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
  entry.parentCollectionId = snapshot.parentCollectionId();
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
  m_entries.remove( row );
  m_byId.remove( id.toString() );
  // Reindex the tail (order-preserving removal): bounded by the removed
  // position's tail length; catalog sizes make a swap-and-pop unnecessary.
  for ( int i = row; i < m_entries.size(); ++i )
    m_byId[m_entries[i].id.toString()] = i;
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
  const QString needle = substring.trimmed();
  for ( int i = 0; i < m_entries.size(); ++i )
  {
    const AssetCatalogEntry &entry = m_entries[i];
    if ( entry.displayName.contains( needle, Qt::CaseInsensitive )
         || entry.source.contains( needle, Qt::CaseInsensitive )
         || entry.id.toString().contains( needle, Qt::CaseInsensitive ) )
      out.append( i );
  }
  return out;
}

void AssetCatalogIndex::groupIndices(
  const QVector<int> &indices,
  QHash<QString, QVector<int>> &byCollection,
  QVector<int> &standalone ) const
{
  byCollection.clear();
  standalone.clear();
  for ( const int i : indices )
  {
    const std::optional<sicnu::data::CollectionId> parent =
      m_entries[i].parentCollectionId;
    if ( parent.has_value() )
      byCollection[parent->toString()].append( i );
    else
      standalone.append( i );
  }
}

int AssetCatalogIndex::indexOfAsset( const sicnu::data::AssetId &id ) const
{
  const auto it = m_byId.constFind( id.toString() );
  return it != m_byId.constEnd() ? it.value() : -1;
}

} // namespace sicnu
