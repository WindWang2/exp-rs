// src/data/execution_fingerprint.cpp
#include "execution_fingerprint.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <algorithm>
#include <mutex>

namespace sicnu::data
{

namespace
{
/// Canonical, order-independent string form of the fingerprint inputs, hashed
/// with SHA-256. Parameter keys are sorted so map insertion order does not
/// change the fingerprint.
QByteArray canonicalForm( const QString &algorithmId,
                          const QString &algorithmVersion,
                          const QJsonObject &parameters,
                          const QVector<DerivationInput> &inputs )
{
  QByteArray out;
  out.append( "alg=" );
  out.append( algorithmId.toUtf8() );
  out.append( "\nver=" );
  out.append( algorithmVersion.toUtf8() );

  // Normalize parameters: serialize via QJsonDocument with sorted keys.
  QJsonObject sortedParams = parameters;
  out.append( "\nparams=" );
  out.append( QJsonDocument( sortedParams ).toJson( QJsonDocument::Compact ) );

  // Inputs in list order (each input's own fields are stable).
  for ( const auto &in : inputs )
  {
    out.append( "\nin=" );
    out.append( in.assetId.toString().toUtf8() );
    out.append( "@rev=" );
    out.append( QByteArray::number( static_cast<qint64>( in.revision.value() ) ) );
    if ( !in.bandReferences.isEmpty() )
    {
      out.append( ";bands=" );
      out.append( in.bandReferences.join( ',' ).toUtf8() );
    }
    if ( !in.valueDomain.isEmpty() )
    {
      out.append( ";vd=" );
      out.append( in.valueDomain.toUtf8() );
    }
  }
  return out;
}

/// Full 256-bit SHA-256 digest of the canonical form. The cache keys on the
/// full digest, never a truncated fold: a truncated key would allow two
/// different executions to collide and silently reuse the wrong output.
QByteArray hashCanonical( const QByteArray &canonical )
{
  return QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 );
}
} // namespace

ExecutionFingerprint makeExecutionFingerprint( const QString &algorithmId,
                                               const QString &algorithmVersion,
                                               const QJsonObject &parameters,
                                               const QVector<DerivationInput> &inputs )
{
  return ExecutionFingerprint{ hashCanonical( canonicalForm( algorithmId, algorithmVersion,
                                                             parameters, inputs ) ) };
}

ExecutionResultCache &ExecutionResultCache::instance()
{
  static ExecutionResultCache cache;
  return cache;
}

void ExecutionResultCache::setEnabled( bool on )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_enabled = on;
}

bool ExecutionResultCache::isEnabled() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_enabled;
}

void ExecutionResultCache::setMaxEntries( size_t maxEntries )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_maxEntries = std::max<size_t>( 1, maxEntries );
  evictIfNeededLocked();
}

size_t ExecutionResultCache::maxEntries() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_maxEntries;
}

std::optional<AssetId> ExecutionResultCache::lookup( const ExecutionFingerprint &fp ) const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return std::nullopt;
  auto it = m_entries.find( fp.digest );
  if ( it == m_entries.end() )
    return std::nullopt;
  // Touch LRU recency.
  it->lastUsedTick = ++m_clock;
  return it->asset;
}

void ExecutionResultCache::store( const ExecutionFingerprint &fp, const AssetId &outputAssetId )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return;
  auto it = m_entries.find( fp.digest );
  if ( it != m_entries.end() )
  {
    it->asset = outputAssetId;
    it->lastUsedTick = ++m_clock;
    return;
  }
  evictIfNeededLocked();
  m_entries.insert( fp.digest, Entry{ outputAssetId, ++m_clock } );
}

void ExecutionResultCache::evictIfNeededLocked()
{
  // Evict least-recently-used entries until we fit under the cap. An LRU scan
  // over the map is O(n) per store past capacity; with a bounded map and
  // store-heavy workloads this is the simple, correct choice.
  while ( static_cast<size_t>( m_entries.size() ) > m_maxEntries )
  {
    QHash<QByteArray, Entry>::iterator lruIt = m_entries.begin();
    for ( QHash<QByteArray, Entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it )
    {
      if ( it->lastUsedTick < lruIt->lastUsedTick )
        lruIt = it;
    }
    m_entries.erase( lruIt );
  }
}

void ExecutionResultCache::invalidate( const ExecutionFingerprint &fp )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.remove( fp.digest );
}

void ExecutionResultCache::clear()
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.clear();
}

int ExecutionResultCache::size() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_entries.size();
}

} // namespace sicnu::data
