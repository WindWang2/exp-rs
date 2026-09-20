// catalog_record_store.h — immutable chunked catalog record store + path index.
//
// Replaces the flat QVector<AssetRecord> the DataManager used to copy wholesale
// into every published snapshot. That copy-on-write vector made population
// O(N^2): each publication aliased the live vector, so the next mutation
// detached and deep-copied every record (see
// benchmarks/observatory/obs_dataset_register_scaling.json, exponent ~2.25).
//
// The store keeps the same logical contents and order as the old vector while
// making publication O(1):
//
//   * records live in fixed-capacity immutable SHARDS. The live tail shard is
//     the only mutable one; when it fills it is sealed (moved, not copied)
//     into the sealed shard list;
//   * a published snapshot aliases the sealed list and the tail by
//     std::shared_ptr — two refcount bumps, no record is touched;
//   * a mutation first makes its target shard private (copy-on-write of ONE
//     shard, bounded by kShardRecords), so already-published snapshots stay
//     immutable. The copy is counted in CatalogScaleCounters so the scale
//     oracles can prove the per-mutation cost is bounded by a constant;
//   * each shard carries a path→record KEY INDEX (identity spellings computed
//     once per mutation) so findByPath never re-aliases or re-canonicalizes
//     the whole catalog per probe.
//
// THREADING: mutations and publication run strictly on the DataManager's
// owning thread (the manager's thread-affinity contract, #703/#852). Readers
// only ever touch published snapshots, whose shards are immutable. The
// private/shared flags below are therefore owner-thread-only state.
#pragma once

#include "../data_asset.h"
#include "../derivation_record.h"
#include "../source_descriptor.h"
#include "../virtual_raster_recipe.h"

#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <optional>

namespace sicnu::data::internal
{

/// One catalog record: identity + published snapshot + optional provenance and
/// virtual-raster recipe. Value type; every member is cheap to copy
/// (implicitly shared Qt containers), which is what keeps the per-mutation
/// shard copy bounded in practice.
struct AssetRecord
{
    SourceKey sourceKey;
    AssetSnapshot snapshot;
    /// Provenance attached by a transactional algorithm-output commit; absent
    /// for assets that were registered directly.
    std::optional<DerivationRecord> derivation;
    /// The recipe a Virtual Raster Asset was created from; absent for
    /// non-virtual assets. The recipe is the identity; the generated `.vrt`
    /// at the snapshot's canonicalSource is a disposable artifact.
    std::optional<VirtualRasterRecipe> virtualRecipe;
};

/// Structural instrumentation for the data-scale oracles (WP1/WP2). Every
/// counter records WORK PERFORMED — copies, seals, filesystem resolutions,
/// hash lookups — never a duration, so the gates stay machine-independent.
/// Process-wide by design: the oracles reset() before a measured phase and
/// drive one manager per phase.
struct CatalogScaleCounters
{
    /// AssetRecord copies performed while making a shard private (bounded by
    /// the shard capacity per mutation).
    std::atomic<quint64> recordCopies{ 0 };
    /// Shard copies (sealed-shard COW) — a superset measure of recordCopies.
    std::atomic<quint64> shardCopies{ 0 };
    /// Tail → sealed-shard transitions.
    std::atomic<quint64> shardSeals{ 0 };
    /// publishSnapshot() calls (publication cost itself is O(1)).
    std::atomic<quint64> snapshotPublications{ 0 };
    /// Filesystem canonical-path resolutions: one per record key refresh and
    /// one per probe query. Never per (probe × record).
    std::atomic<quint64> pathCanonicalizations{ 0 };
    /// Hash lookups served to path probes (shard walk).
    std::atomic<quint64> pathIndexLookups{ 0 };
    /// Records examined by id lookups / full scans.
    std::atomic<quint64> recordVisits{ 0 };

    void reset()
    {
        recordCopies = 0;
        shardCopies = 0;
        shardSeals = 0;
        snapshotPublications = 0;
        pathCanonicalizations = 0;
        pathIndexLookups = 0;
        recordVisits = 0;
    }
};

/// Process-wide counter accessor (tests only; production code just increments).
CatalogScaleCounters &catalogScaleCounters();

/// True for schemes whose identity is string-only: QFileInfo mangles them
/// (empty canonicalFilePath; absolute prepends cwd or a drive letter), so the
/// legacy contract resolves remote/VSI identity by string + alias alone.
bool isVirtualOrRemotePath( const QString &path );

/// Additive aliases so a raw STAC https href and the provider's /vsicurl/
/// spelling resolve to the same catalog record. Local files are unchanged
/// (their only alias is the path itself).
QStringList virtualPathAliases( const QString &path );

/// One ownership entry in a shard's key index: the index of the owning record
/// inside the same shard. Indices (not pointers) survive the COW copies, and
/// because records are only ever appended and removed — never reordered — a
/// lower index always means an earlier insertion, which is what makes "first
/// match in insertion order wins" decidable without a sequence number.
struct PathKeyEntry
{
    int recordIndex = 0;
};

/// Immutable chunk of records plus its key index. Never mutated once a
/// snapshot may alias it: mutations replace the shared_ptr with a COW copy.
struct RecordShard
{
    QVector<AssetRecord> records;
    /// Identity keys per record, parallel to `records` (tier-prefixed; see
    /// CatalogRecordStore::append for the tiers).
    QVector<QStringList> keys;
    /// tier-prefixed key → owning entries, each list ordered by insertion seq.
    QHash<QString, QVector<PathKeyEntry>> byKey;
};

/// The catalog's authoritative record container (WP1 + WP2).
///
/// Ordering contract: iteration order is insertion order, exactly like the
/// flat vector it replaces (sealed shards oldest→newest, then the tail).
class CatalogRecordStore
{
  public:
    /// Records per shard. Bounds the per-mutation copy cost (a mutation makes
    /// at most one shard private) and the per-probe shard walk.
    static constexpr int kShardRecords = 256;

    CatalogRecordStore() = default;

    /// Immutable view handed to readers through CatalogSnapshot. Aliases the
    /// sealed list and the tail shard by shared_ptr; both are immutable for
    /// as long as any view exists.
    struct SnapshotView
    {
        std::shared_ptr<const QVector<std::shared_ptr<const RecordShard>>> sealed;
        std::shared_ptr<const RecordShard> tail;
        quint64 recordCount = 0;

        quint64 size() const { return recordCount; }

        /// First record (insertion order) whose id is @p id, or nullptr.
        const AssetRecord *find( AssetId id ) const;

        /// Read-only iteration in insertion order.
        template <typename F>
        void forEach( F &&f ) const
        {
            if ( sealed )
            {
                for ( const auto &shard : *sealed )
                {
                    for ( const AssetRecord &record : shard->records )
                    {
                        catalogScaleCounters().recordVisits.fetch_add( 1, std::memory_order_relaxed );
                        f( record );
                    }
                }
            }
            if ( tail )
            {
                for ( const AssetRecord &record : tail->records )
                {
                    catalogScaleCounters().recordVisits.fetch_add( 1, std::memory_order_relaxed );
                    f( record );
                }
            }
        }

        /// Path lookup over the key index (WP2). Semantics are identical to
        /// the legacy linear scan — alias/string identity first, then
        /// raw/canonical/absolute path tiers for non-virtual spellings on both
        /// sides — but a probe resolves at most the QUERY path and performs
        /// zero per-record filesystem work.
        const AssetRecord *probe( const QString &path ) const;
    };

    // --- snapshot side -------------------------------------------------------

    /// O(1): aliases the sealed list and the tail shard.
    SnapshotView captureView() const
    {
        SnapshotView view;
        view.sealed = m_sealed;
        view.tail = m_tail;
        view.recordCount = m_size;
        return view;
    }

    /// Called by the publisher right after a snapshot captured this state:
    /// everything the snapshot aliases is now shared, so the next mutation
    /// must copy before writing.
    void markSharedWithSnapshot()
    {
        m_sealedShared = true;
        m_tailShared = true;
        m_privateSealed = -1;
    }

    // --- live side (owner thread only) ---------------------------------------

    quint64 size() const { return m_size; }

    /// Read-only id lookup (no copy). Owner-thread reads only.
    const AssetRecord *find( AssetId id ) const;

    /// The AssetId already registered under the identity of @p descriptor, if
    /// any. O(1) through the live-side source-identity index — the
    /// registration/restore/relocate conflict scans this replaces were O(N)
    /// per call, which made a 100k population pay ~5e9 key comparisons.
    std::optional<AssetId> sourceKeyOwner( const SourceDescriptor &descriptor ) const;

    /// Mutable access to one record. Copies the owning shard first when a
    /// published snapshot still aliases it. When the caller changes the
    /// record's canonical source it MUST call resyncKeys() afterwards.
    /// The returned pointer stays valid until the next erase() that removes an
    /// earlier record of the same shard (which shifts positions) — callers
    /// must re-locate rather than cache it across such a mutation.
    AssetRecord *findMutable( AssetId id );

    /// Recomputes the identity keys of a record whose snapshot was mutated in
    /// place (relocate). No-op when the path tiers are unchanged.
    void resyncKeys( AssetId id );

    /// Swaps a record's source-identity index entry after its descriptor was
    /// replaced (relocate). MUST be called BEFORE the record's snapshot is
    /// replaced: it derives the previous identity from the record itself.
    void reindexSourceKey( AssetId id, const SourceDescriptor &newDescriptor );

    /// Appends a record (identity keys computed once here).
    void append( AssetRecord record );

    /// Removes the record with @p id (index entries included). Returns false
    /// when the id is unknown.
    bool erase( AssetId id );

    /// Iteration in insertion order (read-only; no copy).
    template <typename F>
    void forEach( F &&f ) const
    {
        SnapshotView{ m_sealed, m_tail, m_size }.forEach( f );
    }

  private:
    using SealedShards = QVector<std::shared_ptr<const RecordShard>>;

    static constexpr const char *kAliasTier = "a";  ///< alias/string identity
    static constexpr const char *kPathTier = "p";   ///< filesystem path identity

    int sealedCount() const { return m_sealed ? static_cast<int>( m_sealed->size() ) : 0; }

    /// Locates @p id. `shard == sealedCount()` means the tail shard.
    bool locate( AssetId id, int &shardOut, int &recOut ) const;

    /// Private (copy-on-write) access to the sealed-shard list.
    SealedShards &sealedForWrite();
    /// Private access to sealed shard @p i (COW copy when still shared).
    RecordShard &shardForWrite( int i );
    /// Private access to the tail shard (COW copy when still shared).
    RecordShard &tailForWrite();

    /// Seals a full tail into the sealed list (move, not copy).
    void sealTail();

    /// Identity keys of @p record (tier-prefixed, deduplicated).
    static QStringList recordKeys( const AssetRecord &record );
    /// Query keys of @p path (tier-prefixed, deduplicated).
    static QStringList queryKeys( const QString &path, bool &virtualOut );
    /// Faithful serialization of the SourceKey fields of @p descriptor
    /// (authConfigId excluded — SourceKey ignores it).
    static QString sourceKeyOf( const SourceDescriptor &descriptor );

    /// Inserts/removes one record's keys in @p shard. `recordIndex` is the
    /// record's position inside the shard.
    static void insertKeys( RecordShard &shard, int recordIndex, const QStringList &keys );
    static void removeKeys( RecordShard &shard, int recordIndex );
    /// Recomputes the whole shard index from its records (used after an erase
    /// shifts positions, and after a key refresh).
    static void rebuildIndex( RecordShard &shard );

    std::shared_ptr<const SealedShards> m_sealed;  ///< aliased by snapshots
    std::shared_ptr<const RecordShard> m_tail;     ///< aliased by snapshots
    bool m_sealedShared = true;                    ///< m_sealed needs a COW copy
    bool m_tailShared = true;                      ///< m_tail needs a COW copy
    int m_privateSealed = -1;                      ///< sealed index already private
    quint64 m_size = 0;
    /// Live-side (owner-thread only) source-identity index: serialized
    /// SourceKey → owning AssetId. Consulted only by owner-affine mutation
    /// paths (registration/restore/relocate conflict scans), so it needs no
    /// copy-on-write and is never published in a snapshot. The serialization
    /// is derived from the SAME descriptor the record's SourceKey was built
    /// from, so serialized equality implies SourceKey equality.
    QHash<QString, AssetId> m_bySourceKey;
};

} // namespace sicnu::data::internal
