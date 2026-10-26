// artifact_object_pool.cpp — see artifact_object_pool.h for the contract.
#include "artifact_object_pool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "platform/portable.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "runtime/observability/fault_point.h"

namespace sicnu::data
{
namespace
{
QString digestPrefixDir( const QString &digest )
{
    return digest.left( 2 );
}

QJsonObject encodeExecutionMeta( const PoolExecution &execution )
{
    QJsonObject meta;
    meta[ QStringLiteral( "kind" ) ] = QStringLiteral( "execution_cache" );
    meta[ QStringLiteral( "declaredOriginal" ) ] = execution.declaredOriginal;
    meta[ QStringLiteral( "payloadB64" ) ] =
        QString::fromLatin1( execution.payloadJson.toBase64() );
    QJsonObject inputSizes;
    for ( auto it = execution.inputSizes.constBegin(); it != execution.inputSizes.constEnd(); ++it )
        inputSizes.insert( it.key(), it.value() );
    meta[ QStringLiteral( "inputSizes" ) ] = inputSizes;
    QJsonObject inputMsecs;
    for ( auto it = execution.inputMsecs.constBegin(); it != execution.inputMsecs.constEnd(); ++it )
        inputMsecs.insert( it.key(), it.value() );
    meta[ QStringLiteral( "inputMsecs" ) ] = inputMsecs;
    return meta;
}

// Decodes the execution-level metadata carried on every object record of the
// execution (all records duplicate the declared path so lookup can assemble
// the complete set without a second index).
bool decodeExecutionMeta( const QJsonObject &meta, QString *declaredOriginal,
                          QByteArray *payloadJson, QMap<QString, qint64> *inputSizes,
                          QMap<QString, qint64> *inputMsecs )
{
    if ( meta.value( QStringLiteral( "kind" ) ).toString() != QStringLiteral( "execution_cache" ) )
        return false;
    *declaredOriginal = meta.value( QStringLiteral( "declaredOriginal" ) ).toString();
    *payloadJson = QByteArray::fromBase64(
        meta.value( QStringLiteral( "payloadB64" ) ).toString().toLatin1() );
    const QJsonObject sizes = meta.value( QStringLiteral( "inputSizes" ) ).toObject();
    for ( auto it = sizes.constBegin(); it != sizes.constEnd(); ++it )
        inputSizes->insert( it.key(), static_cast<qint64>( it.value().toDouble( 0 ) ) );
    const QJsonObject msecs = meta.value( QStringLiteral( "inputMsecs" ) ).toObject();
    for ( auto it = msecs.constBegin(); it != msecs.constEnd(); ++it )
        inputMsecs->insert( it.key(), static_cast<qint64>( it.value().toDouble( 0 ) ) );
    return !declaredOriginal->isEmpty();
}
} // namespace

ArtifactObjectPool::~ArtifactObjectPool()
{
    // ArtifactStore closes itself.
}

bool ArtifactObjectPool::enable( const QString &rootDir, QString *errorOut )
{
    QDir dir( rootDir );
    if ( !dir.mkpath( QStringLiteral( "." ) ) )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot create artifact pool root %1" ).arg( rootDir );
        return false;
    }
    const QString objectsDir = dir.filePath( QStringLiteral( "objects" ) );
    if ( !QDir().mkpath( objectsDir ) )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot create artifact pool objects dir" );
        return false;
    }
    if ( !m_store.open( dir.filePath( QStringLiteral( "store.sqlite3" ) ), errorOut ) )
        return false;
    m_root = rootDir;
    m_objectsDir = objectsDir;
    m_enabled = true;
    return true;
}

std::optional<PoolObject> ArtifactObjectPool::put( const QString &filePath, bool declared )
{
    if ( !m_enabled )
        return std::nullopt;
    QFileInfo info( filePath );
    if ( !info.isFile() || info.size() <= 0 )
        return std::nullopt;

    QString digestError;
    const QString digest = artifactContentDigest( filePath, &digestError );
    if ( digest.isEmpty() )
        return std::nullopt;

    const QString dir = m_objectsDir + QLatin1Char( '/' ) + digestPrefixDir( digest );
    if ( !QDir().mkpath( dir ) )
        return std::nullopt;
    const QString objectPath = dir + QLatin1Char( '/' ) + digest;

    for ( ;; )
    {
        if ( QFile::exists( objectPath ) )
        {
            // Content-addressed dedup: the existing object must genuinely
            // carry its digest (corrupted-object hygiene at put time).
            const QString existingDigest = artifactContentDigest( objectPath, &digestError );
            if ( existingDigest == digest )
                break;
            // R20-02: a torn object under this content address (crash between
            // publish and durability, external bit rot) poisoned the digest
            // FOREVER before — every re-put landed here and returned nullopt,
            // and eviction never collects an object no record references,
            // contradicting the pool's "corruption self-heals" contract.
            // Drop the bad bytes and re-stage from the still-healthy source
            // (loop, so a concurrent re-tearer cannot grow the stack); if
            // the removal fails the digest stays refused (fail-closed) and
            // the on-disk bytes stay accounted (R2-2: subtract only after a
            // successful remove — a failed one must not shrink the cache).
            const qint64 tornBytes = QFileInfo( objectPath ).size();
            if ( !QFile::remove( objectPath ) )
                return std::nullopt;
            if ( m_cachedTotalBytes >= 0 )
                m_cachedTotalBytes -= tornBytes;
            continue;
        }
        // Stage-and-verify: copy to a pool-private temp, re-hash the STAGED
        // bytes, and only then rename into the content address. A partial
        // copy or a concurrent source rewrite can never become an object.
        // #1680: the temp name must be unique per CALL, not per process —
        // two pool instances (or threads) in one process staging the same
        // digest used to share one ".<pid>.puttmp" and removed/overwrote
        // each other's staged bytes mid-copy.
        static std::atomic<unsigned> sPutTempCounter{ 0 };
        const QString tmp = objectPath + QStringLiteral( ".%1.%2.puttmp" )
                              .arg( static_cast<int>( sicnu::portable::pid() ) )
                              .arg( sPutTempCounter.fetch_add( 1 ) + 1, 0, 16 );
        QFile::remove( tmp );
        // Injected staging failures (Verification 7.0 fault matrix): route
        // through the real failure paths — copy failure cleans the tmp file,
        // publish-rename failure leaves the object unpooled but healthy.
        if ( SICNU_FAULT_POINT( "artifact_pool.stage_copy" ) )
        {
            QFile::remove( tmp );
            return std::nullopt;
        }
        if ( !QFile::copy( filePath, tmp ) )
        {
            QFile::remove( tmp );
            return std::nullopt;
        }
        const QString stagedDigest = artifactContentDigest( tmp, &digestError );
        if ( stagedDigest != digest )
        {
            QFile::remove( tmp );
            return std::nullopt;
        }
        // R20-02: durability gate before the content address takes the bytes
        // (the pool-side twin of the platform sidecar doctrine — temp +
        // fsync + rename + directory sync): renaming unflushed bytes could
        // commit the object's directory entry for data that never reached
        // the disk, i.e. a torn object under a valid content address after
        // a power loss.
        if ( !sicnu::portable::syncFileUtf8( tmp.toStdString() ) )
        {
            QFile::remove( tmp );
            return std::nullopt;
        }
        const bool publishFault = SICNU_FAULT_POINT( "artifact_pool.stage_publish" );
        if ( publishFault || !QFile::rename( tmp, objectPath ) )
        {
            // #1680: QFile::rename REFUSES to replace an existing
            // destination. Two concurrent put()s of one digest both staged
            // (neither saw the object yet); the loser's rename fails BECAUSE
            // the winner just published the identical bytes. That is dedup
            // working, not a failure: re-enter the loop — the top's digest
            // re-verification either accepts the healthy object or, for a
            // torn one, drops and re-stages it. Only a rename failure with
            // NO object at the address is a real publish failure.
            QFile::remove( tmp );
            if ( !publishFault && QFile::exists( objectPath ) )
                continue;
            return std::nullopt;
        }
        // Commit the rename's directory entry too (durable_sidecar.cpp
        // doctrine: fsync → rename → best-effort directory sync).
        sicnu::portable::syncDirectoryBestEffortUtf8( dir.toStdString() );
        // New object bytes landed: keep the incremental storage accounting
        // accurate without a full-tree re-walk (issue #758-5).
        if ( m_cachedTotalBytes >= 0 )
            m_cachedTotalBytes += QFileInfo( objectPath ).size();
        break;
    }

    const QFileInfo objectInfo( objectPath );
    PoolObject object;
    object.originalPath = filePath;
    object.digest = digest;
    object.poolPath = objectPath;
    object.size = objectInfo.size();
    object.msecs = objectInfo.lastModified().toMSecsSinceEpoch();
    object.declared = declared;
    return object;
}

std::optional<PoolObject> ArtifactObjectPool::objectByDigest( const QString &digestHex )
{
    if ( !m_enabled || digestHex.isEmpty() )
        return std::nullopt;
    // A digest is 64 lowercase hex characters; anything else (including a
    // path-traversal attempt through a caller-supplied string) is refused
    // before it ever reaches the filesystem.
    if ( digestHex.size() != 64 )
        return std::nullopt;
    for ( const QChar ch : digestHex )
    {
        const bool hex = ( ch >= QLatin1Char( '0' ) && ch <= QLatin1Char( '9' ) )
                         || ( ch >= QLatin1Char( 'a' ) && ch <= QLatin1Char( 'f' ) );
        if ( !hex )
            return std::nullopt;
    }
    const QString objectPath = m_objectsDir + QLatin1Char( '/' )
                               + digestPrefixDir( digestHex ) + QLatin1Char( '/' ) + digestHex;
    const QFileInfo info( objectPath );
    if ( !info.isFile() || info.size() <= 0 )
        return std::nullopt;
    // Serve only verified bytes: the address claims content, the bytes must
    // prove it (mirrors lookupExecution's per-object re-verification).
    QString digestError;
    const QString actual = artifactContentDigest( objectPath, &digestError );
    if ( !digestError.isEmpty() || actual != digestHex )
        return std::nullopt;
    PoolObject object;
    object.originalPath = QString(); // unknown: content-addressed, not run-addressed
    object.digest = digestHex;
    object.poolPath = objectPath;
    object.size = info.size();
    object.msecs = info.lastModified().toMSecsSinceEpoch();
    object.declared = false;
    return object;
}

bool ArtifactObjectPool::recordExecution( const QString &fingerprintHex,
                                          const PoolExecution &execution )
{
    if ( !m_enabled || fingerprintHex.isEmpty() || execution.declaredOriginal.isEmpty()
         || execution.objects.isEmpty() )
        return false;

    // Single claim per fingerprint, mirroring the in-memory store: a fresh
    // execution of the same identity replaces the previous record set, so
    // version churn can never produce duplicate object rows on lookup.
    forgetExecution( fingerprintHex );

    // Every object record carries the execution metadata, so lookup can
    // assemble the set from any record; the declared record is the anchor.
    for ( const PoolObject &object : execution.objects )
    {
        ArtifactRegistration reg;
        reg.logicalKey = QStringLiteral( "cache/%1/%2" ).arg( fingerprintHex, object.originalPath );
        reg.storagePath = object.poolPath;
        reg.kind = QStringLiteral( "cache_object" );
        reg.producerFingerprint = fingerprintHex;
        reg.contentDigest = object.digest;
        reg.sizeBytes = object.size;
        reg.mtimeMs = object.msecs;
        QJsonObject meta = encodeExecutionMeta( execution );
        meta[ QStringLiteral( "declaredRole" ) ] = object.declared;
        meta[ QStringLiteral( "objectOriginal" ) ] = object.originalPath;
        // Completeness marker: every record carries the expected set size, so
        // a partially-recorded execution is refused at lookup (never served).
        meta[ QStringLiteral( "objectCount" ) ] = static_cast<int>( execution.objects.size() );
        reg.metadata = meta;
        const auto result = m_store.registerArtifact( reg );
        if ( !result )
            return false;
    }
    // Completeness proof: re-read the records and require the full set.
    const auto verify = lookupExecution( fingerprintHex );
    return verify && static_cast<int>( verify->objects.size() )
        == static_cast<int>( execution.objects.size() );
}

std::optional<PoolExecution> ArtifactObjectPool::lookupExecution( const QString &fingerprintHex )
{
    if ( !m_enabled || fingerprintHex.isEmpty() )
        return std::nullopt;

    const QVector<ArtifactRecord> records = m_store.byProducerFingerprint( fingerprintHex );
    if ( records.isEmpty() )
        return std::nullopt;

    // All records of one execution share the declaredOriginal; pick it from
    // the first parseable record, then require the declared record to exist.
    QString declaredOriginal;
    QByteArray payload;
    QMap<QString, qint64> inputSizes, inputMsecs;
    for ( const ArtifactRecord &record : records )
    {
        QString parsedDeclared;
        QByteArray parsedPayload;
        QMap<QString, qint64> parsedSizes, parsedMsecs;
        if ( decodeExecutionMeta( record.metadata, &parsedDeclared, &parsedPayload, &parsedSizes,
                                  &parsedMsecs ) )
        {
            declaredOriginal = parsedDeclared;
            payload = parsedPayload;
            inputSizes = parsedSizes;
            inputMsecs = parsedMsecs;
            break;
        }
    }
    if ( declaredOriginal.isEmpty() )
        return std::nullopt;

    PoolExecution execution;
    execution.declaredOriginal = declaredOriginal;
    execution.payloadJson = payload;
    execution.inputSizes = inputSizes;
    execution.inputMsecs = inputMsecs;

    for ( const ArtifactRecord &record : records )
    {
        const QJsonObject meta = record.metadata;
        if ( meta.value( QStringLiteral( "declaredOriginal" ) ).toString() != declaredOriginal )
            continue; // a different execution that shares the fingerprint index
        // Content validation (FAILURE_MATRIX: cache corruption ⇒ self-heal).
        const QString actualDigest = artifactContentDigest( record.storagePath );
        if ( actualDigest.isEmpty() || actualDigest != record.contentDigest )
            return std::nullopt;

        PoolObject object;
        object.originalPath = meta.value( QStringLiteral( "objectOriginal" ) ).toString();
        if ( object.originalPath.isEmpty() )
            return std::nullopt;
        object.digest = record.contentDigest;
        object.poolPath = record.storagePath;
        object.size = record.sizeBytes;
        object.msecs = record.mtimeMs;
        object.declared = meta.value( QStringLiteral( "declaredRole" ) ).toBool();
        execution.objects.append( object );
    }
    // Completeness contract: declared object present AND recorded set size
    // matches the declared count (a partial record set from a mid-set crash
    // is refused, never partially served).
    const bool hasDeclared = std::any_of( execution.objects.cbegin(), execution.objects.cend(),
                                          []( const PoolObject &o ) { return o.declared; } );
    if ( !hasDeclared )
        return std::nullopt;
    qint64 expectedCount = -1;
    for ( const ArtifactRecord &record : records )
    {
        const QJsonObject meta = record.metadata;
        if ( meta.value( QStringLiteral( "declaredOriginal" ) ).toString() != declaredOriginal )
            continue;
        expectedCount = meta.value( QStringLiteral( "objectCount" ) ).toInt( -1 );
        if ( expectedCount >= 0 )
            break;
    }
    if ( expectedCount < 0
         || static_cast<qint64>( execution.objects.size() ) != expectedCount )
        return std::nullopt;
    return execution;
}

bool ArtifactObjectPool::forgetExecution( const QString &fingerprintHex )
{
    if ( !m_enabled || fingerprintHex.isEmpty() )
        return false;
    // Trash-state instead of hard forget: rows survive until eviction
    // physically removes the objects; trash + zero refs is exactly what
    // reapable() returns, which is what makes the eviction budget real.
    const QVector<ArtifactRecord> records = m_store.byProducerFingerprint( fingerprintHex );
    for ( const ArtifactRecord &record : records )
        m_store.markTrash( record.artifactId );
    return true;
}

qint64 ArtifactObjectPool::totalObjectBytes() const
{
    // Sum pool objects tracked by the store; objects whose records were
    // forgotten but bytes remain are counted on disk here too.
    qint64 total = 0;
    QDir objectsDir( m_objectsDir );
    const auto entries = objectsDir.entryList( QDir::Dirs | QDir::NoDotAndDotDot );
    for ( const QString &prefix : entries )
    {
        const QDir prefixDir( m_objectsDir + QLatin1Char( '/' ) + prefix );
        for ( const QString &object : prefixDir.entryList( QDir::Files ) )
        {
            if ( object.endsWith( QStringLiteral( ".puttmp" ) ) )
                continue; // in-flight staging, not usage
            total += QFileInfo( prefixDir.filePath( object ) ).size();
        }
    }
    m_cachedTotalBytes = total;
    return total;
}

qint64 ArtifactObjectPool::evictToBytes( qint64 maxBytes )
{
    if ( !m_enabled )
        return 0;
    // Incremental accounting (issue #758-5): the under-budget fast path must
    // not full-tree-walk the objects dir on every cache store. The cached
    // total is maintained by put() (additions) and here (removals); a full
    // walk runs only when the budget is exceeded or the total is unknown.
    if ( m_cachedTotalBytes >= 0 && m_cachedTotalBytes <= maxBytes )
        return 0;
    // Conservative eviction: whole executions whose records are trash-state
    // and unreferenced (the ArtifactStore reap rules), oldest first. Live
    // executions are never evicted — only explicit forgetEntry/age-out.
    qint64 freed = 0;
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - m_evictionGraceMs;
    QVector<ArtifactRecord> reapable = m_store.reapable( cutoff );
    std::sort( reapable.begin(), reapable.end(),
               []( const ArtifactRecord &a, const ArtifactRecord &b ) {
                   return a.lastTouchMs < b.lastTouchMs;
               } );
    qint64 totalNow = totalObjectBytes(); // budget exceeded: walk is warranted
    for ( const ArtifactRecord &record : reapable )
    {
        if ( totalNow - freed <= maxBytes )
            break;
        // Reference-safe CAS eviction (issue #758-5): byte-identical outputs
        // of different fingerprints dedup onto ONE data/<digest> object. When
        // a live record still references the digest, drop only this trash
        // record's row (a cache miss for the evicted execution) and keep the
        // shared object bytes for the live user.
        const std::optional<ArtifactRecord> liveUser =
            m_store.liveByContentDigest( record.contentDigest );
        if ( !record.contentDigest.isEmpty() && liveUser
             && liveUser->artifactId != record.artifactId )
        {
            m_store.forget( record.artifactId );
            continue;
        }
        const QFileInfo objectInfo( record.storagePath );
        if ( !objectInfo.isFile() )
        {
            // Orphaned metadata row (bytes already gone): drop the row so a
            // dead record cannot spin in every future eviction pass.
            m_store.forget( record.artifactId );
            continue;
        }
        const qint64 size = objectInfo.size();
        if ( QFile::remove( record.storagePath ) )
        {
            freed += size;
            totalNow -= size;
            m_store.forget( record.artifactId );
        }
    }
    m_cachedTotalBytes = totalNow;
    return freed;
}

} // namespace sicnu::data
