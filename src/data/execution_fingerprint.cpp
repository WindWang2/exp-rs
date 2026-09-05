// src/data/execution_fingerprint.cpp
#include "execution_fingerprint.h"

#include "artifact_object_pool.h"

#include <QFile>
#include <QFileInfo>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
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
    if ( a.lazyContentDigest != b.lazyContentDigest )
      return a.lazyContentDigest < b.lazyContentDigest;
    if ( a.producerFingerprint != b.producerFingerprint )
      return a.producerFingerprint < b.producerFingerprint;
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
    if ( !in.producerFingerprint.isEmpty() )
    {
      // Chained in-pipeline producer identity (#726, contract v2): the
      // producer step's execution fingerprint is this input's identity, so a
      // change anywhere upstream re-keys every downstream step.
      out.append( ";pfp=" );
      out.append( framingLiteral( in.producerFingerprint ) );
    }
  }

  return ExecutionFingerprint{ QCryptographicHash::hash( out, QCryptographicHash::Sha256 ) };
}

ExecutionResultCache &ExecutionResultCache::instance()
{
  static ExecutionResultCache cache;
  // Production gate (#667): the cache is opt-in per process via
  // SICNU_EXECUTION_CACHE so default runs keep byte-identical execution
  // semantics; hosts that want incremental hits flip the env (or call
  // setEnabled directly, e.g. tests and an explicit CLI flag).
  static bool envApplied = false;
  if ( !envApplied )
  {
    envApplied = true;
    if ( !cache.isEnabled() && executionCacheEnabledFromEnv() )
      cache.setEnabled( true );
  }
  return cache;
}

bool executionCacheEnabledFromEnv()
{
  // Same flag vocabulary as envFlagEnabled (src/agent/env_flag.h), kept local
  // so the data layer does not depend on the agent module.
  const QByteArray v = qgetenv( "SICNU_EXECUTION_CACHE" );
  if ( v.isEmpty() )
    return false;
  const QString s = QString::fromUtf8( v ).trimmed().toLower();
  return s == QLatin1String( "1" ) || s == QLatin1String( "true" )
         || s == QLatin1String( "yes" ) || s == QLatin1String( "on" );
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
  while ( static_cast<size_t>( m_executions.size() ) > m_maxEntries )
  {
    QHash<QByteArray, ExecutionEntry>::iterator lruIt = m_executions.begin();
    for ( QHash<QByteArray, ExecutionEntry>::iterator it = m_executions.begin();
          it != m_executions.end(); ++it )
    {
      if ( it->lastUsedTick < lruIt->lastUsedTick )
        lruIt = it;
    }
    QStringList claims = lruIt->execution.producedArtifacts
                         << lruIt->execution.declaredOutputPath;
    for ( const QString &claimPath : claims )
    {
      const auto ownerIt = m_pathOwners.constFind( claimPath );
      if ( ownerIt != m_pathOwners.constEnd() && ownerIt.value() == lruIt.key() )
        m_pathOwners.remove( claimPath );
    }
    m_executions.erase( lruIt );
  }
}

void ExecutionResultCache::invalidate( const ExecutionFingerprint &fp )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.remove( fp.digest );
  const auto execIt = m_executions.find( fp.digest );
  if ( execIt != m_executions.end() )
  {
    QStringList claims = execIt->execution.producedArtifacts
                         << execIt->execution.declaredOutputPath;
    for ( const QString &claimPath : claims )
    {
      const auto ownerIt = m_pathOwners.constFind( claimPath );
      if ( ownerIt != m_pathOwners.constEnd() && ownerIt.value() == fp.digest )
        m_pathOwners.remove( claimPath );
    }
    m_executions.erase( execIt );
  }
}

namespace
{
bool declaredOutputStillValid( const ExecutionResultCache::CachedExecution &execution )
{
  if ( execution.declaredOutputPath.isEmpty() )
    return false;
  // Every stat'ed artifact must still exist with the recorded size and
  // mtime: a destination rewritten by a DIFFERENT execution (or reaped
  // externally) can never vouch for this fingerprint's result. Size+mtime is
  // the cheap proxy; per-path ownership eviction in storeExecution() closes
  // the realistic overwrite path (a fingerprinted execution publishing onto
  // the same path).
  for ( auto it = execution.artifactSizes.constBegin();
        it != execution.artifactSizes.constEnd(); ++it )
  {
    const QFileInfo info( it.key() );
    if ( !info.isFile() || info.size() != it.value() )
      return false;
    const auto msecs = execution.artifactMsecs.constFind( it.key() );
    if ( msecs != execution.artifactMsecs.constEnd()
         && info.lastModified().toMSecsSinceEpoch() != msecs.value() )
      return false;
  }
  // The chained inputs the producing run consumed are part of the claim too:
  // an intermediate rewritten out-of-band invalidates every cached consumer
  // whose recorded input stats no longer match (fail-closed ⇒ miss).
  for ( auto it = execution.inputSizes.constBegin();
        it != execution.inputSizes.constEnd(); ++it )
  {
    const QFileInfo info( it.key() );
    if ( !info.isFile() || info.size() != it.value() )
      return false;
    const auto msecs = execution.inputMsecs.constFind( it.key() );
    if ( msecs != execution.inputMsecs.constEnd()
         && info.lastModified().toMSecsSinceEpoch() != msecs.value() )
      return false;
  }
  if ( execution.producedArtifacts.isEmpty() )
    return false;
  // The declared output is either a produced artifact (validated above) or a
  // grouping convention the producing run never wrote (grouped composites);
  // requiring its existence would invalidate every grouped entry.
  return true;
}
} // namespace

// Objects larger than this are not pooled (conservative cap).
namespace
{
constexpr qint64 kMaxPooledObjectBytes = 2LL << 30; // 2 GiB
} // namespace

ExecutionResultCache::~ExecutionResultCache() = default;

std::optional<ExecutionResultCache::CachedExecution>
ExecutionResultCache::lookupExecution( const ExecutionFingerprint &fp )
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return std::nullopt;
  auto it = m_executions.find( fp.digest );
  if ( it == m_executions.end() )
  {
    // Phase E: in-memory miss → persistent content-addressed tier. The pool
    // re-verifies every object digest (conservative default), so corruption
    // self-heals into a miss and a real execution re-runs.
    ensurePersistentTierLocked();
    if ( m_persistent )
    {
      const auto pooled = m_persistent->lookupExecution( fp.toHex() );
      if ( pooled )
      {
        CachedExecution reconstructed;
        reconstructed.declaredOutputPath = pooled->declaredOriginal;
        reconstructed.resultPayload = QJsonDocument::fromJson( pooled->payloadJson );
        reconstructed.inputSizes = pooled->inputSizes;
        reconstructed.inputMsecs = pooled->inputMsecs;
        for ( const sicnu::data::PoolObject &object : pooled->objects )
        {
          reconstructed.producedArtifacts.append( object.originalPath );
          // The serve copies FROM the pool object; expectations must describe
          // the pool object (the copy source), not the original path.
          reconstructed.artifactSizes.insert( object.poolPath, object.size );
          reconstructed.artifactMsecs.insert( object.poolPath, object.msecs );
          reconstructed.sourceOverrides.insert( object.originalPath, object.poolPath );
        }
        // Trust gate: in-memory entries validate the producing file's
        // size/mtime. Pool objects are immutable and digest-verified at this
        // lookup; original paths may legitimately be gone, so the size/mtime
        // check is deliberately replaced by the digest check above.
        return reconstructed;
      }
    }
    return std::nullopt;
  }
  if ( !declaredOutputStillValid( it->execution ) )
  {
    // The cached result vanished or was replaced (GC, external write, manual
    // cleanup): a stale entry must never be served, so drop it and report a
    // miss. A future identical execution re-stores a fresh result.
    QStringList claims = it->execution.producedArtifacts
                         << it->execution.declaredOutputPath;
    for ( const QString &claimPath : claims )
    {
      const auto ownerIt = m_pathOwners.constFind( claimPath );
      if ( ownerIt != m_pathOwners.constEnd() && ownerIt.value() == fp.digest )
        m_pathOwners.remove( claimPath );
    }
    m_executions.erase( it );
    return std::nullopt;
  }
  it->lastUsedTick = ++m_clock;
  return it->execution;
}

void ExecutionResultCache::storeExecution( const ExecutionFingerprint &fp,
                                           const CachedExecution &execution )
{
  if ( execution.declaredOutputPath.isEmpty() )
    return;
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  if ( !m_enabled )
    return;

  // Path ownership (cache contract C7): a produced path can only vouch for
  // the most recent execution that wrote it. When a different fingerprint
  // claims any of the same paths, drop that claim BEFORE recording ours, so
  // the older fingerprint can never serve the newer execution's bytes.
  const QStringList claimPaths =
    QStringList( execution.producedArtifacts ) << execution.declaredOutputPath;
  for ( const QString &claimPath : claimPaths )
  {
    if ( claimPath.isEmpty() )
      continue;
    const auto ownerIt = m_pathOwners.constFind( claimPath );
    if ( ownerIt != m_pathOwners.constEnd() && ownerIt.value() != fp.digest )
      m_executions.remove( ownerIt.value() );
  }

  const qint64 tick = ++m_clock;
  auto it = m_executions.find( fp.digest );
  if ( it != m_executions.end() )
  {
    // Re-claim: release the previous artifact claims that are not re-claimed.
    QStringList previous = it->execution.producedArtifacts
                           << it->execution.declaredOutputPath;
    for ( const QString &previousPath : previous )
    {
      const auto ownerIt = m_pathOwners.constFind( previousPath );
      if ( ownerIt != m_pathOwners.constEnd() && ownerIt.value() == fp.digest
           && !claimPaths.contains( previousPath ) )
      {
        m_pathOwners.remove( previousPath );
      }
    }
    it->execution = execution;
    it->lastUsedTick = tick;
  }
  else
  {
    m_executions.insert( fp.digest, ExecutionEntry{ execution, tick } );
  }
  for ( const QString &claimPath : claimPaths )
  {
    if ( !claimPath.isEmpty() )
      m_pathOwners.insert( claimPath, fp.digest );
  }
  evictIfNeededLocked();

  // Phase E: persist into the content-addressed tier. Conservative: the
  // execution is recorded only when EVERY produced artifact pools cleanly;
  // a partial failure records nothing (a lookup never serves a half-cached
  // execution). Per-object size cap avoids pathological copies.
  ensurePersistentTierLocked();
  if ( m_persistent )
  {
    sicnu::data::PoolExecution pooledExecution;
    pooledExecution.declaredOriginal = execution.declaredOutputPath;
    pooledExecution.payloadJson = execution.resultPayload.toJson();
    pooledExecution.inputSizes = execution.inputSizes;
    pooledExecution.inputMsecs = execution.inputMsecs;
    bool complete = true;
    for ( const QString &artifact : execution.producedArtifacts )
    {
      const bool declared = artifact == execution.declaredOutputPath;
      auto object = m_persistent->put( artifact, declared );
      if ( !object || object->size > kMaxPooledObjectBytes )
      {
        complete = false;
        break;
      }
      pooledExecution.objects.append( *object );
    }
    if ( complete && !pooledExecution.objects.isEmpty() )
    {
      m_persistent->recordExecution( fp.toHex(), pooledExecution );
      const qint64 cap = qEnvironmentVariableIntValue( "SICNU_ARTIFACT_CACHE_MAX_GB" );
      m_persistent->evictToBytes( ( cap > 0 ? cap : 8 ) * ( 1024LL << 30 ) );
    }
    else
    {
      m_persistent->forgetExecution( fp.toHex() );
    }
  }
}

void ExecutionResultCache::ensurePersistentTierLocked()
{
  if ( m_persistentInitTried )
    return;
  m_persistentInitTried = true;
  const char *env = std::getenv( "SICNU_ARTIFACT_CACHE" );
  const bool enabled = env && ( env[0] == '1' || env[0] == 't' || env[0] == 'T' );
  if ( !enabled )
    return;
  QString root;
  if ( const char *rootEnv = std::getenv( "SICNU_ARTIFACT_CACHE_DIR" ) )
    root = QString::fromUtf8( rootEnv );
  else
    root = QDir::home().filePath( QStringLiteral( ".rs_studio/artifact-cache" ) );
  auto pool = std::make_unique<sicnu::data::ArtifactObjectPool>();
  if ( pool->enable( root ) )
    m_persistent = std::move( pool );
}

QStringList ExecutionResultCache::cachedArtifacts() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  QStringList paths;
  paths.reserve( m_executions.size() * 2 );
  for ( auto it = m_executions.begin(); it != m_executions.end(); ++it )
  {
    if ( !it->execution.declaredOutputPath.isEmpty() )
      paths.append( it->execution.declaredOutputPath );
    for ( const QString &artifact : it->execution.producedArtifacts )
    {
      if ( !artifact.isEmpty() )
        paths.append( artifact );
    }
  }
  return paths;
}

int ExecutionResultCache::pathSize() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_executions.size();
}

void ExecutionResultCache::storeOutputPath( const ExecutionFingerprint &fp,
                                            const QString &outputPath )
{
  if ( outputPath.isEmpty() )
    return;
  CachedExecution execution;
  execution.declaredOutputPath = outputPath;
  execution.producedArtifacts.append( outputPath );
  // Stat the artifact so the entry participates in the same size/mtime
  // validation as storeExecution()-recorded results (a wrapper-stored entry
  // must not bypass the replaced-file check).
  const QFileInfo info( outputPath );
  if ( info.isFile() )
  {
    execution.artifactSizes.insert( outputPath, info.size() );
    execution.artifactMsecs.insert( outputPath, info.lastModified().toMSecsSinceEpoch() );
  }
  storeExecution( fp, execution );
}

std::optional<QString> ExecutionResultCache::lookupOutputPath( const ExecutionFingerprint &fp )
{
  const auto execution = lookupExecution( fp );
  if ( !execution )
    return std::nullopt;
  return execution->declaredOutputPath;
}

void ExecutionResultCache::clear()
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  m_entries.clear();
  // A "cache reset" must also drop the execution store (#720, #726): stale
  // entries would let lookupExecution() serve pre-clear results while
  // size()/pathSize() disagree, and their artifacts would stay GC-protected
  // forever.
  m_executions.clear();
  m_pathOwners.clear();
}

int ExecutionResultCache::size() const
{
  std::lock_guard<std::recursive_mutex> locker( m_mutex );
  return m_entries.size();
}

} // namespace sicnu::data
