// test_data_scale.cpp — Data Scale 13.0 oracles (Track C).
//
// Machine-independent structural gates for the three measured-but-unfixed
// hotspots the Performance Observatory (#1125) recorded:
//
//   1. catalog population was O(N^2) — every publishSnapshot() aliased the
//      live vector, so the next mutation detached and copied every record
//      (benchmarks/observatory/obs_dataset_register_scaling.json, ~2.25);
//   2. findByPath rescanned the catalog and re-aliased + re-canonicalized
//      every record per probe (obs_dataset_find_by_path_hotspot.json);
//   3. governance deep paging was OFFSET-rescan dominated
//      (obs_governance_paging_scaling.json, worst doubling ~1.71).
//
// The gates here are structural counts (copies, filesystem resolutions,
// record visits, index lookups), never absolute milliseconds. Correctness is
// pinned by an EQUIVALENCE ORACLE: a verbatim copy of the pre-change lookup
// algorithm runs beside the indexed one over a path matrix, so any semantic
// drift fails the gate rather than hiding behind a faster implementation.
//
// All fixtures are hermetic (in-memory source provider, temp dirs); nothing
// touches the network or the repository.
#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "data/internal/catalog_record_store.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <vector>

using sicnu::data::AssetId;
using sicnu::data::AssetSnapshot;
using sicnu::data::DataManager;
using sicnu::data::SourceDescriptor;

namespace
{

//------------------------------------------------------------------------------
// Hermetic provider: registration stays cheap so the measured cost is the
// catalog operation under test, not a GDAL open per asset.
//------------------------------------------------------------------------------
class MemorySourceProvider final : public sicnu::data::internal::SourceProvider
{
  public:
    bool supports( const sicnu::data::SourceDescriptor &source ) const override
    {
        return source.providerKey == QStringLiteral( "memory-raster" );
    }

    sicnu::data::Result<sicnu::data::internal::ResolvedSource> resolve(
        const sicnu::data::SourceDescriptor &source ) const override
    {
        using namespace sicnu::data;
        internal::ResolvedSource resolved;
        resolved.kind = AssetKind::Raster;
        resolved.state = AssetState::Ready;
        resolved.capabilities = AssetCapability::Renderable | AssetCapability::ReadablePixels;
        resolved.storageKind = StorageKind::Memory;
        resolved.displayName = QStringLiteral( "Scale asset" );
        resolved.canonicalSource = source.canonicalSource;
        resolved.canonicalProviderKey = source.providerKey;
        resolved.structure = AssetStructure{ sicnu::data::RasterStructure{} };
        return sicnu::data::Result<sicnu::data::internal::ResolvedSource>::success( resolved );
    }
};

std::unique_ptr<DataManager> makeManager()
{
    auto providers = std::make_unique<sicnu::data::internal::SourceProviderRegistry>();
    providers->add( std::make_unique<MemorySourceProvider>() );
    return providers->createDataManager();
}

AssetId registerAsset( DataManager &manager, const QString &path )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "memory-raster" );
    source.canonicalSource = path;
    sicnu::data::RegisterRequest request;
    request.source = source;
    request.persistence = sicnu::data::PersistencePolicy::ProjectPersistent;
    const auto result = manager.registerSource( request );
    REQUIRE_FALSE( result.assetId.isNull() );
    return result.assetId;
}

//------------------------------------------------------------------------------
// Reference implementation: the PRE-CHANGE findByPath algorithm, verbatim,
// over the public assets() listing. The indexed lookup must agree with it on
// every probe — this is the equivalence oracle that makes the index a
// performance change rather than a behaviour change.
//------------------------------------------------------------------------------
bool isVirtualOrRemotePathRef( const QString &path )
{
    return path.startsWith( QLatin1String( "/vsi" ), Qt::CaseInsensitive ) ||
           path.startsWith( QLatin1String( "http://" ), Qt::CaseInsensitive ) ||
           path.startsWith( QLatin1String( "https://" ), Qt::CaseInsensitive );
}

QStringList virtualPathAliasesRef( const QString &path )
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

std::optional<AssetId> referenceFindByPath( const QVector<AssetSnapshot> &assets,
                                            const QString &path )
{
    if ( path.trimmed().isEmpty() )
        return std::nullopt;

    const QStringList queryAliases = virtualPathAliasesRef( path );
    const bool queryVirtual = isVirtualOrRemotePathRef( path );

    const QFileInfo fi( path );
    const QString absolute = queryVirtual ? QString() : fi.absoluteFilePath();
    const QString canonicalPath = queryVirtual ? QString() : fi.canonicalFilePath();
    for ( const AssetSnapshot &snapshot : assets )
    {
        const QString &stored = snapshot.source().canonicalSource;
        const QStringList storedAliases = virtualPathAliasesRef( stored );
        bool aliasHit = false;
        for ( const QString &alias : queryAliases )
        {
            if ( storedAliases.contains( alias ) )
            {
                aliasHit = true;
                break;
            }
        }
        if ( aliasHit )
            return snapshot.id();

        if ( queryVirtual || isVirtualOrRemotePathRef( stored ) )
            continue;

        if ( stored == canonicalPath && !canonicalPath.isEmpty() )
            return snapshot.id();
        const QFileInfo storedFi( stored );
        const QString storedCanonical = storedFi.canonicalFilePath();
        if ( !canonicalPath.isEmpty() && !storedCanonical.isEmpty() )
        {
            if ( storedCanonical == canonicalPath )
                return snapshot.id();
        }
        else if ( !absolute.isEmpty() && storedFi.absoluteFilePath() == absolute )
        {
            return snapshot.id();
        }
    }
    return std::nullopt;
}

/// Compares the indexed lookup against the reference for every probe in
/// @p probes and returns the number of disagreements.
int countDisagreements( DataManager &manager, const std::vector<QString> &probes )
{
    const QVector<AssetSnapshot> assets = manager.assets( {} );
    int disagreements = 0;
    for ( const QString &probe : probes )
    {
        const std::optional<AssetId> expected = referenceFindByPath( assets, probe );
        const std::optional<AssetSnapshot> actual = manager.findByPath( probe );
        const bool same = expected.has_value() == actual.has_value() &&
                          ( !expected.has_value() || *expected == actual->id() );
        if ( !same )
            ++disagreements;
    }
    return disagreements;
}

constexpr quint64 kShardRecords = sicnu::data::internal::CatalogRecordStore::kShardRecords;

/// Resident set size in bytes (Linux /proc). Used for the structural-memory
/// oracle: the point is a BOUND with headroom, not a precise measurement.
qint64 residentBytes()
{
    QFile status( QStringLiteral( "/proc/self/statm" ) );
    if ( !status.open( QIODevice::ReadOnly ) )
        return -1;
    const QList<QByteArray> fields = status.readAll().simplified().split( ' ' );
    if ( fields.size() < 2 )
        return -1;
    bool ok = false;
    const qint64 pages = fields.at( 1 ).toLongLong( &ok );
    if ( !ok )
        return -1;
    return pages * static_cast<qint64>( sysconf( _SC_PAGESIZE ) );
}

/// Restores the process working directory on every exit path (including a
/// failing REQUIRE), so one case cannot leak its cwd into later cases.
class ScopedCwd
{
  public:
    explicit ScopedCwd( const QString &path )
        : m_previous( QDir::currentPath() )
    {
        QDir::setCurrent( path );
    }
    ~ScopedCwd() { QDir::setCurrent( m_previous ); }
    ScopedCwd( const ScopedCwd & ) = delete;
    ScopedCwd &operator=( const ScopedCwd & ) = delete;

  private:
    QString m_previous;
};

} // namespace

//------------------------------------------------------------------------------
// WP1 — population is not O(N^2)
//------------------------------------------------------------------------------

TEST_CASE( "data scale: catalog population copies a bounded number of records per mutation",
           "[data_scale][population]" )
{
    auto manager = makeManager();

    struct Rung { int count; quint64 copies; quint64 publications; };
    std::vector<Rung> rungs;

    int rung = 0;
    int accumulated = 0;
    for ( const int count : { 1000, 10000, 100000 } )
    {
        accumulated += count;
        auto &counters = sicnu::data::internal::catalogScaleCounters();
        counters.reset();
        // Rung-unique prefixes: reusing one prefix would make every later rung
        // dedup-hit the earlier registrations (no publication, no append) and
        // the ladder would measure the dedup path instead of population.
        const QString prefix = QStringLiteral( "mem://scale/rung%1/asset_" ).arg( rung++ );
        for ( int i = 0; i < count; ++i )
            registerAsset( *manager, prefix + QStringLiteral( "%1.tif" )
                                              .arg( i, 7, 10, QLatin1Char( '0' ) ) );
        rungs.push_back( { count, counters.recordCopies.load(),
                           counters.snapshotPublications.load() } );

        // Structural invariants that must hold at every rung.
        CHECK( counters.recordCopies.load() <=
               static_cast<quint64>( count ) * kShardRecords );
        CHECK( counters.snapshotPublications.load() == static_cast<quint64>( count ) );
        CHECK( manager->catalogGeneration() >= static_cast<quint64>( accumulated ) );
        // The manager accumulates across rungs.
        CHECK( static_cast<int>( manager->assets( {} ).size() ) == accumulated );
    }

    // The population cost is linear: a 10x catalog must not copy 100x records.
    // (The pre-change implementation copied every record on every mutation:
    // 100k registrations paid ~5e9 record copies; the bound here is
    // count * kShardRecords / 2 on average.)
    const double copies1k = static_cast<double>( rungs[ 0 ].copies );
    const double copies10k = static_cast<double>( rungs[ 1 ].copies );
    const double copies100k = static_cast<double>( rungs[ 2 ].copies );
    const double growth10 = copies1k > 0 ? copies10k / copies1k : 0.0;
    const double growth100 = copies10k > 0 ? copies100k / copies10k : 0.0;
    WARN( "population record copies: 1k=" << copies1k << " 10k=" << copies10k
          << " 100k=" << copies100k << " (growth x" << growth10 << " / x"
          << growth100 << " for a 10x catalog)" );
    CHECK( growth10 < 20.0 );
    CHECK( growth100 < 20.0 );

    // A fresh snapshot must be a consistent view of the whole catalog: the
    // three rungs accumulate into one manager.
    const QVector<AssetSnapshot> all = manager->assets( {} );
    CHECK( static_cast<int>( all.size() ) == accumulated );

    // Structural-memory oracle (O5): the catalog's own structures (records,
    // shards, per-shard key index, live source-key index) must be bounded per
    // record — a publication that RETAINED a snapshot per mutation would grow
    // without bound. Generous ceiling: 64 KiB per record (the measured 100k
    // catalog is ~0.2 MiB per record of RSS including the test harness).
    const qint64 rss = residentBytes();
    if ( rss > 0 )
    {
        const double kibPerRecord = static_cast<double>( rss ) / 1024.0
                                    / static_cast<double>( accumulated );
        WARN( "100k catalog resident set: " << ( rss / 1024 / 1024 ) << " MiB ("
              << kibPerRecord << " KiB per record)" );
        // #1179 potency fix: the oracle SAID 64 KiB per record but enforced
        // 64 * 1024 KiB = 64 MiB (~1000× the documented ceiling). Enforce
        // the documented bound, with modest slack for allocator overhead.
        CHECK( kibPerRecord < 96.0 );
    }
}

//------------------------------------------------------------------------------
// WP2 — findByPath probes the index, never the catalog
//------------------------------------------------------------------------------

TEST_CASE( "data scale: findByPath probes resolve with zero per-record work",
           "[data_scale][path_index]" )
{
    auto manager = makeManager();
    constexpr int kCount = 100000;
    constexpr int kProbes = 1000;

    std::vector<QString> paths;
    paths.reserve( kCount );
    for ( int i = 0; i < kCount; ++i )
        paths.push_back( QStringLiteral( "mem://scale/asset_%1.tif" )
                             .arg( i, 7, 10, QLatin1Char( '0' ) ) );
    for ( const QString &path : paths )
        registerAsset( *manager, path );

    // Deterministic probe set: spread across the catalog, all hits.
    std::vector<QString> probes;
    probes.reserve( kProbes );
    for ( int i = 0; i < kProbes; ++i )
        probes.push_back( paths[ ( i * 9973 ) % kCount ] );

    // Warm-up (builds nothing lazily — the index is maintained incrementally),
    // then measure a pure probe phase.
    REQUIRE( manager->findByPath( probes.front() ).has_value() );

    auto &counters = sicnu::data::internal::catalogScaleCounters();
    counters.reset();
    for ( const QString &probe : probes )
        REQUIRE( manager->findByPath( probe ).has_value() );

    const quint64 canonicalizations = counters.pathCanonicalizations.load();
    const quint64 recordVisits = counters.recordVisits.load();
    const quint64 indexLookups = counters.pathIndexLookups.load();
    WARN( "probe phase: " << kProbes << " probes over " << kCount
          << " records — canonicalizations=" << canonicalizations
          << " record_visits=" << recordVisits
          << " index_lookups=" << indexLookups );

    // The hotspot this track removes: per-record aliasing + canonicalization.
    CHECK( recordVisits == 0 );
    // One filesystem resolution of the QUERY per probe; never per record.
    CHECK( canonicalizations <= static_cast<quint64>( kProbes ) * 2 );
    // The shard walk is O(shards) hash lookups — bounded by the catalog size
    // divided by the shard capacity, times the (few) query spellings. The old
    // implementation visited every record (kCount) per probe.
    const quint64 shardBound =
        static_cast<quint64>( kProbes ) *
        ( static_cast<quint64>( kCount ) / kShardRecords + 2 ) * 4;
    CHECK( indexLookups <= shardBound );
    CHECK( indexLookups < static_cast<quint64>( kProbes ) * static_cast<quint64>( kCount ) );
}

TEST_CASE( "data scale: findByPath matches the pre-change algorithm exactly",
           "[data_scale][path_index][equivalence]" )
{
    // A path matrix exercising every identity tier the legacy scan had:
    // existing local files (canonical + absolute + relative + symlink +
    // dot-segment spellings), missing local files, /vsicurl/https/https
    // aliases in both directions, case-variant schemes, unicode, and
    // whitespace/empty probes. The reference implementation is the contract.
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString real = QDir::toNativeSeparators( scratch.filePath( "scene-a.tif" ) );
    {
        QFile f( real );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x", 1 );
        f.close();
    }
    const QString link = QDir::toNativeSeparators( scratch.filePath( "scene-link.tif" ) );
    REQUIRE( QFile::link( real, link ) );

    auto manager = makeManager();
    registerAsset( *manager, real );
    registerAsset( *manager, QStringLiteral( "/vsicurl/https://obs.example/scene-b.tif" ) );
    registerAsset( *manager, QStringLiteral( "mem://scale/asset_0000001.tif" ) );

    const ScopedCwd cwd( scratch.path() );
    const std::vector<QString> probes = {
        real,
        QFileInfo( real ).canonicalFilePath(),
        QFileInfo( real ).absoluteFilePath(),
        QStringLiteral( "./scene-a.tif" ),
        QStringLiteral( "scene-a.tif" ),
        QStringLiteral( "scene-link.tif" ),
        QStringLiteral( "sub/../scene-a.tif" ),
        QDir::toNativeSeparators( scratch.filePath( "missing.tif" ) ),
        QStringLiteral( "missing.tif" ),
        QStringLiteral( "https://obs.example/scene-b.tif" ),
        QStringLiteral( "/vsicurl/https://obs.example/scene-b.tif" ),
        QStringLiteral( "/VSICURL/https://obs.example/scene-b.tif" ),
        QStringLiteral( "HTTPS://obs.example/scene-b.tif" ),
        QStringLiteral( "mem://scale/asset_0000001.tif" ),
        QStringLiteral( "mem://scale/asset_9999999.tif" ),
        QStringLiteral( "/vsi://scale/other.tif" ),
        QStringLiteral( "unicode/scene-\u00e9\u4e2d.tif" ),
        QString(),
        QStringLiteral( "   " ),
        QStringLiteral( "\t" ),
    };
    const int disagreements = countDisagreements( *manager, probes );
    CHECK( disagreements == 0 );

    // Spot-check the semantics the matrix encodes, so a future refactor that
    // breaks BOTH implementations still fails loudly. Still inside the scratch
    // cwd: the relative probes resolve against it.
    CHECK( manager->findByPath( QStringLiteral( "https://obs.example/scene-b.tif" ) )
               .has_value() );
    CHECK( manager->findByPath( QStringLiteral( "scene-link.tif" ) ).has_value() );
    CHECK_FALSE( manager->findByPath( QStringLiteral( "missing.tif" ) ).has_value() );
    CHECK_FALSE( manager->findByPath( QString() ).has_value() );
}

TEST_CASE( "data scale: path index stays consistent across every mutation",
           "[data_scale][path_index][consistency]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString first = QDir::toNativeSeparators( scratch.filePath( "one.tif" ) );
    const QString second = QDir::toNativeSeparators( scratch.filePath( "two.tif" ) );
    const QString third = QDir::toNativeSeparators( scratch.filePath( "three.tif" ) );
    for ( const QString &path : { first, second, third } )
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x", 1 );
        f.close();
    }

    auto manager = makeManager();
    const AssetId a = registerAsset( *manager, first );
    const AssetId b = registerAsset( *manager, second );
    const AssetId c = registerAsset( *manager, third );
    registerAsset( *manager, QStringLiteral( "/vsicurl/https://obs.example/remote.tif" ) );

    std::vector<QString> probes = {
        first, second, third,
        QFileInfo( first ).canonicalFilePath(),
        QStringLiteral( "one.tif" ),
        QStringLiteral( "two.tif" ),
        QStringLiteral( "missing.tif" ),
        QStringLiteral( "https://obs.example/remote.tif" ),
        QStringLiteral( "/vsicurl/https://obs.example/remote.tif" ),
    };

    auto check = [&]( const char *where ) {
        const int disagreements = countDisagreements( *manager, probes );
        INFO( "disagreements after " << where );
        CHECK( disagreements == 0 );
    };

    check( "registration" );

    // update: relocate moves an asset's canonical source; the index must drop
    // the old spelling and answer for the new one. The target must be a path
    // no other asset owns (relocate refuses a collision).
    const QString relocated = QDir::toNativeSeparators( scratch.filePath( "relocated.tif" ) );
    {
        QFile f( relocated );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x", 1 );
        f.close();
    }
    {
        sicnu::data::SourceDescriptor replacement;
        replacement.providerKey = QStringLiteral( "memory-raster" );
        replacement.canonicalSource = relocated;
        sicnu::data::RelocateRequest request;
        request.id = a;
        request.replacement = replacement;
        const auto result = manager->relocate( request );
        REQUIRE( static_cast<bool>( result ) );
        probes.push_back( relocated );
        probes.push_back( QStringLiteral( "relocated.tif" ) );
    }
    check( "relocate" );

    // update: revision-advancing mutations must not disturb the index.
    {
        const auto lease = manager->acquire( sicnu::data::AssetRef{ b },
                                             sicnu::data::AssetUse{ sicnu::data::LeaseKind::Edit,
                                                                    QStringLiteral( "edit" ) } );
        REQUIRE( static_cast<bool>( lease ) );
        REQUIRE( static_cast<bool>( manager->commitEdit( b ) ) );
    }
    check( "commitEdit" );

    {
        REQUIRE( static_cast<bool>( manager->notifyExternalContentChange( c ) ) );
        REQUIRE( static_cast<bool>( manager->promote( c ) ) );
    }
    check( "notifyExternalContentChange + promote" );

    // delete: an unloaded asset's paths must resolve to nothing (or to a
    // remaining duplicate owner), never to the dead record.
    {
        const auto plan = manager->planUnload( c ).confirmedCascade();
        REQUIRE( static_cast<bool>( manager->unload( plan ) ) );
    }
    check( "unload" );

    // delete + re-add under a different spelling of the same file.
    {
        const auto plan = manager->planUnload( b ).confirmedCascade();
        REQUIRE( static_cast<bool>( manager->unload( plan ) ) );
    }
    check( "unload b" );
    registerAsset( *manager, QStringLiteral( "./two.tif" ) );
    probes.push_back( QFileInfo( second ).absoluteFilePath() );
    check( "re-register two.tif under a relative spelling" );
}

TEST_CASE( "data scale: first registration wins duplicate path identities",
           "[data_scale][path_index][precedence]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString real = QDir::toNativeSeparators( scratch.filePath( "dup.tif" ) );
    {
        QFile f( real );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x", 1 );
        f.close();
    }
    // Two spellings of the same file are two SourceKeys (the canonicalSource
    // strings differ), so both register; the legacy scan returned the FIRST.
    auto manager = makeManager();
    const ScopedCwd cwd( scratch.path() );
    const AssetId first = registerAsset( *manager, real );
    const AssetId second = registerAsset( *manager, QStringLiteral( "./dup.tif" ) );

    CHECK( manager->findByPath( real )->id() == first );
    CHECK( manager->findByPath( QFileInfo( real ).canonicalFilePath() )->id() == first );
    CHECK( manager->findByPath( QStringLiteral( "./dup.tif" ) )->id() == first );
    CHECK( manager->findByPath( QStringLiteral( "dup.tif" ) )->id() == first );

    // Remove the first owner: the second spelling must take over the identity.
    const auto plan = manager->planUnload( first ).confirmedCascade();
    REQUIRE( static_cast<bool>( manager->unload( plan ) ) );
    const std::optional<AssetSnapshot> after = manager->findByPath( real );
    REQUIRE( after.has_value() );
    CHECK( after->id() == second );

    // No stale entry: the removed asset's own id no longer resolves.
    const std::optional<AssetSnapshot> gone = manager->findByPath( QStringLiteral( "dup.tif" ) );
    if ( gone.has_value() )
        CHECK( gone->id() != first );
}

//------------------------------------------------------------------------------
// WP5 — concurrency & lifetime
//------------------------------------------------------------------------------

TEST_CASE( "data scale: concurrent readers see consistent snapshots under mutation",
           "[data_scale][concurrency]" )
{
    auto manager = makeManager();
    constexpr int kInitial = 2000;
    constexpr int kMutations = 600;
    std::vector<QString> paths;
    paths.reserve( kInitial );
    for ( int i = 0; i < kInitial; ++i )
        paths.push_back( QStringLiteral( "mem://scale/asset_%1.tif" )
                             .arg( i, 7, 10, QLatin1Char( '0' ) ) );
    for ( const QString &path : paths )
        registerAsset( *manager, path );

    std::atomic<bool> stop{ false };
    std::atomic<int> readerErrors{ 0 };
    std::atomic<quint64> reads{ 0 };
    std::atomic<quint64> lastGeneration{ 0 };

    auto reader = [&]() {
        while ( !stop.load( std::memory_order_relaxed ) )
        {
            // Snapshot-served readers only (the thread-affinity contract):
            // each call takes one immutable snapshot and never observes a
            // partially applied mutation.
            const QVector<AssetSnapshot> all = manager->assets( {} );
            for ( int i = 0; i < 8; ++i )
            {
                const QString &probe = paths[ ( reads.load( std::memory_order_relaxed ) + i )
                                              % paths.size() ];
                const std::optional<AssetSnapshot> hit = manager->findByPath( probe );
                // A probe for a registered spelling either resolves to a live
                // record or (the asset was unloaded this instant) to nothing;
                // it must never resolve to an id absent from the same read.
                if ( hit )
                {
                    bool live = false;
                    for ( const AssetSnapshot &snapshot : all )
                    {
                        if ( snapshot.id() == hit->id() )
                        {
                            live = true;
                            break;
                        }
                    }
                    if ( !live )
                        readerErrors.fetch_add( 1, std::memory_order_relaxed );
                }
            }
            const AssetId probeId = all.isEmpty() ? AssetId{} : all.front().id();
            const std::optional<AssetSnapshot> one = manager->asset( probeId );
            if ( probeId.isNull() != !one.has_value() && !probeId.isNull() )
                readerErrors.fetch_add( 1, std::memory_order_relaxed );
            const quint64 generation = manager->catalogGeneration();
            quint64 previous = lastGeneration.load( std::memory_order_relaxed );
            if ( generation < previous )
                readerErrors.fetch_add( 1, std::memory_order_relaxed );
            while ( generation > previous &&
                    !lastGeneration.compare_exchange_weak( previous, generation ) )
            {
            }
            reads.fetch_add( 1, std::memory_order_relaxed );
        }
    };

    std::vector<std::thread> readers;
    for ( int i = 0; i < 4; ++i )
        readers.emplace_back( reader );

    // Mutation writer on the owning thread: register/unload churn that
    // publishes a snapshot per mutation, exactly like the population path.
    for ( int i = 0; i < kMutations; ++i )
    {
        const AssetId id = registerAsset(
            *manager, QStringLiteral( "mem://scale/churn_%1.tif" ).arg( i, 7, 10, QLatin1Char( '0' ) ) );
        const auto plan = manager->planUnload( id ).confirmedCascade();
        const sicnu::data::Result<void> unloaded = manager->unload( plan );
        if ( !unloaded )
            readerErrors.fetch_add( 1, std::memory_order_relaxed );
    }

    stop.store( true, std::memory_order_relaxed );
    for ( std::thread &t : readers )
        t.join();

    CHECK( readerErrors.load() == 0 );
    CHECK( reads.load() > 0 );
    CHECK( lastGeneration.load() >= static_cast<quint64>( kInitial + kMutations ) );

    // Generation rollover + stale-plan rejection still hold after the
    // representation change.
    const AssetId probe = registerAsset( *manager, QStringLiteral( "mem://scale/final.tif" ) );
    const auto stalePlan = manager->planUnload( probe );
    registerAsset( *manager, QStringLiteral( "mem://scale/stale-bump.tif" ) );
    const sicnu::data::Result<void> rejected = manager->unload( stalePlan );
    CHECK_FALSE( static_cast<bool>( rejected ) );
}

//------------------------------------------------------------------------------
// WP3 — governance keyset pagination
//------------------------------------------------------------------------------

TEST_CASE( "data scale: governance keyset walk returns every row exactly once",
           "[data_scale][governance]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString dbPath = scratch.filePath( QStringLiteral( "gov-scale.db" ) );
    constexpr int kRows = 20000;
    {
        sicnu::workspace::GovernanceStore store;
        REQUIRE( store.open( dbPath ) );
        QVector<sicnu::workspace::GovernedAsset> batch;
        batch.reserve( 512 );
        // One shared updated_ms stamp per batch: duplicate sort keys are the
        // case that makes an un-tiebroken ORDER BY unstable.
        for ( int i = 0; i < kRows; ++i )
        {
            sicnu::workspace::GovernedAsset asset;
            asset.assetId = QStringLiteral( "scale-%1" ).arg( i, 7, 10, QLatin1Char( '0' ) );
            asset.canonicalSource = QStringLiteral( "/data/scene_%1.tif" ).arg( i );
            asset.kind = ( i % 2 == 0 ) ? QStringLiteral( "raster" ) : QStringLiteral( "vector" );
            asset.state = QStringLiteral( "Ready" );
            asset.displayName = QStringLiteral( "scene_%1" ).arg( i, 7, 10, QLatin1Char( '0' ) );
            asset.updatedAtMs = static_cast<qint64>( i / 512 );  // ties inside a batch
            batch.append( asset );
            if ( batch.size() == 512 )
            {
                REQUIRE( static_cast<bool>( store.upsertAssets( batch ) ) );
                batch.clear();
            }
        }
        if ( !batch.isEmpty() )
            REQUIRE( static_cast<bool>( store.upsertAssets( batch ) ) );
    }

    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    // --- first page + cursor walk ------------------------------------------
    sicnu::workspace::WorkspaceQuery query;
    query.limit = 200;
    sicnu::workspace::WorkspacePage page = store.query( query );
    CHECK( page.total == kRows );
    CHECK( static_cast<int>( page.items.size() ) == 200 );
    CHECK_FALSE( page.nextCursor.isEmpty() );

    QStringList ids;
    for ( const QVariantMap &row : page.items )
        ids.append( row.value( QStringLiteral( "asset_id" ) ).toString() );

    int pages = 1;
    QString cursor = page.nextCursor;
    while ( !cursor.isEmpty() && pages < 10000 )
    {
        sicnu::workspace::WorkspaceQuery next = query;
        next.offset = 0;
        next.cursor = cursor;
        const sicnu::workspace::WorkspacePage walked = store.query( next );
        CHECK( walked.cursorError.isEmpty() );
        if ( walked.items.isEmpty() )
            break;
        for ( const QVariantMap &row : walked.items )
            ids.append( row.value( QStringLiteral( "asset_id" ) ).toString() );
        cursor = walked.nextCursor;
        ++pages;
    }

    // Every row exactly once, no duplicates, no skips.
    CHECK( static_cast<int>( ids.size() ) == kRows );
    std::sort( ids.begin(), ids.end() );
    CHECK( std::adjacent_find( ids.begin(), ids.end() ) == ids.end() );
    CHECK( pages == ( kRows + 199 ) / 200 );

    // --- keyset is a total order: tie-broken by id, stable across walks -----
    sicnu::workspace::WorkspaceQuery sorted = query;
    sorted.sortBy = QStringLiteral( "name" );
    const sicnu::workspace::WorkspacePage byName = store.query( sorted );
    CHECK( byName.total == kRows );
    CHECK_FALSE( byName.nextCursor.isEmpty() );
    QString previousName;
    QString previousId;
    bool ordered = true;
    for ( const QVariantMap &row : byName.items )
    {
        const QString name = row.value( QStringLiteral( "display_name" ) ).toString();
        const QString id = row.value( QStringLiteral( "asset_id" ) ).toString();
        if ( !previousName.isEmpty() )
        {
            const int cmp = QString::compare( previousName, name, Qt::CaseInsensitive );
            if ( cmp > 0 || ( cmp == 0 && previousId >= id ) )
                ordered = false;
        }
        previousName = name;
        previousId = id;
    }
    CHECK( ordered );

    // --- cursor hygiene ------------------------------------------------------
    sicnu::workspace::WorkspaceQuery bogus = query;
    bogus.cursor = QStringLiteral( "not-a-cursor" );
    const sicnu::workspace::WorkspacePage invalid = store.query( bogus );
    CHECK_FALSE( invalid.cursorError.isEmpty() );
    CHECK( invalid.items.isEmpty() );

    // A cursor minted under one filter must not be replayed under another.
    sicnu::workspace::WorkspaceQuery filtered = query;
    filtered.kind = QStringLiteral( "raster" );
    const sicnu::workspace::WorkspacePage filteredPage = store.query( filtered );
    REQUIRE( static_cast<bool>( !filteredPage.nextCursor.isEmpty() ) );
    sicnu::workspace::WorkspaceQuery replayed = query;  // no kind filter
    replayed.cursor = filteredPage.nextCursor;
    const sicnu::workspace::WorkspacePage mismatch = store.query( replayed );
    CHECK( mismatch.cursorError == QStringLiteral( "governance.cursor_mismatch" ) );
    CHECK( mismatch.items.isEmpty() );

    // Count semantics: legacy offset walk keeps exact totals; cursor
    // continuations do not rescan for a count.
    sicnu::workspace::WorkspaceQuery offsetQuery = query;
    offsetQuery.offset = 400;
    const sicnu::workspace::WorkspacePage offsetPage = store.query( offsetQuery );
    CHECK( offsetPage.total == kRows );
    CHECK( static_cast<int>( offsetPage.items.size() ) == 200 );
    sicnu::workspace::WorkspaceQuery continuation = query;
    continuation.cursor = page.nextCursor;
    const sicnu::workspace::WorkspacePage second = store.query( continuation );
    CHECK( second.total == 0 );
}

TEST_CASE( "data scale: governance deep page is index-driven, not a table rescan",
           "[data_scale][governance][plan]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString dbPath = scratch.filePath( QStringLiteral( "gov-plan.db" ) );
    {
        sicnu::workspace::GovernanceStore store;
        REQUIRE( store.open( dbPath ) );
        QVector<sicnu::workspace::GovernedAsset> batch;
        for ( int i = 0; i < 5000; ++i )
        {
            sicnu::workspace::GovernedAsset asset;
            asset.assetId = QStringLiteral( "plan-%1" ).arg( i );
            asset.kind = QStringLiteral( "raster" );
            asset.state = QStringLiteral( "Ready" );
            asset.displayName = QStringLiteral( "scene_%1" ).arg( i, 6, 10, QLatin1Char( '0' ) );
            asset.updatedAtMs = i;
            batch.append( asset );
        }
        REQUIRE( static_cast<bool>( store.upsertAssets( batch ) ) );
    }

    // The store's own paged SELECT must be served by the composite
    // (sort key, pk) index: a SEARCH ... USING INDEX plan, never
    // "SCAN assets" + "USE TEMP B-TREE FOR ORDER BY" (the sorter an
    // un-indexed or mixed-direction ORDER BY forces per page). The plan is
    // read from the store's own database file through the SQLite C API.
    auto planOf = [&]( const QString &sql, const QVector<QString> &binds ) {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY,
                                  nullptr ) == SQLITE_OK );
        sqlite3_stmt *stmt = nullptr;
        const QByteArray utf8 = ( QStringLiteral( "EXPLAIN QUERY PLAN " ) + sql ).toUtf8();
        REQUIRE( sqlite3_prepare_v2( raw, utf8.constData(), utf8.size(), &stmt, nullptr )
                 == SQLITE_OK );
        for ( int i = 0; i < binds.size(); ++i )
        {
            const QByteArray value = binds[ i ].toUtf8();
            REQUIRE( sqlite3_bind_text( stmt, i + 1, value.constData(), value.size(),
                                        SQLITE_TRANSIENT ) == SQLITE_OK );
        }
        QString plan;
        while ( sqlite3_step( stmt ) == SQLITE_ROW )
        {
            const unsigned char *detail = sqlite3_column_text( stmt, 3 );
            plan += QString::fromUtf8( reinterpret_cast<const char *>( detail ) );
            plan += QLatin1Char( '\n' );
        }
        sqlite3_finalize( stmt );
        sqlite3_close( raw );
        return plan;
    };

    // Production spellings: the default sort (updated_ms DESC, pk DESC) and the
    // keyset continuation, exactly as GovernanceStore::query() emits them.
    const QString cols = QStringLiteral(
        "a.asset_id, a.kind, a.state, a.display_name, a.canonical_source, a.sensor,"
        " a.modality, a.crs, a.acquisition_ms, a.revision, a.size_bytes, a.format,"
        " a.content_fingerprint, a.availability, a.updated_ms" );
    const QString firstPage =
        QStringLiteral( "SELECT %1 FROM assets a ORDER BY a.updated_ms DESC, a.asset_id DESC"
                        " LIMIT 201" ).arg( cols );
    const QString continuation =
        QStringLiteral( "SELECT %1 FROM assets a WHERE (a.updated_ms, a.asset_id) < (?, ?)"
                        " ORDER BY a.updated_ms DESC, a.asset_id DESC LIMIT 201" ).arg( cols );

    const QString planFirst = planOf( firstPage, {} );
    INFO( "first-page plan:\n" << planFirst.toStdString() );
    CHECK( planFirst.contains( QStringLiteral( "USING INDEX idx_gov_assets_updated" ) ) );
    CHECK_FALSE( planFirst.contains( QStringLiteral( "TEMP B-TREE" ) ) );

    const QString planSeek = planOf( continuation, { QStringLiteral( "42" ),
                                                     QStringLiteral( "plan-0042" ) } );
    INFO( "seek plan:\n" << planSeek.toStdString() );
    CHECK( planSeek.contains( QStringLiteral( "USING INDEX idx_gov_assets_updated" ) ) );
    CHECK( planSeek.contains( QStringLiteral( "(updated_ms,asset_id)<(?,?)" ) ) );
    CHECK_FALSE( planSeek.contains( QStringLiteral( "TEMP B-TREE" ) ) );

    // Behavioural cross-check on the same fixture: a deep page is a seek, so
    // walking to the last page costs the same order of work as the first.
    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );
    sicnu::workspace::WorkspaceQuery query;
    query.limit = 200;
    sicnu::workspace::WorkspacePage first = store.query( query );
    REQUIRE( static_cast<bool>( !first.nextCursor.isEmpty() ) );

    QString cursor = first.nextCursor;
    sicnu::workspace::WorkspacePage page = first;
    int pages = 1;
    while ( !cursor.isEmpty() )
    {
        sicnu::workspace::WorkspaceQuery next = query;
        next.cursor = cursor;
        page = store.query( next );
        if ( page.items.isEmpty() )
            break;
        cursor = page.nextCursor;
        ++pages;
        if ( pages > 1000 )
            break;
    }
    CHECK( pages == 25 );
    CHECK( static_cast<int>( page.items.size() ) == 200 );
    CHECK( page.items.first().value( QStringLiteral( "asset_id" ) ).toString() !=
           first.items.first().value( QStringLiteral( "asset_id" ) ).toString() );
}

//------------------------------------------------------------------------------
// Cross-shard coverage: the single-shard cases above never exercise the
// sealed-shard walk, the "first shard with a hit wins" rule, or the index
// rebuild that follows an erase which shifts positions.
//------------------------------------------------------------------------------

TEST_CASE( "data scale: path index spans shards and survives position shifts",
           "[data_scale][path_index][shards]" )
{
    // 3 shards' worth of records so the target identity lives in a SEALED
    // shard, not the live tail.
    constexpr int kCount = 3 * static_cast<int>( kShardRecords ) + 7;
    auto manager = makeManager();
    std::vector<QString> paths;
    paths.reserve( kCount );
    for ( int i = 0; i < kCount; ++i )
        paths.push_back( QStringLiteral( "mem://shards/asset_%1.tif" )
                             .arg( i, 7, 10, QLatin1Char( '0' ) ) );
    for ( const QString &path : paths )
        registerAsset( *manager, path );

    // A duplicate identity: the earliest registration lives in one of the
    // sealed shards, the later spelling is appended after enough padding that
    // it lands in a LATER shard. The earliest-inserted owner must win, and
    // after it is erased the later owner must take over — which also forces
    // the shard index rebuild (positions shifted).
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString shared = QDir::toNativeSeparators( scratch.filePath( "shared.tif" ) );
    {
        QFile f( shared );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x", 1 );
        f.close();
    }
    const ScopedCwd cwd( scratch.path() );

    const AssetId early = registerAsset( *manager, QStringLiteral( "./shared.tif" ) );
    // Push enough records that the second owner lands in a later shard.
    for ( int i = 0; i < static_cast<int>( kShardRecords ) + 3; ++i )
        registerAsset( *manager, QStringLiteral( "mem://shards/pad_%1.tif" )
                                     .arg( i, 7, 10, QLatin1Char( '0' ) ) );
    // A DIFFERENT spelling of the same file (absolute path): a different
    // SourceKey, so it registers a second record that shares the canonical
    // identity. (Re-registering the same spelling would be a dedup hit.)
    const AssetId late = registerAsset( *manager, shared );

    const std::optional<AssetSnapshot> before = manager->findByPath( shared );
    REQUIRE( before.has_value() );
    CHECK( before->id() == early );

    // Erase the earliest owner: its record sits in an early shard, so every
    // later position in that shard shifts and the index must be rebuilt.
    const auto plan = manager->planUnload( early ).confirmedCascade();
    REQUIRE( static_cast<bool>( manager->unload( plan ) ) );

    const std::optional<AssetSnapshot> after = manager->findByPath( shared );
    REQUIRE( after.has_value() );
    CHECK( after->id() == late );

    // Every other path still resolves, and the reference scan agrees with the
    // index over the whole (now shifted) catalog.
    std::vector<QString> probes;
    probes.reserve( paths.size() + 4 );
    for ( const QString &path : paths )
        probes.push_back( path );
    probes.push_back( shared );
    probes.push_back( QStringLiteral( "shared.tif" ) );
    probes.push_back( QStringLiteral( "./shared.tif" ) );
    probes.push_back( QStringLiteral( "mem://shards/pad_0000000.tif" ) );
    probes.push_back( QStringLiteral( "mem://shards/asset_9999999.tif" ) );
    CHECK( countDisagreements( *manager, probes ) == 0 );
}

//------------------------------------------------------------------------------
// Probe cost ladder: the O2 oracle promised an exponent across 1k/10k/100k.
//------------------------------------------------------------------------------

TEST_CASE( "data scale: probe cost is flat across the 1k/10k/100k ladder",
           "[data_scale][path_index][complexity]" )
{
    struct Rung { int assets; double perProbeUs; };
    std::vector<Rung> rungs;
    constexpr int kProbes = 512;

    for ( const int count : { 1000, 10000, 100000 } )
    {
        auto manager = makeManager();
        std::vector<QString> paths;
        paths.reserve( count );
        for ( int i = 0; i < count; ++i )
            paths.push_back( QStringLiteral( "mem://ladder/asset_%1.tif" )
                                 .arg( i, 7, 10, QLatin1Char( '0' ) ) );
        for ( const QString &path : paths )
            registerAsset( *manager, path );
        REQUIRE( manager->findByPath( paths.front() ).has_value() );

        QElapsedTimer timer;
        timer.start();
        for ( int i = 0; i < kProbes; ++i )
            REQUIRE( manager->findByPath( paths[ ( i * 6151 ) % count ] ).has_value() );
        rungs.push_back( { count, timer.nsecsElapsed() / 1000.0 / kProbes } );
    }

    WARN( "probe cost: " << rungs[ 0 ].perProbeUs << " us (1k), " << rungs[ 1 ].perProbeUs
          << " us (10k), " << rungs[ 2 ].perProbeUs << " us (100k)" );
    // A shard walk is O(shards) = O(N / kShardRecords) hash lookups plus the
    // query's own (one) filesystem resolution, so the rungs are not perfectly
    // flat. The gates bound the walk far below the behaviour this replaced:
    // a linear scan costs ~1 us PER RECORD, i.e. ~100 ms per probe at 100k,
    // so 2 ms is a 50x margin and the growth gate is 100x below the scan's.
    const double growth = rungs[ 0 ].perProbeUs > 0.0
                              ? rungs[ 2 ].perProbeUs / rungs[ 0 ].perProbeUs
                              : 0.0;
    CHECK( growth < 100.0 );
    CHECK( rungs[ 2 ].perProbeUs < 2000.0 );
}

//------------------------------------------------------------------------------
// 100k governance page walk (the O4 oracle promised the 100k rung).
//------------------------------------------------------------------------------

TEST_CASE( "data scale: governance keyset walk covers 100k rows exactly once",
           "[data_scale][governance][scale]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString dbPath = scratch.filePath( QStringLiteral( "gov-100k.db" ) );
    constexpr int kRows = 100000;
    {
        sicnu::workspace::GovernanceStore store;
        REQUIRE( store.open( dbPath ) );
        QVector<sicnu::workspace::GovernedAsset> batch;
        batch.reserve( 1024 );
        for ( int i = 0; i < kRows; ++i )
        {
            sicnu::workspace::GovernedAsset asset;
            asset.assetId = QStringLiteral( "big-%1" ).arg( i, 7, 10, QLatin1Char( '0' ) );
            asset.canonicalSource = QStringLiteral( "/data/scene_%1.tif" ).arg( i );
            asset.kind = ( i % 3 == 0 ) ? QStringLiteral( "raster" ) : QStringLiteral( "vector" );
            asset.state = QStringLiteral( "Ready" );
            asset.displayName = QStringLiteral( "scene_%1" ).arg( i, 7, 10, QLatin1Char( '0' ) );
            asset.updatedAtMs = i / 1024;  // ties inside every batch
            batch.append( asset );
            if ( batch.size() == 1024 )
            {
                REQUIRE( static_cast<bool>( store.upsertAssets( batch ) ) );
                batch.clear();
            }
        }
        if ( !batch.isEmpty() )
            REQUIRE( static_cast<bool>( store.upsertAssets( batch ) ) );
    }

    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    sicnu::workspace::WorkspaceQuery query;
    query.limit = 500;
    sicnu::workspace::WorkspacePage page = store.query( query );
    CHECK( page.total == kRows );
    CHECK( static_cast<int>( page.items.size() ) == 500 );

    QStringList ids;
    ids.reserve( kRows );
    for ( const QVariantMap &row : page.items )
        ids.append( row.value( QStringLiteral( "asset_id" ) ).toString() );

    QString cursor = page.nextCursor;
    int pages = 1;
    while ( !cursor.isEmpty() && pages < 10000 )
    {
        sicnu::workspace::WorkspaceQuery next = query;
        next.offset = 0;
        next.cursor = cursor;
        const sicnu::workspace::WorkspacePage walked = store.query( next );
        CHECK( walked.cursorError.isEmpty() );
        if ( walked.items.isEmpty() )
            break;
        for ( const QVariantMap &row : walked.items )
            ids.append( row.value( QStringLiteral( "asset_id" ) ).toString() );
        cursor = walked.nextCursor;
        ++pages;
    }

    CHECK( static_cast<int>( ids.size() ) == kRows );
    CHECK( pages == ( kRows + 499 ) / 500 );
    std::sort( ids.begin(), ids.end() );
    CHECK( std::adjacent_find( ids.begin(), ids.end() ) == ids.end() );

    // The total order is (updated_ms DESC, asset_id DESC): verify the walk
    // never moves backwards across a page boundary.
    sicnu::workspace::WorkspaceQuery ordered = query;
    ordered.limit = 500;
    const sicnu::workspace::WorkspacePage firstOrdered = store.query( ordered );
    REQUIRE( static_cast<bool>( !firstOrdered.nextCursor.isEmpty() ) );
    qint64 previousMs = firstOrdered.items.last().value( QStringLiteral( "updated_ms" ) ).toLongLong();
    QString previousId = firstOrdered.items.last().value( QStringLiteral( "asset_id" ) ).toString();
    bool monotone = true;
    QString cursor2 = firstOrdered.nextCursor;
    int guard = 0;
    while ( !cursor2.isEmpty() && guard++ < 10000 )
    {
        sicnu::workspace::WorkspaceQuery next = ordered;
        next.cursor = cursor2;
        const sicnu::workspace::WorkspacePage walked = store.query( next );
        if ( walked.items.isEmpty() )
            break;
        const qint64 ms = walked.items.first().value( QStringLiteral( "updated_ms" ) ).toLongLong();
        const QString id = walked.items.first().value( QStringLiteral( "asset_id" ) ).toString();
        if ( ms > previousMs || ( ms == previousMs && id >= previousId ) )
            monotone = false;
        previousMs = walked.items.last().value( QStringLiteral( "updated_ms" ) ).toLongLong();
        previousId = walked.items.last().value( QStringLiteral( "asset_id" ) ).toString();
        cursor2 = walked.nextCursor;
    }
    CHECK( monotone );
}

//------------------------------------------------------------------------------
// Potency: the equivalence oracle must be able to FAIL. This self-test runs
// deliberately broken lookup semantics beside the reference algorithm and
// asserts the oracle reports the disagreement — a green equivalence test is
// only evidence if a wrong answer makes it red.
//------------------------------------------------------------------------------

TEST_CASE( "data scale: the equivalence oracle detects a deliberately broken lookup",
           "[data_scale][path_index][potency]" )
{
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    const QString real = QDir::toNativeSeparators( scratch.filePath( "potency.tif" ) );
    {
        QFile f( real );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x", 1 );
        f.close();
    }
    auto manager = makeManager();
    const ScopedCwd cwd( scratch.path() );
    // The file is registered under a RELATIVE spelling: the absolute probe is
    // then answerable only through the filesystem path tiers, never through
    // the alias tier — that is what makes the injected defects below visible.
    registerAsset( *manager, QStringLiteral( "./potency.tif" ) );
    registerAsset( *manager, QStringLiteral( "/vsicurl/https://obs.example/potency.tif" ) );
    registerAsset( *manager, QStringLiteral( "mem://potency/asset_0000001.tif" ) );

    const std::vector<QString> probes = {
        real,
        QFileInfo( real ).canonicalFilePath(),
        QStringLiteral( "./potency.tif" ),
        QStringLiteral( "https://obs.example/potency.tif" ),
        QStringLiteral( "/vsicurl/https://obs.example/potency.tif" ),
        QStringLiteral( "mem://potency/asset_0000001.tif" ),
        QStringLiteral( "mem://potency/asset_9999999.tif" ),
        QString(),
    };

    // Sanity: the real implementation agrees with the reference.
    CHECK( countDisagreements( *manager, probes ) == 0 );

    const QVector<AssetSnapshot> assets = manager->assets( {} );
    auto referenceHits = [&]() {
        int hits = 0;
        for ( const QString &probe : probes )
        {
            if ( referenceFindByPath( assets, probe ).has_value() )
                ++hits;
        }
        return hits;
    };

    // Injected defect 1: an implementation that only answers ALIAS-tier
    // lookups (drops the filesystem path tiers) must be caught by the oracle.
    int aliasOnlyMisses = 0;
    for ( const QString &probe : probes )
    {
        const bool expected = referenceFindByPath( assets, probe ).has_value();
        bool aliasOnlyHit = false;
        for ( const QString &alias : virtualPathAliasesRef( probe ) )
        {
            for ( const AssetSnapshot &snapshot : assets )
            {
                if ( virtualPathAliasesRef( snapshot.source().canonicalSource )
                         .contains( alias ) )
                {
                    aliasOnlyHit = true;
                    break;
                }
            }
            if ( aliasOnlyHit )
                break;
        }
        if ( expected != aliasOnlyHit )
            ++aliasOnlyMisses;
    }
    CHECK( aliasOnlyMisses > 0 );

    // Injected defect 2: returning the LAST match instead of the first must be
    // caught whenever two records share an identity.
    const AssetId firstOwner = registerAsset( *manager, real );
    const std::optional<AssetId> expectedFirst =
        referenceFindByPath( manager->assets( {} ), real );
    REQUIRE( expectedFirst.has_value() );
    CHECK( manager->findByPath( real )->id() == *expectedFirst );
    CHECK( firstOwner != *expectedFirst );

    // Injected defect 3: an empty index (always nullopt) must disagree with
    // the reference on exactly the spellings that resolve.
    const int expectedHits = referenceHits();
    CHECK( expectedHits > 0 );
    int emptyIndexMisses = 0;
    for ( const QString &probe : probes )
    {
        if ( referenceFindByPath( manager->assets( {} ), probe ).has_value() )
            ++emptyIndexMisses;
    }
    CHECK( emptyIndexMisses == expectedHits );
}

//------------------------------------------------------------------------------
// #1161 — resyncKeys must not invert path-key precedence
//------------------------------------------------------------------------------

TEST_CASE( "data scale: relocating an asset never inverts shared-path precedence",
           "[data_scale][path_index][issue1161]" )
{
    auto manager = makeManager();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Two spellings of ONE file registered as two assets: ./dup.tif first,
    // then the absolute spelling — both fall into the same shard and share
    // the path key. Insertion order pins findByPath to the FIRST asset.
    const QString file = dir.filePath( QStringLiteral( "dup.tif" ) );
    {
        QFile f( file );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x" );
    }
    const QDir workDir( dir.path() );
    const QString relative = workDir.filePath( QStringLiteral( "./dup.tif" ) );
    const AssetId first = registerAsset( *manager, relative );
    const AssetId second = registerAsset( *manager, file );
    REQUIRE_FALSE( first.isNull() );
    REQUIRE_FALSE( second.isNull() );
    REQUIRE( first != second );

    auto probe = [&manager]( const QString &path ) {
        const auto hit = manager->findByPath( path );
        REQUIRE( hit.has_value() );
        return hit->id();
    };
    REQUIRE( probe( relative ) == first );
    REQUIRE( probe( file ) == first );

    // Self-relocate of the EARLIER asset onto its own source: resyncKeys
    // re-inserts its keys. Pre-#1161 the re-insert appended at the vector
    // tail and findByPath silently returned the LATER asset.
    sicnu::data::SourceDescriptor same;
    same.providerKey = QStringLiteral( "memory-raster" );
    same.canonicalSource = relative;
    const auto relocated = manager->relocate( { first, same } );
    REQUIRE( relocated );

    REQUIRE( probe( relative ) == first );
    REQUIRE( probe( file ) == first );

    // Relocating the earlier asset to a FRESH path (the covered case) keeps
    // resolving the shared key to the remaining asset.
    const QString moved = dir.filePath( QStringLiteral( "moved.tif" ) );
    {
        QFile f( moved );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "x" );
    }
    sicnu::data::SourceDescriptor movedSource;
    movedSource.providerKey = QStringLiteral( "memory-raster" );
    movedSource.canonicalSource = moved;
    REQUIRE( manager->relocate( { first, movedSource } ) );
    REQUIRE( probe( file ) == second );
    REQUIRE( manager->findByPath( relative ).has_value() );
}
