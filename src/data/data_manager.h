#pragma once

#include <memory>
#include <optional>

#include <QObject>
#include <QVector>

#include "collection_types.h"
#include "data_asset.h"
#include "data_result.h"
#include "derivation_record.h"
#include "virtual_raster_recipe.h"

namespace sicnu::data
{

namespace internal
{
class SourceProviderRegistry;
class NetworkProbe;
struct AssetLeaseControl;
}

} // namespace sicnu::data

// Forward declaration: ProjectContext (in src/app, a SIBLING of sicnu::data) is
// the host that injects the NetworkProbe; it is friended inside DataManager for
// the private probe-accepting ctor (#66 host-injection seam).
namespace sicnu::app
{
class ProjectContext;
}

namespace sicnu::data
{

class DataManager;

class AssetLease
{
  public:
    AssetLease() = default;

    AssetLease( const AssetLease & ) = delete;
    AssetLease &operator=( const AssetLease & ) = delete;

    AssetLease( AssetLease &&other ) noexcept;
    AssetLease &operator=( AssetLease &&other ) noexcept;
    ~AssetLease();

    bool isValid() const;
    const AssetId &assetId() const;
    quint64 token() const;
    LeaseKind kind() const;
    const QString &purpose() const;
    LeaseRef toRef() const;

    /// Releases the lease explicitly. Returns Released on success, Invalid when
    /// already released or detached. After this call isValid() is false.
    LeaseOutcome release();

  private:
    friend class DataManager;

    explicit AssetLease( std::shared_ptr<internal::AssetLeaseControl> control );

    std::shared_ptr<internal::AssetLeaseControl> m_control;
};

class DataManager : public QObject
{
    Q_OBJECT

  public:
    explicit DataManager( QObject *parent = nullptr );
    ~DataManager() override;

    RegisterResult registerSource( const RegisterRequest &request );
    Result<AssetId> restoreSource( const RestoreRequest &request );
    Result<RelocateResult> relocate( const RelocateRequest &request );
    std::optional<AssetSnapshot> asset( AssetId id ) const;
    QVector<AssetSnapshot> assets( const AssetQuery &query = {} ) const;

    /// Structured provenance attached to an asset, if any. Algorithm-produced
    /// assets carry a Derivation Record; directly-registered assets do not.
    std::optional<DerivationRecord> provenance( AssetId id ) const;

    quint64 catalogGeneration() const;

    Result<AssetLease> acquire( const AssetRef &asset, const AssetUse &use );
    int leaseCount( AssetId id ) const;
    QVector<LeaseRef> leases( AssetId id ) const;
    /// True while an active Edit Lease exists for the asset (some view is editing).
    bool hasActiveEditLease( AssetId id ) const;

    /// Commits the active Edit Lease for a Vector Asset: advances the asset
    /// revision, releases the Edit Lease, and emits one assetChanged so other
    /// Display Layers refresh. Fails if no active Edit Lease exists.
    Result<void> commitEdit( AssetId id );
    /// Rolls back the active Edit Lease without advancing the revision. The Edit
    /// Lease is released and no change event is emitted.
    Result<void> rollbackEdit( AssetId id );

  UnloadPlan planUnload( AssetId id ) const;
  Result<void> unload( const UnloadPlan &confirmedPlan );

  /// Records a strong dependency edge: `dependent` consumes `input` (e.g. a
  /// Virtual Raster Asset depends on its input bands). Strong dependencies
  /// form a directed acyclic graph — adding an edge that would close a cycle
  /// (direct, transitive, or self) fails with `dependency.cycle` and mutates
  /// nothing. Both assets must be registered; a duplicate edge is a successful
  /// no-op. Normal unload of `input` is refused while the edge exists.
  Result<void> addStrongDependency( AssetId dependent, AssetId input );

  /// The inputs `id` depends on (outgoing edges), in edge-insertion order.
  QVector<AssetId> strongDependenciesOf( AssetId id ) const;

  /// The assets that depend on `id` (incoming edges), in edge-insertion
  /// order. Normal unload of `id` is refused while this list is non-empty.
  QVector<AssetId> strongDependentsOf( AssetId id ) const;

  /// Creates a Virtual Raster Asset from `recipe`. Runs the preflight against
  /// the input snapshots and refuses hard-failure verdicts (registering
  /// nothing); registers the composition through the normal pipeline (dedup
  /// by recipe - a same-recipe creation reuses the existing virtual asset);
  /// records one strong-dependency edge per distinct input (rolling back on a
  /// cycle); and stores the recipe. The provider generates the `.vrt`
  /// artifact in a managed scratch location - the recipe, not the file, is
  /// the identity. Cross-CRS recipes (RequiresReprojection) are refused in
  /// this wave with `virtual_raster.reprojection_unsupported` (warped VRT is
  /// a follow-up).
  Result<AssetId> createVirtualRaster(
    const VirtualRasterRecipe &recipe,
    PersistencePolicy persistence = PersistencePolicy::ProjectPersistent );

  /// The recipe a Virtual Raster Asset was created from, if any.
  std::optional<VirtualRasterRecipe> virtualRasterRecipe( AssetId id ) const;

  /// Restores a Virtual Raster Asset from a persisted recipe, preserving the
  /// caller-supplied AssetId (mirroring `restoreSource`/`restoreCollection`).
  /// The recipe is identity: the scratch `.vrt` is regenerated at the
  /// deterministic recipe-hash path. A recipe whose inputs are all present
  /// resolves to Ready and records one strong-dependency edge per distinct
  /// input; an input that is not registered (the saved project dropped it) is
  /// tolerated: the asset is still recorded in a non-Ready state, the missing
  /// input's edge is skipped, and a Warning is surfaced. Conflicts on the id
  /// or the recipe SourceKey are refused like `restoreSource`.
  Result<AssetId> restoreVirtualRaster( const RestoreVirtualRasterRequest &request );

  /// Reaps a temporary Data Asset: removes it from the catalog and, when the
  /// asset declares `DeletableSource`, deletes its on-disk source file. A
  /// distinct operation from unload (unload never deletes source data).
  /// Refuses a `ProjectPersistent` asset, an asset holding an active lease,
  /// and an unknown asset. A file-deletion failure still unloads the catalog
  /// entry and reports a warning; the catalog never points at a deleted file.
  ReapResult reap( const ReapRequest &request );

  /// Promotes a `SessionTemporary` or `TaskTemporary` asset to
  /// `ProjectPersistent` so it survives the session and is serialized into the
  /// `.qgz`. A policy flip on the same file - identity, revision, source,
  /// structure, capabilities, and provenance are all unchanged; one
  /// `assetChanged` is emitted so observers refresh. Promoting an already-
  /// persistent asset is a successful no-op (no signal). Promoting an unknown
  /// id is rejected.
  Result<void> promote( AssetId id );

  /// Reaps every idle `SessionTemporary` asset in one sweep - the batch form
  /// of `reap()`. Leased session-temporaries are skipped and reported (not
  /// force-revoked); the host decides what to do. `TaskTemporary` and
  /// `ProjectPersistent` assets are never touched. Called by the host on
  /// session close.
  TemporaryReapResult reapSessionTemporaries();

  /// Reaps every idle `TaskTemporary` asset in one sweep - the task-scope
  /// counterpart of `reapSessionTemporaries()`. Leased task-temporaries are
  /// skipped and reported; `SessionTemporary` and `ProjectPersistent` assets
  /// are never touched. Called by the host when a task scope ends. Uses a
  /// `persistence == TaskTemporary` query rather than a per-task-id binding, so
  /// no asset record is widened with task ownership.
  TemporaryReapResult reapTaskTemporaries();

  /// Creates a Data Collection (organizational catalog node) with the given
  /// display name and product metadata. Emits `collectionAdded`.
  CollectionCreateResult createCollection( const CollectionCreateRequest &request );

  /// Restores a collection with a specific id (from project deserialization),
  /// mirroring restoreSource for assets. Emits `collectionAdded`.
  CollectionCreateResult restoreCollection( CollectionId id,
                                             const CollectionCreateRequest &request );

  /// Returns a snapshot of the collection, if it exists.
  std::optional<CollectionSnapshot> collection( CollectionId id ) const;

  /// Lists all collection ids.
  QVector<CollectionId> collections() const;

  /// Adds a registered asset as a child of a collection. The child's
  /// `parentCollectionId` is set. Fails if the collection or asset is unknown.
  Result<void> addChildToCollection( CollectionId collectionId, AssetId childAssetId );

  /// Unloads a collection. Without cascade, only the collection node is removed
  /// (children become standalone assets with no parent). With cascade, the
  /// collection and all its children are removed. Cascade unload is refused
  /// while any child holds an active lease (mirroring the asset lease-safety
  /// rule). Emits `collectionRemoved` (and `assetRemoved` per cascaded child).
  Result<void> unloadCollection( CollectionId id, bool cascade );

    /// Attaches a Derivation Record to an existing asset, the final step of a
    /// transactional algorithm-output commit performed outside this layer. The
    /// record's `outputAssetId` is stamped with `id` (its caller-supplied value
    /// is ignored) so provenance always agrees with the registered asset. Does
    /// not emit `assetChanged` — registration already emitted `assetAdded`.
    Result<void> attachDerivationRecord( AssetId id, const DerivationRecord &derivation );

  signals:
    void assetAdded( AssetId id );
    void assetChanged( AssetId id );
    void assetAboutToUnload( AssetId id );
    void assetRemoved( AssetId id );
    void collectionAdded( CollectionId id );
    void collectionRemoved( CollectionId id );

  private:
    friend class internal::SourceProviderRegistry;
    friend class AssetLease;
    // ProjectContext is the application's host for the DataManager; it injects
    // the host-side NetworkProbe (#66) through the private probe-accepting
    // constructor. This is a host-injection seam, not a public API widening.
    friend class ::sicnu::app::ProjectContext;

    explicit DataManager( std::unique_ptr<internal::SourceProviderRegistry> providers,
                          QObject *parent = nullptr );

    /// Constructs with the built-in providers, forwarding `probe` to the four
    /// remote-map providers (WMS/WMTS/TMS/XYZ) so the host can inject a real
    /// HTTP-backed NetworkProbe (which lives in src/app — src/data is
    /// network-free). A null probe falls back to NoNetworkProbe (assets
    /// register Offline). This is the host-injection seam for #66; it stays
    /// private/friend-gated so no public API widens.
    explicit DataManager( const internal::NetworkProbe *probe,
                          QObject *parent = nullptr );

    /// Registry seeded with the built-in local GDAL raster and OGR vector
    /// providers. Used by the public constructor so the application resolves real
    /// sources by default; tests inject their own registry to stay hermetic.
    static std::unique_ptr<internal::SourceProviderRegistry> defaultProviders();

    /// Same as defaultProviders(), but forwards `probe` to the four remote-map
    /// providers. A null probe (the default-providers() case) yields the
    /// NoNetworkProbe fallback — backward-compatible.
    static std::unique_ptr<internal::SourceProviderRegistry>
    defaultProviders( const internal::NetworkProbe *probe );

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    LeaseOutcome releaseLease( const LeaseRef &lease );
    void revokeLease( const LeaseRef &lease );

    /// Shared sweep body for `reapSessionTemporaries` / `reapTaskTemporaries`:
    /// reaps every idle temporary asset of `policy`, skipping and reporting
    /// leased ones.
    TemporaryReapResult reapTemporaries( PersistencePolicy policy );

    /// Removes `childId` from every collection's child list (eager pruning so
    /// the persisted lists never hold dead asset ids). Called on unload/reap.
    void pruneChildFromCollections( AssetId childId );

    /// Removes every strong-dependency edge touching `id` (either endpoint).
    /// Called whenever an asset leaves the catalog so the DAG never holds dead
    /// asset ids.
    void pruneDependencyEdgesOf( AssetId id );

    /// Re-binds the strong-dependency edges for a restored virtual raster: one
    /// edge per distinct input that is present in the catalog; a missing input
    /// appends a Warning to `diagnostics` and is skipped (not a drop). Shared
    /// by `restoreVirtualRaster` and its idempotent re-restore path.
    void restoreVirtualRasterEdges( const RestoreVirtualRasterRequest &request,
                                    QVector<Diagnostic> &diagnostics );
};

} // namespace sicnu::data
