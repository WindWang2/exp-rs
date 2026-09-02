// src/data/execution_fingerprint.cpp
#include "execution_fingerprint.h"

#include <QFile>

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
/// Plain JSON string literal (quotes + standard escaping) — the RFC 8785
/// canonical-JSON form used for every string inside canonicalizeJsonValue.
/// (The #639 hex hardening lives in framingLiteral() below; hex has no place
/// inside the canonical JSON itself.)
QByteArray jsonEscaped( const QString &s )
{
  QJsonArray wrap = { s };
  QByteArray out = QJsonDocument( wrap ).toJson( QJsonDocument::Compact );
  if ( out.startsWith( '[' ) && out.endsWith( ']' ) )
    out = out.mid( 1, out.size() - 2 );
  return out;
}

/// Framing form for the V1/V2 canonical layouts (#639): JSON string literal,
/// then hex-encoded. Hex is injective and contains none of the framing
/// delimiters (newline, ';', '=', '@'), so a caller-controlled string cannot
/// inject or shift the field layout — two different (id, version) tuples can
/// never collide into the same canonical bytes. (Plain JSON escaping left
/// ';'/@/'=' intact inside the JSON body, so sub-field injection like
/// fromPort="output;to=x" still collided with two-field inputs, #639.)
/// canonicalizeJsonRfc8785 must NOT use this: RFC 8785 output is readable
/// canonical JSON, and hex-encoding every string silently changed the params
/// serialization (#639 follow-up).
QByteArray hexFramed( const QByteArray &jsonLiteral )
{
  static const char hex[] = "0123456789abcdef";
  QByteArray encoded;
  encoded.reserve( jsonLiteral.size() * 2 );
  for ( unsigned char c : jsonLiteral )
  {
    encoded.append( hex[c >> 4] );
    encoded.append( hex[c & 0xF] );
  }
  return encoded;
}

QByteArray framingLiteral( const QString &s )
{
  return hexFramed( jsonEscaped( s ) );
}

/// Canonical, order-independent string form of the fingerprint inputs, hashed
/// with SHA-256. Parameter keys are sorted so map insertion order does not
/// change the fingerprint. Every caller-controlled string is JSON-escaped so
/// the '\n'-delimited framing is not injectable.
QByteArray canonicalForm( const QString &algorithmId,
                          const QString &algorithmVersion,
                          const QJsonObject &parameters,
                          const QVector<DerivationInput> &inputs )
{
  QByteArray out;
  out.append( "alg=" );
  out.append( framingLiteral( algorithmId ) );
  out.append( "\nver=" );
  out.append( framingLiteral( algorithmVersion ) );

  // Normalize parameters: serialize via QJsonDocument with sorted keys.
  QJsonObject sortedParams = parameters;
  out.append( "\nparams=" );
  out.append( QJsonDocument( sortedParams ).toJson( QJsonDocument::Compact ) );

  // Inputs in list order (each input's own fields are stable).
  for ( const auto &in : inputs )
  {
    out.append( "\nin=" );
    out.append( framingLiteral( in.assetId.toString() ) );
    out.append( "@rev=" );
    out.append( QByteArray::number( static_cast<qint64>( in.revision.value() ) ) );
    if ( !in.bandReferences.isEmpty() )
    {
      out.append( ";bands=" );
      out.append( framingLiteral( in.bandReferences.join( ',' ) ) );
    }
    if ( !in.valueDomain.isEmpty() )
    {
      out.append( ";vd=" );
      out.append( framingLiteral( in.valueDomain ) );
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
      return "0"; // RFC 8785 §3.2.2.3: ES6 Number::toString does not
                  // distinguish -0, so both zero signs serialize as "0" and
                  // hash identically instead of splitting the cache.
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
    return jsonEscaped( val.toString() );
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
      out.append( jsonEscaped( k ) );
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
  // Every caller-controlled string goes through jsonEscaped() — the same
  // guarantee as the V1 canonical form: an id/port/band value containing the
  // framing delimiters (newline, ';', '=', '@') cannot inject or shift the
  // field layout, so distinct (algorithmId, version) tuples can never collide into
  // the same canonical bytes (#639).
  out.append( "v=2\n" );
  out.append( "alg=" );
  out.append( framingLiteral( algorithmId ) );
  out.append( "\nver=" );
  out.append( framingLiteral( algorithmVersion ) );
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
    out.append( framingLiteral( in.assetId.toString() ) );
    out.append( "@rev=" );
    out.append( QByteArray::number( static_cast<qint64>( in.revision.value() ) ) );
    if ( !in.fromPort.isEmpty() )
    {
      out.append( ";from=" );
      out.append( framingLiteral( in.fromPort ) );
    }
    if ( !in.toPort.isEmpty() )
    {
      out.append( ";to=" );
      out.append( framingLiteral( in.toPort ) );
    }
    if ( !in.bandReferences.isEmpty() )
    {
      out.append( ";bands=" );
      QStringList bands = in.bandReferences;
      bands.sort();
      out.append( framingLiteral( bands.join( ',' ) ) );
    }
    if ( !in.valueDomain.isEmpty() )
    {
      out.append( ";vd=" );
      out.append( framingLiteral( in.valueDomain ) );
    }
    if ( !in.lazyContentDigest.isEmpty() )
    {
      out.append( ";digest=" );
      out.append( framingLiteral( in.lazyContentDigest ) );
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
  while ( static_cast<size_t>( m_pathEntries.size() ) > m_maxEntries )
  {
    QHash<QByteArray, PathEntry>::iterator lruIt = m_pathEntries.begin();
    for ( QHash<QByteArray, PathEntry>::iterator it = m_pathEntries.begin(); it != m_pathEntries.end(); ++it )
    {
      if ( it->lastUsedTick < lruIt->lastUsedTick )
        lruIt = it;
    }
    m_pathEntries.erase( lruIt );
  }
}

void ExecutionResultCache::invalidate( const ExecutionFingerprint &fp )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.remove( fp.digest );
  m_pathEntries.remove( fp.digest );
}

std::optional<QString> ExecutionResultCache::lookupOutputPath( const ExecutionFingerprint &fp )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return std::nullopt;
  auto it = m_pathEntries.find( fp.digest );
  if ( it == m_pathEntries.end() )
    return std::nullopt;
  if ( !QFile::exists( it->path ) )
  {
    // The cached artifact vanished (GC, manual cleanup): a stale entry must
    // never be served, so drop it and report a miss.
    m_pathEntries.erase( it );
    return std::nullopt;
  }
  it->lastUsedTick = ++m_clock;
  return it->path;
}

void ExecutionResultCache::storeOutputPath( const ExecutionFingerprint &fp, const QString &outputPath )
{
  if ( outputPath.isEmpty() )
    return;
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return;
  auto it = m_pathEntries.find( fp.digest );
  if ( it != m_pathEntries.end() )
  {
    it->path = outputPath;
    it->lastUsedTick = ++m_clock;
    return;
  }
  m_pathEntries.insert( fp.digest, PathEntry{ outputPath, ++m_clock } );
  evictIfNeededLocked();
}

int ExecutionResultCache::pathSize() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_pathEntries.size();
}

QStringList ExecutionResultCache::cachedOutputPaths() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  QStringList paths;
  paths.reserve( m_pathEntries.size() );
  for ( auto it = m_pathEntries.begin(); it != m_pathEntries.end(); ++it )
    paths.append( it->path );
  return paths;
}

void ExecutionResultCache::clear()
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.clear();
  // A "cache reset" must also drop the output-path store (#720): invalidate()
  // removes from both, and a clear() that skips m_pathEntries lets
  // lookupOutputPath() serve pre-clear paths while size()/pathSize() disagree.
  m_pathEntries.clear();
}

int ExecutionResultCache::size() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_entries.size();
}

} // namespace sicnu::data
