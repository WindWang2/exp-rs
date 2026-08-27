// src/data/execution_fingerprint.cpp
#include "execution_fingerprint.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <algorithm>
#include <cmath>
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

QByteArray canonicalizeJsonValue( const QJsonValue &val )
{
  if ( val.isNull() )
    return "null";
  if ( val.isBool() )
    return val.toBool() ? "true" : "false";
  if ( val.isDouble() )
  {
    const double d = val.toDouble();
    if ( d == 0.0 )
      return std::signbit( d ) ? "-0" : "0"; // ES6 distinguishes -0 from 0
    // Integer-valued doubles within the qint64 range serialize exactly. The
    // range guard also avoids UB on the static_cast for |d| >= 2^63.
    if ( std::fabs( d ) < 9.2233720368547758e18 && d == static_cast<qint64>( d ) )
      return QByteArray::number( static_cast<qint64>( d ) );
    // Shortest round-trip (ES6-style): a fixed 16-digit format provably
    // collides - 0.3 and 0.30000000000000004 both print "0.3" at 'g',16,
    // which would make distinct parameters hash to the same fingerprint and
    // silently reuse a wrong cached result. Emit the fewest significant
    // digits whose parse reproduces the exact double (doubles need up to 17).
    for ( int precision = 1; precision <= 17; ++precision )
    {
      const QByteArray candidate = QByteArray::number( d, 'g', precision );
      bool ok = false;
      if ( candidate.toDouble( &ok ) == d && ok )
        return candidate;
    }
    return QByteArray::number( d, 'g', 17 );
  }
  if ( val.isString() )
  {
    QJsonArray wrap = { val.toString() };
    QByteArray s = QJsonDocument( wrap ).toJson( QJsonDocument::Compact );
    if ( s.startsWith( '[' ) && s.endsWith( ']' ) )
      return s.mid( 1, s.size() - 2 );
    return s;
  }
  if ( val.isArray() )
  {
    const QJsonArray arr = val.toArray();
    QByteArray out = "[";
    for ( int i = 0; i < arr.size(); ++i )
    {
      if ( i > 0 ) out.append( ',' );
      out.append( canonicalizeJsonValue( arr[i] ) );
    }
    out.append( ']' );
    return out;
  }
  if ( val.isObject() )
  {
    const QJsonObject obj = val.toObject();
    QStringList keys = obj.keys();
    keys.sort();
    QByteArray out = "{";
    for ( int i = 0; i < keys.size(); ++i )
    {
      if ( i > 0 ) out.append( ',' );
      const QString &k = keys[i];
      QJsonArray wrap = { k };
      QByteArray keyStr = QJsonDocument( wrap ).toJson( QJsonDocument::Compact );
      if ( keyStr.startsWith( '[' ) && keyStr.endsWith( ']' ) )
        keyStr = keyStr.mid( 1, keyStr.size() - 2 );
      out.append( keyStr );
      out.append( ':' );
      out.append( canonicalizeJsonValue( obj.value( k ) ) );
    }
    out.append( '}' );
    return out;
  }
  return QByteArray();
}

} // namespace

QByteArray canonicalizeJsonRfc8785( const QJsonObject &obj )
{
  return canonicalizeJsonValue( obj );
}

ExecutionFingerprint makeExecutionFingerprint( const QString &algorithmId,
                                               const QString &algorithmVersion,
                                               const QJsonObject &parameters,
                                               const QVector<DerivationInput> &inputs )
{
  return ExecutionFingerprint{ hashCanonical( canonicalForm( algorithmId, algorithmVersion,
                                                             parameters, inputs ) ) };
}

ExecutionFingerprint makeExecutionFingerprintV2( const QString &algorithmId,
                                                 const QString &algorithmVersion,
                                                 const QJsonObject &parameters,
                                                 const QVector<TaggedDerivationInput> &inputs )
{
  QByteArray out;
  out.append( "v=2\n" );
  out.append( "alg=" );
  out.append( algorithmId.toUtf8() );
  out.append( "\nver=" );
  out.append( algorithmVersion.toUtf8() );
  out.append( "\nparams=" );
  out.append( canonicalizeJsonRfc8785( parameters ) );

  QVector<TaggedDerivationInput> sortedInputs = inputs;
  std::stable_sort( sortedInputs.begin(), sortedInputs.end(), []( const TaggedDerivationInput &a, const TaggedDerivationInput &b ) {
    if ( a.toPort != b.toPort ) return a.toPort < b.toPort;
    if ( a.fromPort != b.fromPort ) return a.fromPort < b.fromPort;
    if ( a.assetId != b.assetId ) return a.assetId.toString() < b.assetId.toString();
    if ( a.revision.value() != b.revision.value() )
      return a.revision.value() < b.revision.value();
    // The four fields above do not uniquely identify an input: two inputs may
    // share (toPort, fromPort, assetId, revision) yet differ in bands, value
    // domain, or content digest. Order over ALL serialized fields so the
    // concatenated form is independent of the caller's list order.
    const auto joinedBands = []( const QStringList &bands ) {
      QStringList sorted = bands;
      sorted.sort();
      return sorted.join( u',' );
    };
    const QString aBands = joinedBands( a.bandReferences );
    const QString bBands = joinedBands( b.bandReferences );
    if ( aBands != bBands ) return aBands < bBands;
    if ( a.valueDomain != b.valueDomain ) return a.valueDomain < b.valueDomain;
    if ( a.lazyContentDigest != b.lazyContentDigest ) return a.lazyContentDigest < b.lazyContentDigest;
    return false;
  } );

  for ( const auto &in : sortedInputs )
  {
    out.append( "\nin=" );
    out.append( in.assetId.toString().toUtf8() );
    out.append( "@rev=" );
    out.append( QByteArray::number( static_cast<qint64>( in.revision.value() ) ) );
    if ( !in.fromPort.isEmpty() )
    {
      out.append( ";from=" );
      out.append( in.fromPort.toUtf8() );
    }
    if ( !in.toPort.isEmpty() )
    {
      out.append( ";to=" );
      out.append( in.toPort.toUtf8() );
    }
    if ( !in.bandReferences.isEmpty() )
    {
      out.append( ";bands=" );
      QStringList bands = in.bandReferences;
      bands.sort();
      out.append( bands.join( ',' ).toUtf8() );
    }
    if ( !in.valueDomain.isEmpty() )
    {
      out.append( ";vd=" );
      out.append( in.valueDomain.toUtf8() );
    }
    if ( !in.lazyContentDigest.isEmpty() )
    {
      out.append( ";digest=" );
      out.append( in.lazyContentDigest.toUtf8() );
    }
  }

  return ExecutionFingerprint{ QCryptographicHash::hash( out, QCryptographicHash::Sha256 ) };
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
  // Insert first, then evict: evicting before the insert leaves the cache
  // permanently at maxEntries + 1 once full (the loop never runs at exactly
  // capacity). The fresh entry always carries the largest lastUsedTick, so the
  // LRU scan cannot evict it.
  m_entries.insert( fp.digest, Entry{ outputAssetId, ++m_clock } );
  evictIfNeededLocked();
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
