// catalog_record_store.cpp — implementation of the immutable chunked catalog
// record store and its path index. See catalog_record_store.h for the design
// contract; the short version: publication is O(1) (structural sharing), a
// mutation copies at most one shard (bounded by kShardRecords), and path
// lookups probe a generation-scoped key index instead of rescanning and
// re-canonicalizing the whole catalog.

#include "catalog_record_store.h"

#include <QFileInfo>

namespace sicnu::data::internal
{

CatalogScaleCounters &catalogScaleCounters()
{
    static CatalogScaleCounters counters;
    return counters;
}

bool isVirtualOrRemotePath( const QString &path )
{
    return path.startsWith( QLatin1String( "/vsi" ), Qt::CaseInsensitive ) ||
           path.startsWith( QLatin1String( "http://" ), Qt::CaseInsensitive ) ||
           path.startsWith( QLatin1String( "https://" ), Qt::CaseInsensitive );
}

QStringList virtualPathAliases( const QString &path )
{
    QStringList aliases;
    aliases.append( path );
    const QString prefix = QStringLiteral( "/vsicurl/" );
    if ( path.startsWith( QLatin1String( "http://" ), Qt::CaseInsensitive ) ||
         path.startsWith( QLatin1String( "https://" ), Qt::CaseInsensitive ) )
    {
        aliases.append( prefix + path );
    }
    else if ( path.startsWith( prefix, Qt::CaseInsensitive ) )
    {
        const QString rest = path.mid( prefix.size() );
        if ( rest.startsWith( QLatin1String( "http://" ), Qt::CaseInsensitive ) ||
             rest.startsWith( QLatin1String( "https://" ), Qt::CaseInsensitive ) )
            aliases.append( rest );
    }
    return aliases;
}

namespace
{

QString aliasKey( const QString &payload )
{
    return QStringLiteral( "a|" ) + payload;
}

QString pathKey( const QString &payload )
{
    return QStringLiteral( "p|" ) + payload;
}

/// Resolves @p id in @p sealed (oldest→newest) followed by @p tail.
const AssetRecord *locateIn(
    const std::shared_ptr<const QVector<std::shared_ptr<const RecordShard>>> &sealed,
    const std::shared_ptr<const RecordShard> &tail, AssetId id )
{
    if ( sealed )
    {
        for ( const auto &shard : *sealed )
        {
            for ( const AssetRecord &record : shard->records )
            {
                catalogScaleCounters().recordVisits.fetch_add( 1, std::memory_order_relaxed );
                if ( record.snapshot.id() == id )
                    return &record;
            }
        }
    }
    if ( tail )
    {
        for ( const AssetRecord &record : tail->records )
        {
            catalogScaleCounters().recordVisits.fetch_add( 1, std::memory_order_relaxed );
            if ( record.snapshot.id() == id )
                return &record;
        }
    }
    return nullptr;
}

/// Probes one shard's key index. Returns the owning record of the lowest-index
/// (== earliest-inserted) matching entry, or nullptr.
const AssetRecord *probeShard( const RecordShard &shard, const QStringList &keys )
{
    int best = -1;
    for ( const QString &key : keys )
    {
        catalogScaleCounters().pathIndexLookups.fetch_add( 1, std::memory_order_relaxed );
        const auto it = shard.byKey.constFind( key );
        if ( it == shard.byKey.constEnd() || it->isEmpty() )
            continue;
        if ( best < 0 || it->first().recordIndex < best )
            best = it->first().recordIndex;
    }
    return best >= 0 ? &shard.records[ best ] : nullptr;
}

} // namespace

// --- SnapshotView -----------------------------------------------------------

const AssetRecord *CatalogRecordStore::SnapshotView::find( AssetId id ) const
{
    return locateIn( sealed, tail, id );
}

const AssetRecord *CatalogRecordStore::SnapshotView::probe( const QString &path ) const
{
    bool queryVirtual = false;
    const QStringList keys = queryKeys( path, queryVirtual );
    if ( keys.isEmpty() )
        return nullptr;

    // Shards are walked oldest→newest and records never move between shards,
    // so the first shard with a hit owns the earliest-inserted match — the
    // same record the legacy linear scan would have returned.
    if ( sealed )
    {
        for ( const auto &shard : *sealed )
        {
            if ( const AssetRecord *hit = probeShard( *shard, keys ) )
                return hit;
        }
    }
    if ( tail )
        return probeShard( *tail, keys );
    return nullptr;
}

// --- live side --------------------------------------------------------------

const AssetRecord *CatalogRecordStore::find( AssetId id ) const
{
    return locateIn( m_sealed, m_tail, id );
}

bool CatalogRecordStore::locate( AssetId id, int &shardOut, int &recOut ) const
{
    const int sealed = sealedCount();
    for ( int i = 0; i < sealed; ++i )
    {
        const RecordShard &shard = *( *m_sealed )[ i ];
        for ( int j = 0; j < static_cast<int>( shard.records.size() ); ++j )
        {
            catalogScaleCounters().recordVisits.fetch_add( 1, std::memory_order_relaxed );
            if ( shard.records[ j ].snapshot.id() == id )
            {
                shardOut = i;
                recOut = j;
                return true;
            }
        }
    }
    if ( m_tail )
    {
        for ( int j = 0; j < static_cast<int>( m_tail->records.size() ); ++j )
        {
            catalogScaleCounters().recordVisits.fetch_add( 1, std::memory_order_relaxed );
            if ( m_tail->records[ j ].snapshot.id() == id )
            {
                shardOut = sealed;
                recOut = j;
                return true;
            }
        }
    }
    return false;
}

CatalogRecordStore::SealedShards &CatalogRecordStore::sealedForWrite()
{
    if ( m_sealedShared )
    {
        // Copy the shard LIST (pointer copies only) — the shard objects
        // themselves stay shared with published snapshots and immutable.
        m_sealed = std::make_shared<const SealedShards>(
            m_sealed ? *m_sealed : SealedShards{} );
        m_sealedShared = false;
    }
    return const_cast<SealedShards &>( *m_sealed );
}

RecordShard &CatalogRecordStore::shardForWrite( int i )
{
    SealedShards &shards = sealedForWrite();
    if ( m_privateSealed != i )
    {
        catalogScaleCounters().shardCopies.fetch_add( 1, std::memory_order_relaxed );
        catalogScaleCounters().recordCopies.fetch_add(
            shards[ i ]->records.size(), std::memory_order_relaxed );
        shards[ i ] = std::make_shared<RecordShard>( *shards[ i ] );
        m_privateSealed = i;
    }
    return const_cast<RecordShard &>( *shards[ i ] );
}

RecordShard &CatalogRecordStore::tailForWrite()
{
    if ( m_tailShared || !m_tail )
    {
        if ( m_tail )
        {
            catalogScaleCounters().shardCopies.fetch_add( 1, std::memory_order_relaxed );
            catalogScaleCounters().recordCopies.fetch_add(
                m_tail->records.size(), std::memory_order_relaxed );
            m_tail = std::make_shared<RecordShard>( *m_tail );
        }
        else
        {
            m_tail = std::make_shared<RecordShard>();
        }
        m_tailShared = false;
    }
    return const_cast<RecordShard &>( *m_tail );
}

void CatalogRecordStore::sealTail()
{
    if ( !m_tail || m_tail->records.isEmpty() )
        return;
    const bool tailWasPrivate = !m_tailShared;
    SealedShards &shards = sealedForWrite();
    shards.append( m_tail );
    // The sealed tail keeps its identity; it is private (and may be mutated in
    // place later) only when no published snapshot aliased it.
    m_privateSealed = tailWasPrivate ? static_cast<int>( shards.size() ) - 1 : -1;
    m_tail = std::make_shared<RecordShard>();
    m_tailShared = false;
    catalogScaleCounters().shardSeals.fetch_add( 1, std::memory_order_relaxed );
}

QStringList CatalogRecordStore::recordKeys( const AssetRecord &record )
{
    const QString &stored = record.snapshot.source().canonicalSource;
    QStringList keys;
    if ( stored.isEmpty() )
        return keys;

    if ( isVirtualOrRemotePath( stored ) )
    {
        // Remote/VSI identity is string + alias only: QFileInfo mangles these
        // spellings, so the path tiers would fabricate identities.
        for ( const QString &alias : virtualPathAliases( stored ) )
            keys.append( aliasKey( alias ) );
        keys.removeDuplicates();
        return keys;
    }

    keys.append( aliasKey( stored ) );
    keys.append( pathKey( stored ) );
    const QFileInfo info( stored );
    const QString absolute = info.absoluteFilePath();
    if ( !absolute.isEmpty() && absolute != stored )
        keys.append( pathKey( absolute ) );
    const QString canonical = info.canonicalFilePath();
    if ( !canonical.isEmpty() && canonical != stored )
    {
        keys.append( pathKey( canonical ) );
        catalogScaleCounters().pathCanonicalizations.fetch_add( 1, std::memory_order_relaxed );
    }
    keys.removeDuplicates();
    return keys;
}

QStringList CatalogRecordStore::queryKeys( const QString &path, bool &virtualOut )
{
    QStringList keys;
    if ( path.trimmed().isEmpty() )
        return keys;

    virtualOut = isVirtualOrRemotePath( path );
    for ( const QString &alias : virtualPathAliases( path ) )
        keys.append( aliasKey( alias ) );
    if ( virtualOut )
    {
        keys.removeDuplicates();
        return keys;
    }

    keys.append( pathKey( path ) );
    const QFileInfo info( path );
    const QString canonical = info.canonicalFilePath();
    if ( !canonical.isEmpty() && canonical != path )
    {
        keys.append( pathKey( canonical ) );
        catalogScaleCounters().pathCanonicalizations.fetch_add( 1, std::memory_order_relaxed );
    }
    const QString absolute = info.absoluteFilePath();
    if ( !absolute.isEmpty() && absolute != path )
        keys.append( pathKey( absolute ) );
    keys.removeDuplicates();
    return keys;
}

QString CatalogRecordStore::sourceKeyOf( const SourceDescriptor &descriptor )
{
    // Faithful serialization of the SourceKey fields. authConfigId is
    // deliberately excluded: SourceKey ignores it, so two descriptors that
    // differ only in authConfigId share one identity. QMap iterates sorted,
    // so the serialization is deterministic.
    QString key = descriptor.providerKey + QChar( 0x1f ) + descriptor.canonicalSource +
                  QChar( 0x1f ) + descriptor.subdataset;
    for ( auto it = descriptor.dataOptions.constBegin();
          it != descriptor.dataOptions.constEnd(); ++it )
    {
        key += QChar( 0x1f ) + it.key() + QChar( 0x1e ) + it.value();
    }
    return key;
}

std::optional<AssetId> CatalogRecordStore::sourceKeyOwner(
    const SourceDescriptor &descriptor ) const
{
    const auto it = m_bySourceKey.constFind( sourceKeyOf( descriptor ) );
    if ( it == m_bySourceKey.constEnd() )
        return std::nullopt;
    return *it;
}

void CatalogRecordStore::reindexSourceKey( AssetId id,
                                           const SourceDescriptor &newDescriptor )
{
    int shard = -1;
    int rec = -1;
    if ( !locate( id, shard, rec ) )
        return;
    const RecordShard &target = shard == sealedCount() ? *m_tail : *( *m_sealed )[ shard ];
    m_bySourceKey.remove( sourceKeyOf( target.records[ rec ].snapshot.source() ) );
    m_bySourceKey.insert( sourceKeyOf( newDescriptor ), id );
}

void CatalogRecordStore::insertKeys( RecordShard &shard, int recordIndex,
                                     const QStringList &keys )
{
    for ( const QString &key : keys )
        shard.byKey[ key ].append( PathKeyEntry{ recordIndex } );
}

void CatalogRecordStore::removeKeys( RecordShard &shard, int recordIndex )
{
    for ( const QString &key : shard.keys.value( recordIndex ) )
    {
        auto it = shard.byKey.find( key );
        if ( it == shard.byKey.end() )
            continue;
        it->removeIf( [recordIndex]( const PathKeyEntry &entry ) {
            return entry.recordIndex == recordIndex;
        } );
        if ( it->isEmpty() )
            shard.byKey.erase( it );
    }
}

void CatalogRecordStore::rebuildIndex( RecordShard &shard )
{
    shard.byKey.clear();
    for ( int i = 0; i < static_cast<int>( shard.records.size() ); ++i )
        insertKeys( shard, i, shard.keys.value( i ) );
}

AssetRecord *CatalogRecordStore::findMutable( AssetId id )
{
    int shard = -1;
    int rec = -1;
    if ( !locate( id, shard, rec ) )
        return nullptr;
    RecordShard &target = shard == sealedCount() ? tailForWrite() : shardForWrite( shard );
    return &target.records[ rec ];
}

void CatalogRecordStore::resyncKeys( AssetId id )
{
    int shard = -1;
    int rec = -1;
    if ( !locate( id, shard, rec ) )
        return;
    RecordShard &target = shard == sealedCount() ? tailForWrite() : shardForWrite( shard );
    removeKeys( target, rec );
    target.keys[ rec ] = recordKeys( target.records[ rec ] );
    insertKeys( target, rec, target.keys[ rec ] );
}

void CatalogRecordStore::append( AssetRecord record )
{
    if ( m_tail && static_cast<int>( m_tail->records.size() ) >= kShardRecords )
        sealTail();

    RecordShard &tail = tailForWrite();
    const QStringList keys = recordKeys( record );
    const int index = static_cast<int>( tail.records.size() );
    const AssetId id = record.snapshot.id();
    const QString identity = sourceKeyOf( record.snapshot.source() );
    tail.records.append( std::move( record ) );
    tail.keys.append( keys );
    insertKeys( tail, index, keys );
    m_bySourceKey.insert( identity, id );
    ++m_size;
}

bool CatalogRecordStore::erase( AssetId id )
{
    int shard = -1;
    int rec = -1;
    if ( !locate( id, shard, rec ) )
        return false;

    RecordShard &target = shard == sealedCount() ? tailForWrite() : shardForWrite( shard );
    m_bySourceKey.remove( sourceKeyOf( target.records[ rec ].snapshot.source() ) );
    target.records.remove( rec );
    target.keys.remove( rec );
    // Positions shifted: rebuild the shard's index from its (unchanged)
    // per-record key lists rather than patching stale indices.
    rebuildIndex( target );
    --m_size;
    return true;
}

} // namespace sicnu::data::internal
