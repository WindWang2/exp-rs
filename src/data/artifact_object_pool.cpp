// artifact_object_pool.cpp — see artifact_object_pool.h for the contract.
#include "artifact_object_pool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <unistd.h>

#include <algorithm>
#include <vector>

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

    if ( !QFile::exists( objectPath ) )
    {
        // Stage-and-verify: copy to a pool-private temp, re-hash the STAGED
        // bytes, and only then rename into the content address. A partial
        // copy or a concurrent source rewrite can never become an object.
        const QString tmp = objectPath + QStringLiteral( ".%1.puttmp" ).arg( ::getpid() );
        QFile::remove( tmp );
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
        if ( !QFile::rename( tmp, objectPath ) )
        {
            QFile::remove( tmp );
            return std::nullopt;
        }
    }
    else
    {
        // Content-addressed dedup: the existing object must genuinely carry
        // its digest (corrupted-object hygiene at put time).
        const QString existingDigest = artifactContentDigest( objectPath, &digestError );
        if ( existingDigest != digest )
            return std::nullopt;
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
        reg.metadata = meta;
        const auto result = m_store.registerArtifact( reg );
        if ( !result )
            return false;
    }
    return true;
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
    // Completeness contract: the declared object must be present.
    const bool hasDeclared = std::any_of( execution.objects.cbegin(), execution.objects.cend(),
                                          []( const PoolObject &o ) { return o.declared; } );
    if ( !hasDeclared )
        return std::nullopt;
    return execution;
}

bool ArtifactObjectPool::forgetExecution( const QString &fingerprintHex )
{
    if ( !m_enabled || fingerprintHex.isEmpty() )
        return false;
    const QVector<ArtifactRecord> records = m_store.byProducerFingerprint( fingerprintHex );
    for ( const ArtifactRecord &record : records )
        m_store.forget( record.artifactId );
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
            total += QFileInfo( prefixDir.filePath( object ) ).size();
    }
    return total;
}

qint64 ArtifactObjectPool::evictToBytes( qint64 maxBytes )
{
    if ( !m_enabled || totalObjectBytes() <= maxBytes )
        return 0;
    // Conservative eviction: whole executions whose records are trash-state
    // and unreferenced (the ArtifactStore reap rules), oldest first. Live
    // executions are never evicted — only explicit forgetEntry/age-out.
    qint64 freed = 0;
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 60 * 1000; // 1 min grace
    QVector<ArtifactRecord> reapable = m_store.reapable( cutoff );
    std::sort( reapable.begin(), reapable.end(),
               []( const ArtifactRecord &a, const ArtifactRecord &b ) {
                   return a.lastTouchMs < b.lastTouchMs;
               } );
    for ( const ArtifactRecord &record : reapable )
    {
        if ( totalObjectBytes() - freed <= maxBytes )
            break;
        const QFileInfo objectInfo( record.storagePath );
        const qint64 size = objectInfo.size();
        if ( QFile::remove( record.storagePath ) )
        {
            freed += size;
            m_store.forget( record.artifactId );
        }
    }
    return freed;
}

} // namespace sicnu::data
