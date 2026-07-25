#include "data_manager.h"

#include <algorithm>
#include <utility>

#include <QFile>
#include <QPointer>
#include <QThread>

#include "internal/source_provider_registry.h"
#include "providers/gdal_raster_source_provider.h"
#include "providers/ogr_vector_source_provider.h"

namespace sicnu::data
{

namespace
{

Diagnostic wrongThreadDiagnostic()
{
  return Diagnostic{ QStringLiteral( "data.wrong_thread" ),
                     QStringLiteral( "Data Manager mutations must run on its owning thread" ),
                     DiagnosticSeverity::Error };
}

/// Builds the diagnostics for refusing to remove a leased asset, shared by
/// unload() and reap(). `codePrefix` selects the per-operation code
/// ("unload" / "reap").
QVector<Diagnostic> leasedRefusalDiagnostics( const QString &codePrefix,
                                              const QVector<LeaseImpact> &impacts )
{
  Diagnostic block{ QStringLiteral( "%1.leased" ).arg( codePrefix ),
                    QStringLiteral( "The asset is referenced by %1 active lease(s)" )
                      .arg( impacts.size() ),
                    DiagnosticSeverity::Error };
  QVector<Diagnostic> diagnostics{ std::move( block ) };
  for ( const LeaseImpact &impact : impacts )
  {
    diagnostics.append(
      Diagnostic{ QStringLiteral( "%1.lease" ).arg( codePrefix ),
                  QStringLiteral( "Held by %1 lease" )
                    .arg( impact.lease.kind == LeaseKind::View
                            ? QStringLiteral( "a view" )
                            : impact.lease.kind == LeaseKind::Task
                                ? QStringLiteral( "a task" )
                                : QStringLiteral( "an edit" ) ),
                  DiagnosticSeverity::Info } );
  }
  return diagnostics;
}

} // namespace

namespace internal
{

struct AssetLeaseControl
{
  AssetId assetId;
  quint64 token = 0;
  LeaseKind kind = LeaseKind::View;
  QString purpose;
  QPointer<DataManager> manager;
  bool active = false;
};

} // namespace internal

struct DataManager::Impl
{
  struct AssetRecord
  {
    SourceKey sourceKey;
    AssetSnapshot snapshot;
    /// Provenance attached by a transactional algorithm-output commit; absent
    /// for assets that were registered directly.
    std::optional<DerivationRecord> derivation;
  };

  struct LeaseRecord
  {
    std::shared_ptr<internal::AssetLeaseControl> control;
  };

  explicit Impl( std::unique_ptr<internal::SourceProviderRegistry> sourceProviders )
    : providers( std::move( sourceProviders ) )
  {
  }

  std::unique_ptr<internal::SourceProviderRegistry> providers;
  QVector<AssetRecord> records;
  QVector<LeaseRecord> leases;
  quint64 catalogGeneration = 1;
  quint64 nextLeaseToken = 1;

  QVector<AssetRecord>::iterator findRecord( AssetId id )
  {
    return std::find_if(
      records.begin(), records.end(),
      [&]( const AssetRecord &record ) { return record.snapshot.id() == id; } );
  }

  QVector<AssetRecord>::const_iterator findRecord( AssetId id ) const
  {
    return std::find_if(
      records.begin(), records.end(),
      [&]( const AssetRecord &record ) { return record.snapshot.id() == id; } );
  }

  QVector<LeaseImpact> leaseImpacts( AssetId id ) const
  {
    QVector<LeaseImpact> impacts;
    for ( const LeaseRecord &lease : leases )
    {
      if ( lease.control->assetId == id )
      {
        impacts.append( LeaseImpact{ LeaseRef{ lease.control->assetId,
                                               lease.control->token,
                                               lease.control->kind },
                                     lease.control->purpose } );
      }
    }
    return impacts;
  }
};

std::unique_ptr<internal::SourceProviderRegistry> DataManager::defaultProviders()
{
  auto registry = std::make_unique<internal::SourceProviderRegistry>();
  registry->add( std::make_unique<providers::GdalRasterSourceProvider>() );
  registry->add( std::make_unique<providers::OgrVectorSourceProvider>() );
  return registry;
}

DataManager::DataManager( QObject *parent )
  : DataManager( defaultProviders(), parent )
{
}

DataManager::DataManager( std::unique_ptr<internal::SourceProviderRegistry> providers,
                          QObject *parent )
  : QObject( parent )
  , m_impl( std::make_unique<Impl>( std::move( providers ) ) )
{
}

DataManager::~DataManager() = default;

RegisterResult DataManager::registerSource( const RegisterRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return RegisterResult{ {}, false, { wrongThreadDiagnostic() } };

  // Resolve first so providers can normalize the canonical identity (e.g. GDAL
  // rewrites an ENVI `.hdr` sidecar to its paired binary data file, and follows
  // symlinks). Deduplication is then keyed on that normalized identity rather
  // than the caller's raw string.
  const Result<internal::ResolvedSource> resolved = m_impl->providers->resolve( request.source );
  if ( !resolved )
    return RegisterResult{ {}, false, resolved.diagnostics() };

  const internal::ResolvedSource &source = resolved.value();

  SourceDescriptor normalizedDescriptor = request.source;
  if ( !source.canonicalSource.isEmpty() )
    normalizedDescriptor.canonicalSource = source.canonicalSource;
  if ( !source.canonicalProviderKey.isEmpty() )
    normalizedDescriptor.providerKey = source.canonicalProviderKey;

  const SourceKey sourceKey = normalizedDescriptor.sourceKey();
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.sourceKey == sourceKey )
      return RegisterResult{ record.snapshot.id(), true, {} };
  }

  const AssetId id = AssetId::generate();
  const AssetCapabilities capabilities =
    source.capabilities | request.additionalCapabilities;
  AssetSnapshot snapshot{ id,
                          AssetRevision::initial(),
                          normalizedDescriptor,
                          source.kind,
                          source.state,
                          capabilities,
                          request.persistence,
                          source.storageKind,
                          source.displayName,
                          source.structure };
  m_impl->records.push_back( Impl::AssetRecord{ sourceKey, std::move( snapshot ) } );
  m_impl->catalogGeneration++;

  emit assetAdded( id );
  return RegisterResult{ id, false, resolved.diagnostics() };
}

Result<AssetId> DataManager::restoreSource( const RestoreRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return Result<AssetId>::failure( wrongThreadDiagnostic() );

  if ( request.id.isNull() )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "restore.invalid_asset_id" ),
                  QStringLiteral( "A persisted Data Asset requires a valid id" ),
                  DiagnosticSeverity::Error } );
  }

  const Result<internal::ResolvedSource> resolved =
    m_impl->providers->resolve( request.source );
  if ( !resolved )
    return Result<AssetId>::failure( resolved.diagnostics() );

  const internal::ResolvedSource &source = resolved.value();
  SourceDescriptor normalizedDescriptor = request.source;
  if ( !source.canonicalSource.isEmpty() )
    normalizedDescriptor.canonicalSource = source.canonicalSource;
  if ( !source.canonicalProviderKey.isEmpty() )
    normalizedDescriptor.providerKey = source.canonicalProviderKey;
  const SourceKey sourceKey = normalizedDescriptor.sourceKey();

  const auto existingId = m_impl->findRecord( request.id );
  if ( existingId != m_impl->records.end() )
  {
    if ( existingId->sourceKey == sourceKey )
      return Result<AssetId>::success( request.id, resolved.diagnostics() );
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "restore.asset_id_conflict" ),
                  QStringLiteral( "The persisted Asset ID is already bound to another source" ),
                  DiagnosticSeverity::Error } );
  }

  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.sourceKey == sourceKey )
    {
      return Result<AssetId>::failure(
        Diagnostic{ QStringLiteral( "restore.source_conflict" ),
                    QStringLiteral( "The persisted source is already bound to another Asset ID" ),
                    DiagnosticSeverity::Error } );
    }
  }

  const AssetRevision revision =
    request.revision.isValid() ? request.revision : AssetRevision::initial();
  AssetSnapshot snapshot{ request.id,
                          revision,
                          normalizedDescriptor,
                          source.kind,
                          source.state,
                          source.capabilities,
                          request.persistence,
                          source.storageKind,
                          source.displayName,
                          source.structure };
  m_impl->records.push_back(
    Impl::AssetRecord{ sourceKey, std::move( snapshot ) } );
  m_impl->catalogGeneration++;
  emit assetAdded( request.id );
  return Result<AssetId>::success( request.id, resolved.diagnostics() );
}

Result<RelocateResult> DataManager::relocate( const RelocateRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return Result<RelocateResult>::failure( wrongThreadDiagnostic() );

  const auto recordIt = m_impl->findRecord( request.id );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<RelocateResult>::failure(
      Diagnostic{ QStringLiteral( "relocate.unknown_asset" ),
                  QStringLiteral( "No registered asset matches the requested id" ),
                  DiagnosticSeverity::Error } );
  }

  const AssetSnapshot &current = recordIt->snapshot;

  // Resolve the replacement through the provider seam so its canonical identity
  // and structural metadata are computed the same way as a fresh registration.
  const Result<internal::ResolvedSource> resolved =
    m_impl->providers->resolve( request.replacement );
  if ( !resolved )
    return Result<RelocateResult>::failure( resolved.diagnostics() );

  const internal::ResolvedSource &replacement = resolved.value();

  // Relocation cannot change what the asset is. The replacement must be the
  // same kind of data.
  if ( replacement.kind != current.kind() )
  {
    return Result<RelocateResult>::failure(
      Diagnostic{ QStringLiteral( "relocate.kind_mismatch" ),
                  QStringLiteral( "The replacement source is a different kind of data" ),
                  DiagnosticSeverity::Error } );
  }

  // Validate the replacement's structure before mutating the catalog, so a
  // moved source cannot silently swap in an incompatible dataset under the
  // existing Asset ID.
  if ( !structuresCompatible( current.structure(), replacement.structure ) )
  {
    return Result<RelocateResult>::failure(
      Diagnostic{ QStringLiteral( "relocate.structure_mismatch" ),
                  QStringLiteral( "The replacement source has an incompatible structure" ),
                  DiagnosticSeverity::Error } );
  }

  SourceDescriptor normalizedDescriptor = request.replacement;
  if ( !replacement.canonicalSource.isEmpty() )
    normalizedDescriptor.canonicalSource = replacement.canonicalSource;
  if ( !replacement.canonicalProviderKey.isEmpty() )
    normalizedDescriptor.providerKey = replacement.canonicalProviderKey;
  const SourceKey newSourceKey = normalizedDescriptor.sourceKey();

  // The relocated source must not collide with another registered asset.
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.snapshot.id() != request.id && record.sourceKey == newSourceKey )
    {
      return Result<RelocateResult>::failure(
        Diagnostic{ QStringLiteral( "relocate.source_conflict" ),
                    QStringLiteral( "The replacement source is already bound to another Asset ID" ),
                    DiagnosticSeverity::Error } );
    }
  }

  // Recompute the SourceKey index in place — the same Asset ID is preserved.
  const AssetRevision newRevision = current.revision().next();
  AssetSnapshot updated{ current.id(),
                         newRevision,
                         normalizedDescriptor,
                         replacement.kind,
                         replacement.state,
                         replacement.capabilities,
                         current.persistence(),
                         replacement.storageKind,
                         replacement.displayName,
                         replacement.structure };
  recordIt->sourceKey = newSourceKey;
  recordIt->snapshot = std::move( updated );
  m_impl->catalogGeneration++;

  emit assetChanged( request.id );
  return Result<RelocateResult>::success(
    RelocateResult{ request.id, newRevision, resolved.diagnostics() } );
}

std::optional<AssetSnapshot> DataManager::asset( AssetId id ) const
{
  const auto it = m_impl->findRecord( id );
  if ( it == m_impl->records.end() )
    return std::nullopt;
  return it->snapshot;
}

QVector<AssetSnapshot> DataManager::assets( const AssetQuery &query ) const
{
  QVector<AssetSnapshot> snapshots;
  snapshots.reserve( m_impl->records.size() );
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( query.kind && record.snapshot.kind() != *query.kind )
      continue;
    if ( query.state && record.snapshot.state() != *query.state )
      continue;
    if ( query.persistence && record.snapshot.persistence() != *query.persistence )
      continue;

    snapshots.push_back( record.snapshot );
  }
  return snapshots;
}

quint64 DataManager::catalogGeneration() const
{
  return m_impl->catalogGeneration;
}

std::optional<DerivationRecord> DataManager::provenance( AssetId id ) const
{
  const auto it = m_impl->findRecord( id );
  if ( it == m_impl->records.end() )
    return std::nullopt;
  return it->derivation;
}

Result<void> DataManager::attachDerivationRecord( AssetId id,
                                                  const DerivationRecord &derivation )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto it = m_impl->findRecord( id );
  if ( it == m_impl->records.end() )
  {
    return Result<void>::failure(
      { QStringLiteral( "data.asset.unknown" ),
        QStringLiteral( "Cannot attach a Derivation Record to an unregistered asset" ) } );
  }

  DerivationRecord stamped = derivation;
  stamped.outputAssetId = id;
  it->derivation = stamped;
  return Result<void>::success();
}

Result<AssetLease> DataManager::acquire( const AssetRef &asset, const AssetUse &use )
{
  if ( QThread::currentThread() != thread() )
    return Result<AssetLease>::failure( wrongThreadDiagnostic() );

  const auto recordIt = m_impl->findRecord( asset.id );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<AssetLease>::failure(
      Diagnostic{ QStringLiteral( "asset.unknown" ),
                  QStringLiteral( "No registered asset matches the requested id" ),
                  DiagnosticSeverity::Error } );
  }

  if ( asset.expectedRevision.isValid() && recordIt->snapshot.revision() != asset.expectedRevision )
  {
    return Result<AssetLease>::failure(
      Diagnostic{ QStringLiteral( "asset.stale_revision" ),
                  QStringLiteral( "The asset revision no longer matches the expected revision" ),
                  DiagnosticSeverity::Error } );
  }

  // One Vector Asset may grant at most one Edit Lease at a time. View and Task
  // leases are unconstrained; editing is exclusive so that uncommitted QGIS
  // edit buffers never become a cross-view collaboration mechanism.
  if ( use.kind == LeaseKind::Edit )
  {
    for ( const Impl::LeaseRecord &lease : m_impl->leases )
    {
      if ( lease.control->assetId == asset.id && lease.control->active &&
           lease.control->kind == LeaseKind::Edit )
      {
        return Result<AssetLease>::failure(
          Diagnostic{ QStringLiteral( "asset.edit_lease_conflict" ),
                      QStringLiteral( "The asset is already being edited; commit or "
                                      "roll back the current edit session first" ),
                      DiagnosticSeverity::Error } );
      }
    }
  }

  const quint64 token = m_impl->nextLeaseToken++;
  auto control = std::make_shared<internal::AssetLeaseControl>(
    internal::AssetLeaseControl{ asset.id, token, use.kind, use.purpose, this, true } );
  m_impl->leases.push_back( Impl::LeaseRecord{ control } );
  m_impl->catalogGeneration++;

  return Result<AssetLease>::success( AssetLease{ std::move( control ) } );
}

Result<void> DataManager::commitEdit( AssetId id )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto recordIt = m_impl->findRecord( id );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "asset.unknown" ),
                  QStringLiteral( "No registered asset matches the requested id" ),
                  DiagnosticSeverity::Error } );
  }

  const auto leaseIt =
    std::find_if( m_impl->leases.begin(), m_impl->leases.end(),
                  [&]( const Impl::LeaseRecord &lease ) {
                    return lease.control->assetId == id && lease.control->active &&
                           lease.control->kind == LeaseKind::Edit;
                  } );
  if ( leaseIt == m_impl->leases.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "asset.no_edit_lease" ),
                  QStringLiteral( "The asset has no active Edit Lease to commit" ),
                  DiagnosticSeverity::Error } );
  }

  // A successful commit advances the Asset Revision and refreshes other Display
  // Layers via assetChanged.
  const AssetRevision newRevision = recordIt->snapshot.revision().next();
  recordIt->snapshot = AssetSnapshot{ recordIt->snapshot.id(),
                                      newRevision,
                                      recordIt->snapshot.source(),
                                      recordIt->snapshot.kind(),
                                      recordIt->snapshot.state(),
                                      recordIt->snapshot.capabilities(),
                                      recordIt->snapshot.persistence(),
                                      recordIt->snapshot.storageKind(),
                                      recordIt->snapshot.displayName(),
                                      recordIt->snapshot.structure() };

  ( *leaseIt ).control->active = false;
  ( *leaseIt ).control->manager.clear();
  m_impl->leases.erase( leaseIt );
  m_impl->catalogGeneration++;

  emit assetChanged( id );
  return Result<void>::success();
}

Result<void> DataManager::rollbackEdit( AssetId id )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto recordIt = m_impl->findRecord( id );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "asset.unknown" ),
                  QStringLiteral( "No registered asset matches the requested id" ),
                  DiagnosticSeverity::Error } );
  }

  const auto leaseIt =
    std::find_if( m_impl->leases.begin(), m_impl->leases.end(),
                  [&]( const Impl::LeaseRecord &lease ) {
                    return lease.control->assetId == id && lease.control->active &&
                           lease.control->kind == LeaseKind::Edit;
                  } );
  if ( leaseIt == m_impl->leases.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "asset.no_edit_lease" ),
                  QStringLiteral( "The asset has no active Edit Lease to roll back" ),
                  DiagnosticSeverity::Error } );
  }

  // Rollback releases the Edit Lease without advancing the revision.
  ( *leaseIt ).control->active = false;
  ( *leaseIt ).control->manager.clear();
  m_impl->leases.erase( leaseIt );
  m_impl->catalogGeneration++;

  return Result<void>::success();
}

int DataManager::leaseCount( AssetId id ) const
{
  return static_cast<int>(
    std::count_if( m_impl->leases.begin(), m_impl->leases.end(),
                   [&]( const Impl::LeaseRecord &lease ) {
                     return lease.control->assetId == id;
                   } ) );
}

QVector<LeaseRef> DataManager::leases( AssetId id ) const
{
  QVector<LeaseRef> result;
  for ( const Impl::LeaseRecord &lease : m_impl->leases )
  {
    if ( lease.control->assetId == id )
    {
      result.append(
        LeaseRef{ lease.control->assetId, lease.control->token, lease.control->kind } );
    }
  }
  return result;
}

bool DataManager::hasActiveEditLease( AssetId id ) const
{
  for ( const Impl::LeaseRecord &lease : m_impl->leases )
  {
    if ( lease.control->assetId == id && lease.control->active &&
         lease.control->kind == LeaseKind::Edit )
      return true;
  }
  return false;
}

UnloadPlan DataManager::planUnload( AssetId id ) const
{
  AssetRevision revision;
  const auto recordIt = m_impl->findRecord( id );
  if ( recordIt != m_impl->records.end() )
    revision = recordIt->snapshot.revision();

  return UnloadPlan{
    id, revision, m_impl->catalogGeneration, m_impl->leaseImpacts( id ) };
}

Result<void> DataManager::unload( const UnloadPlan &confirmedPlan )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto recordIt = m_impl->findRecord( confirmedPlan.assetId() );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "unload.unknown_asset" ),
                  QStringLiteral( "The asset is no longer registered" ),
                  DiagnosticSeverity::Error } );
  }

  if ( confirmedPlan.catalogGeneration() != m_impl->catalogGeneration )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "unload.stale_plan" ),
                  QStringLiteral( "The unload plan was produced against an outdated catalog" ),
                  DiagnosticSeverity::Error } );
  }

  if ( confirmedPlan.revision() != recordIt->snapshot.revision() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "unload.stale_revision" ),
                  QStringLiteral( "The asset revision no longer matches the unload plan" ),
                  DiagnosticSeverity::Error } );
  }

  const QVector<LeaseImpact> liveLeaseImpacts =
    m_impl->leaseImpacts( confirmedPlan.assetId() );

  // Normal unload is rejected while any active lease exists. A confirmed cascade
  // plan revokes those leases atomically rather than silently dropping them.
  if ( !confirmedPlan.cascade() && !liveLeaseImpacts.isEmpty() )
  {
    return Result<void>::failure( leasedRefusalDiagnostics( QStringLiteral( "unload" ),
                                                            liveLeaseImpacts ) );
  }

  emit assetAboutToUnload( confirmedPlan.assetId() );

  if ( confirmedPlan.cascade() )
  {
    for ( const LeaseImpact &impact : liveLeaseImpacts )
      revokeLease( impact.lease );
  }

  m_impl->records.erase( recordIt );
  m_impl->catalogGeneration++;

  emit assetRemoved( confirmedPlan.assetId() );
  return Result<void>::success();
}

ReapResult DataManager::reap( const ReapRequest &request )
{
  ReapResult result;

  if ( QThread::currentThread() != thread() )
  {
    result.diagnostics.append( wrongThreadDiagnostic() );
    return result;
  }

  const auto recordIt = m_impl->findRecord( request.id );
  if ( recordIt == m_impl->records.end() )
  {
    result.diagnostics.append(
      Diagnostic{ QStringLiteral( "reap.unknown_asset" ),
                  QStringLiteral( "The asset is no longer registered" ),
                  DiagnosticSeverity::Error } );
    return result;
  }

  const AssetSnapshot &snapshot = recordIt->snapshot;

  // Reaping is the capability-limited deletion command for temporary assets
  // only. A persistent asset is never reaped - its source is not owned by the
  // Data Manager's lifecycle.
  if ( snapshot.persistence() == PersistencePolicy::ProjectPersistent )
  {
    result.diagnostics.append(
      Diagnostic{ QStringLiteral( "reap.persistent" ),
                  QStringLiteral( "A ProjectPersistent asset cannot be reaped; "
                                  "use unload to remove it from the catalog" ),
                  DiagnosticSeverity::Error } );
    return result;
  }

  // The lease is the "still in use" signal. Reaping a leased asset would
  // revoke leases out from under a viewer or processor; refuse instead.
  const QVector<LeaseImpact> liveLeaseImpacts = m_impl->leaseImpacts( request.id );
  if ( !liveLeaseImpacts.isEmpty() )
  {
    result.diagnostics = leasedRefusalDiagnostics( QStringLiteral( "reap" ), liveLeaseImpacts );
    return result;
  }

  // Announce before mutation so the Display Manager removes the layer, exactly
  // as unload does.
  emit assetAboutToUnload( request.id );

  const QString sourcePath = snapshot.source().canonicalSource;
  const bool deletable =
    snapshot.capabilities().testFlag( AssetCapability::DeletableSource );

  m_impl->records.erase( recordIt );
  m_impl->catalogGeneration++;
  result.unloaded = true;

  // Physical deletion is gated by DeletableSource. A temporary asset the Data
  // Manager did not publish (e.g. an imported scratch file) is unloaded but
  // its file is left on disk. NOTE: this deletes the single canonical source
  // file; multi-file resource sets (ENVI .hdr/.dat pairs, Shapefile components)
  // are a known follow-up to route through a provider deleteSource seam.
  if ( deletable && !sourcePath.isEmpty() && QFile::exists( sourcePath ) )
  {
    if ( !QFile::remove( sourcePath ) )
    {
      // The catalog entry is already gone; surface the orphaned file rather
      // than hiding it. The catalog never points at a deleted file because the
      // record is already removed.
      result.diagnostics.append(
        Diagnostic{ QStringLiteral( "reap.delete_failed" ),
                    QStringLiteral( "The asset was removed from the catalog but its "
                                    "source file could not be deleted: %1" )
                      .arg( sourcePath ),
                    DiagnosticSeverity::Warning } );
    }
    else
    {
      result.sourceDeleted = true;
    }
  }

  emit assetRemoved( request.id );
  return result;
}

Result<void> DataManager::promote( AssetId id )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto recordIt = m_impl->findRecord( id );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "promote.unknown_asset" ),
                  QStringLiteral( "The asset is no longer registered" ),
                  DiagnosticSeverity::Error } );
  }

  // Promoting an already-persistent asset is a successful no-op: no catalog
  // mutation, no signal.
  if ( recordIt->snapshot.persistence() == PersistencePolicy::ProjectPersistent )
    return Result<void>::success();

  // Rebuild the snapshot with the policy flipped; identity, revision, source,
  // structure, capabilities, and provenance are all preserved. AssetSnapshot
  // is immutable, so a fresh instance replaces the record (the same pattern
  // commitEdit uses to advance a revision; provenance lives in a separate
  // optional on the record and is untouched here).
  const AssetSnapshot &current = recordIt->snapshot;
  AssetSnapshot promoted{ current.id(),
                          current.revision(),
                          current.source(),
                          current.kind(),
                          current.state(),
                          current.capabilities(),
                          PersistencePolicy::ProjectPersistent,
                          current.storageKind(),
                          current.displayName(),
                          current.structure() };
  recordIt->snapshot = std::move( promoted );
  m_impl->catalogGeneration++;

  emit assetChanged( id );
  return Result<void>::success();
}

TemporaryReapResult DataManager::reapSessionTemporaries()
{
  return reapTemporaries( PersistencePolicy::SessionTemporary );
}

TemporaryReapResult DataManager::reapTaskTemporaries()
{
  return reapTemporaries( PersistencePolicy::TaskTemporary );
}

TemporaryReapResult DataManager::reapTemporaries( PersistencePolicy policy )
{
  TemporaryReapResult result;

  if ( QThread::currentThread() != thread() )
  {
    result.diagnostics.append( wrongThreadDiagnostic() );
    return result;
  }

  // Collect the matching temporary asset ids first; reaping mutates the
  // records vector, so we cannot iterate it while removing. Leased assets are
  // skipped and reported (not force-revoked); the host decides what to do.
  QVector<AssetId> idle;
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.snapshot.persistence() != policy )
      continue;
    if ( m_impl->leaseImpacts( record.snapshot.id() ).isEmpty() )
      idle.append( record.snapshot.id() );
    else
      result.skippedLeased.append( record.snapshot.id() );
  }

  for ( const AssetId &id : idle )
  {
    const ReapResult one = reap( ReapRequest{ id } );
    if ( one.unloaded )
    {
      ++result.reapedCount;
    }
    else
    {
      // An asset that was idle at collect-time but became leased (or was
      // otherwise refused) by reap-time is reported as skipped so the result's
      // skipped set stays complete - the host sees every temporary of this
      // policy that remained in the catalog, not just the ones leased at
      // classification.
      result.skippedLeased.append( id );
    }
    result.diagnostics.append( one.diagnostics );
  }

  return result;
}

LeaseOutcome DataManager::releaseLease( const LeaseRef &lease )
{
  if ( QThread::currentThread() != thread() )
    return LeaseOutcome::Invalid;

  const auto it =
    std::find_if( m_impl->leases.begin(), m_impl->leases.end(),
                  [&]( const Impl::LeaseRecord &record ) {
                    return record.control->token == lease.token &&
                           record.control->assetId == lease.assetId;
                  } );
  if ( it == m_impl->leases.end() )
    return LeaseOutcome::Invalid;

  ( *it ).control->active = false;
  ( *it ).control->manager.clear();
  m_impl->leases.erase( it );
  m_impl->catalogGeneration++;
  return LeaseOutcome::Released;
}

void DataManager::revokeLease( const LeaseRef &lease )
{
  const auto it =
    std::find_if( m_impl->leases.begin(), m_impl->leases.end(),
                  [&]( const Impl::LeaseRecord &record ) {
                    return record.control->token == lease.token &&
                           record.control->assetId == lease.assetId;
                  } );
  if ( it != m_impl->leases.end() )
  {
    ( *it ).control->active = false;
    ( *it ).control->manager.clear();
    m_impl->leases.erase( it );
  }
}

AssetLease::AssetLease( std::shared_ptr<internal::AssetLeaseControl> control )
  : m_control( std::move( control ) )
{
}

AssetLease::AssetLease( AssetLease &&other ) noexcept
  : m_control( std::move( other.m_control ) )
{
}

AssetLease &AssetLease::operator=( AssetLease &&other ) noexcept
{
  if ( this == &other )
    return *this;

  release();
  m_control = std::move( other.m_control );
  return *this;
}

AssetLease::~AssetLease()
{
  release();
}

bool AssetLease::isValid() const
{
  return m_control && m_control->active && !m_control->manager.isNull();
}

const AssetId &AssetLease::assetId() const
{
  static const AssetId nullAssetId;
  return m_control ? m_control->assetId : nullAssetId;
}

quint64 AssetLease::token() const
{
  return m_control ? m_control->token : 0;
}

LeaseKind AssetLease::kind() const
{
  return m_control ? m_control->kind : LeaseKind::View;
}

const QString &AssetLease::purpose() const
{
  static const QString emptyPurpose;
  return m_control ? m_control->purpose : emptyPurpose;
}

LeaseRef AssetLease::toRef() const
{
  return LeaseRef{ assetId(), token(), kind() };
}

LeaseOutcome AssetLease::release()
{
  if ( !isValid() )
    return LeaseOutcome::Invalid;

  DataManager *manager = m_control->manager.data();
  const LeaseOutcome outcome = manager->releaseLease( toRef() );
  return outcome;
}

} // namespace sicnu::data
