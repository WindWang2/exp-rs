#include "data_manager.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <utility>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include "internal/source_provider_registry.h"
#include "internal/network_probe.h"
#include "providers/gdal_raster_source_provider.h"
#include "providers/ogr_vector_source_provider.h"
#include "providers/tms_source_provider.h"
#include "providers/virtual_raster_source_provider.h"
#include "providers/wms_source_provider.h"
#include "providers/wmts_source_provider.h"
#include "providers/xyz_source_provider.h"
#include "virtual_raster_preflight.h"

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

bool isVirtualOrRemotePath( const QString &path )
{
  return path.startsWith( QLatin1String( "/vsi" ), Qt::CaseInsensitive ) ||
         path.startsWith( QLatin1String( "http://" ), Qt::CaseInsensitive ) ||
         path.startsWith( QLatin1String( "https://" ), Qt::CaseInsensitive );
}

/// Additive aliases so a raw STAC https href and the provider's /vsicurl/
/// spelling resolve to the same catalog record. Local files are unchanged.
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
  // Atomic: AssetLease::release() neutralizes the flag from a foreign
  // thread (the RAII holder's single release attempt must always consume
  // the lease) while the manager thread may be reading lease impacts.
  std::atomic<bool> active{ false };
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
    /// The recipe a Virtual Raster Asset was created from; absent for
    /// non-virtual assets. The recipe is the identity; the generated `.vrt`
    /// at the snapshot's canonicalSource is a disposable artifact.
    std::optional<VirtualRasterRecipe> virtualRecipe;
  };

  struct LeaseRecord
  {
    std::shared_ptr<internal::AssetLeaseControl> control;
  };

  /// A Data Collection catalog node. Organizational only; groups child assets.
  struct CollectionRecord
  {
    CollectionId id;
    QString displayName;
    ProductMetadata metadata;
    QVector<AssetId> childAssetIds;
  };

  /// A TemporalCollection catalog record: identity + canonical descriptor
  /// document (the temporal layer's opaque schema, stored verbatim).
  struct TemporalCollectionRecord_
  {
    CollectionId id;
    QString displayName;
    QString descriptor;
    quint64 revision = 1;
    QDateTime createdAtUtc = QDateTime::currentDateTimeUtc();
    QDateTime updatedAtUtc = QDateTime::currentDateTimeUtc();
  };

  /// One strong-dependency edge: `dependent` consumes `input`. Edges form a
  /// DAG (cycle-checked at insertion). Kept as an insertion-ordered list — the
  /// graph is small (one edge per virtual-raster input) and queries are scans.
  struct DependencyEdge
  {
    AssetId dependent;
    AssetId input;
  };

  explicit Impl( std::unique_ptr<internal::SourceProviderRegistry> sourceProviders )
    : providers( std::move( sourceProviders ) )
  {
  }

  std::unique_ptr<internal::SourceProviderRegistry> providers;
  QVector<AssetRecord> records;
  QVector<LeaseRecord> leases;
  QVector<CollectionRecord> collections;
  QVector<TemporalCollectionRecord_> temporalCollections;
  QVector<DependencyEdge> dependencyEdges;
  /// Managed scratch directory for generated virtual-raster artifacts. Lazily
  /// created by createVirtualRaster; the `.vrt` files inside are disposable
  /// build artifacts (the recipes are the identity).
  std::unique_ptr<QTemporaryDir> vrtScratchDir;
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

  QVector<CollectionRecord>::iterator findCollection( CollectionId id )
  {
    return std::find_if(
      collections.begin(), collections.end(),
      [&]( const CollectionRecord &c ) { return c.id == id; } );
  }

  QVector<CollectionRecord>::const_iterator findCollection( CollectionId id ) const
  {
    return std::find_if(
      collections.begin(), collections.end(),
      [&]( const CollectionRecord &c ) { return c.id == id; } );
  }

  QVector<TemporalCollectionRecord_>::iterator findTemporalCollection( CollectionId id )
  {
    return std::find_if(
      temporalCollections.begin(), temporalCollections.end(),
      [&]( const TemporalCollectionRecord_ &c ) { return c.id == id; } );
  }

  QVector<TemporalCollectionRecord_>::const_iterator findTemporalCollection( CollectionId id ) const
  {
    return std::find_if(
      temporalCollections.begin(), temporalCollections.end(),
      [&]( const TemporalCollectionRecord_ &c ) { return c.id == id; } );
  }

  QVector<LeaseImpact> leaseImpacts( AssetId id ) const
  {
    QVector<LeaseImpact> impacts;
    for ( const LeaseRecord &lease : leases )
    {
      // Only ACTIVE leases count (#703): a cross-thread AssetLease::release()
      // neutralizes the control immediately and queues the record removal for
      // the manager's thread. Until that queued call runs, the dead record
      // must not block unload/reap (mirroring the Edit-lease conflict check,
      // which already filtered on active).
      if ( lease.control->assetId == id && lease.control->active )
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
  return defaultProviders( nullptr );
}

std::unique_ptr<internal::SourceProviderRegistry>
DataManager::defaultProviders( const internal::NetworkProbe *probe )
{
  auto registry = std::make_unique<internal::SourceProviderRegistry>();
  registry->add( std::make_unique<providers::GdalRasterSourceProvider>() );
  registry->add( std::make_unique<providers::OgrVectorSourceProvider>() );
  // The four remote-map providers take a NetworkProbe*; a null probe (the
  // defaultProviders() case) yields the NoNetworkProbe fallback so an asset
  // still records (resolving Offline) when the host has not injected a real
  // HTTP probe. The real Qt-Network-backed probe lives in src/app (the
  // network-free invariant for src/data) and is host-injected (#66). Tests
  // inject a stub probe.
  registry->add( std::make_unique<providers::XyzSourceProvider>( probe ) );
  // WMS/WMTS/TMS share the XYZ shape (spec #63); each claims its own provider
  // key and probes through the same NetworkProbe seam.
  registry->add( std::make_unique<providers::WmsSourceProvider>( probe ) );
  registry->add( std::make_unique<providers::WmtsSourceProvider>( probe ) );
  registry->add( std::make_unique<providers::TmsSourceProvider>( probe ) );
  return registry;
}

DataManager::DataManager( QObject *parent )
  : DataManager( defaultProviders(), parent )
{
  // The virtual-raster provider needs the owning DataManager to look up input
  // snapshots when realizing a recipe; bind it here (internal seam - the
  // public interface never exposes it). Both live as long as the DataManager.
  m_impl->providers->add(
    std::make_unique<providers::VirtualRasterSourceProvider>(
      [this]( AssetId id ) { return asset( id ); } ) );
}

DataManager::DataManager( const internal::NetworkProbe *probe, QObject *parent )
  : DataManager( defaultProviders( probe ), parent )
{
  // Same virtual-raster bind as the public constructor; only the remote-map
  // providers differ (they received `probe`). This is the host-injection seam
  // for #66 — private/friend-gated so no public API widens.
  m_impl->providers->add(
    std::make_unique<providers::VirtualRasterSourceProvider>(
      [this]( AssetId id ) { return asset( id ); } ) );
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
  for ( Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.sourceKey != sourceKey )
      continue;

    // Dedup hit. The bytes behind the stable path may still have changed:
    // OutputCommitter's publish-then-swap replaces them, and a source file can
    // be mutated externally. Reuse silently would leave the snapshot (and any
    // displayed layer) stale while the content moved under it (#687).
    const bool structureDiffers = record.snapshot.structure() != source.structure;
    // #726 revision convergence: a re-publication whose producing execution is
    // unchanged (same execution fingerprint on the existing derivation) and
    // whose structure snapshot still matches IS the same artifact — advancing
    // the revision would fabricate a change that downstream fingerprints then
    // chase forever. Any real difference (structure changed, fingerprint
    // different/absent) keeps the #687 update semantics.
    const bool sameExecutionRepublished =
      !structureDiffers && !request.executionFingerprint.isEmpty()
      && record.derivation.has_value()
      && record.derivation->executionFingerprint == request.executionFingerprint;
    if ( sameExecutionRepublished || ( !request.notifyUpdateOnReuse && !structureDiffers ) )
      return RegisterResult{ record.snapshot.id(), true, {} };

    // Treat the asset as updated: refresh the snapshot from the fresh
    // resolution, advance the revision one step (mirroring relocate), and
    // emit assetChanged so displays reload. The descriptor is preserved —
    // the re-registration carries a bare provider/path descriptor and must
    // not drop the stored dataOptions (which also key the identity).
    const AssetId existingId = record.snapshot.id();
    AssetSnapshot updated{ existingId,
                           record.snapshot.revision().next(),
                           record.snapshot.source(),
                           source.kind,
                           source.state,
                           record.snapshot.capabilities() | source.capabilities
                             | request.additionalCapabilities,
                           record.snapshot.persistence(),
                           source.storageKind,
                           source.displayName,
                           source.structure,
                           record.snapshot.acquisitionTime(),
                           record.snapshot.parentCollectionId() };
    record.snapshot = std::move( updated );
    m_impl->catalogGeneration++;

    emit assetChanged( existingId );
    return RegisterResult{ existingId, true, resolved.diagnostics() };
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
                          source.structure,
                          request.acquisitionTime };
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
                          source.structure,
                          request.acquisitionTime };
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
                         replacement.structure,
                         current.acquisitionTime(),
                         current.parentCollectionId() };
  recordIt->sourceKey = newSourceKey;
  recordIt->snapshot = std::move( updated );
  m_impl->catalogGeneration++;

  // Regenerate dependent virtual rasters: their recipes reference this asset
  // by AssetId, so a relocation must rewrite the generated VRT against the new
  // canonical source. The artifact path is unchanged; each dependent's
  // structure is unchanged (relocation validated it), but its file content
  // changed - report the change so displays reload.
  QVector<Diagnostic> relocateDiagnostics = resolved.diagnostics();
  for ( const AssetId &dependentId : strongDependentsOf( request.id ) )
  {
    const auto dependentIt = m_impl->findRecord( dependentId );
    if ( dependentIt == m_impl->records.end() ||
         !dependentIt->virtualRecipe.has_value() )
      continue;

    QVector<AssetSnapshot> inputSnapshots;
    bool allAvailable = true;
    for ( const BandRef &bandRef : dependentIt->virtualRecipe->inputs )
    {
      const std::optional<AssetSnapshot> snapshot = asset( bandRef.asset );
      if ( !snapshot.has_value() )
      {
        allAvailable = false;
        break;
      }
      inputSnapshots.append( *snapshot );
    }
    if ( !allAvailable )
      continue;

    const QString xml = providers::buildVirtualRasterXml(
      *dependentIt->virtualRecipe, inputSnapshots );
    if ( xml.isEmpty() )
      continue;

    QFile vrtFile( dependentIt->snapshot.source().canonicalSource );
    if ( vrtFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
    {
      vrtFile.write( xml.toUtf8() );
      vrtFile.close();
      emit assetChanged( dependentId );
    }
    else
    {
      relocateDiagnostics.append( Diagnostic{
        QStringLiteral( "relocate.dependent_vrt_write_failed" ),
        QStringLiteral( "Failed to rewrite dependent virtual raster VRT: %1" )
          .arg( dependentIt->snapshot.source().canonicalSource ),
        DiagnosticSeverity::Warning
      } );
    }
  }

  emit assetChanged( request.id );
  return Result<RelocateResult>::success(
    RelocateResult{ request.id, newRevision, std::move( relocateDiagnostics ) } );
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

std::optional<AssetSnapshot> DataManager::findByPath( const QString &path ) const
{
  if ( path.trimmed().isEmpty() )
    return std::nullopt;

  const QStringList queryAliases = virtualPathAliases( path );
  const bool queryVirtual = isVirtualOrRemotePath( path );

  const QFileInfo fi( path );
  // QFileInfo mangles non-local strings (empty canonicalFilePath; absolute
  // prepends cwd or a drive letter). Remote/VSI identity is string+alias only.
  const QString absolute = queryVirtual ? QString() : fi.absoluteFilePath();
  const QString canonicalPath = queryVirtual ? QString() : fi.canonicalFilePath();
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    const QString &stored = record.snapshot.source().canonicalSource;
    const QStringList storedAliases = virtualPathAliases( stored );
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
      return record.snapshot;

    if ( queryVirtual || isVirtualOrRemotePath( stored ) )
      continue;

    if ( stored == canonicalPath && !canonicalPath.isEmpty() )
      return record.snapshot;
    const QFileInfo storedFi( stored );
    const QString storedCanonical = storedFi.canonicalFilePath();
    if ( !canonicalPath.isEmpty() && !storedCanonical.isEmpty() )
    {
      if ( storedCanonical == canonicalPath )
        return record.snapshot;
    }
    else if ( !absolute.isEmpty() && storedFi.absoluteFilePath() == absolute )
    {
      return record.snapshot;
    }
  }
  return std::nullopt;
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

QVector<AssetId> DataManager::derivedFrom( AssetId id ) const
{
  QVector<AssetId> result;
  const auto it = m_impl->findRecord( id );
  if ( it == m_impl->records.end() || !it->derivation )
    return result;
  for ( const DerivationInput &input : it->derivation->inputs )
    result.append( input.assetId );
  return result;
}

QVector<AssetId> DataManager::derivedOutputsOf( AssetId id ) const
{
  QVector<AssetId> result;
  for ( const auto &record : m_impl->records )
  {
    if ( !record.derivation )
      continue;
    for ( const DerivationInput &input : record.derivation->inputs )
    {
      if ( input.assetId == id )
      {
        result.append( record.snapshot.id() );
        break;
      }
    }
  }
  return result;
}

QVector<AssetId> DataManager::derivedOutputsOfCollection( CollectionId id ) const
{
  QVector<AssetId> result;
  const auto colAssetId = AssetId::fromString( id.toString() );
  for ( const auto &record : m_impl->records )
  {
    if ( !record.derivation )
      continue;
    if ( record.derivation->collectionId && *record.derivation->collectionId == id )
    {
      result.append( record.snapshot.id() );
      continue;
    }
    if ( colAssetId )
    {
      for ( const DerivationInput &input : record.derivation->inputs )
      {
        if ( input.assetId == *colAssetId )
        {
          result.append( record.snapshot.id() );
          break;
        }
      }
    }
  }
  return result;
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
  // Replacing an existing derivation (#687): a re-commit over the same stable
  // path rewrites provenance under an unchanged asset identity. Emit one
  // assetChanged so observers refresh, mirroring the revision bump emitted by
  // the registerSource update path. A first attach stays silent — registration
  // already emitted assetAdded for the fresh asset.
  const bool replaced = it->derivation.has_value();
  it->derivation = stamped;
  if ( replaced )
    emit assetChanged( id );
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
  // The control holds a std::atomic flag, so it is built in place rather
  // than move-constructed from a temporary.
  auto control = std::make_shared<internal::AssetLeaseControl>();
  control->assetId = asset.id;
  control->token = token;
  control->kind = use.kind;
  control->purpose = use.purpose;
  control->manager = this;
  control->active.store( true, std::memory_order_release );
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
                                      recordIt->snapshot.structure(),
                                      recordIt->snapshot.acquisitionTime(),
                                      recordIt->snapshot.parentCollectionId() };

  ( *leaseIt ).control->active = false;
  ( *leaseIt ).control->manager.clear();
  m_impl->leases.erase( leaseIt );
  m_impl->catalogGeneration++;

  emit assetChanged( id );
  return Result<void>::success();
}

Result<void> DataManager::notifyExternalContentChange( AssetId id )
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
                                      recordIt->snapshot.structure(),
                                      recordIt->snapshot.acquisitionTime(),
                                      recordIt->snapshot.parentCollectionId() };

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
                     // Active leases only (#703): see Impl::leaseImpacts.
                     return lease.control->assetId == id && lease.control->active;
                   } ) );
}

QVector<LeaseRef> DataManager::leases( AssetId id ) const
{
  QVector<LeaseRef> result;
  for ( const Impl::LeaseRecord &lease : m_impl->leases )
  {
    // Active leases only (#703): see Impl::leaseImpacts.
    if ( lease.control->assetId == id && lease.control->active )
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
    id, revision, m_impl->catalogGeneration, m_impl->leaseImpacts( id ),
    strongDependentsOf( id ) };
}

Result<void> DataManager::addStrongDependency( AssetId dependent, AssetId input )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  if ( dependent == input )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "dependency.cycle" ),
                  QStringLiteral( "An asset cannot strongly depend on itself" ),
                  DiagnosticSeverity::Error } );
  }

  if ( m_impl->findRecord( dependent ) == m_impl->records.end() ||
       m_impl->findRecord( input ) == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "dependency.unknown_asset" ),
                  QStringLiteral( "Both assets must be registered to record a "
                                  "strong dependency" ),
                  DiagnosticSeverity::Error } );
  }

  // Duplicate edge: successful no-op.
  for ( const Impl::DependencyEdge &edge : m_impl->dependencyEdges )
  {
    if ( edge.dependent == dependent && edge.input == input )
      return Result<void>::success();
  }

  // Cycle check: adding dependent -> input closes a cycle exactly when
  // `dependent` is reachable from `input` following existing edges (i.e. the
  // input already depends on the dependent, transitively).
  {
    QVector<AssetId> stack{ input };
    QVector<AssetId> visited;
    while ( !stack.isEmpty() )
    {
      const AssetId current = stack.takeLast();
      if ( current == dependent )
      {
        return Result<void>::failure(
          Diagnostic{ QStringLiteral( "dependency.cycle" ),
                      QStringLiteral( "Adding this dependency would close a cycle "
                                      "in the strong-dependency graph" ),
                      DiagnosticSeverity::Error } );
      }
      if ( visited.contains( current ) )
        continue;
      visited.append( current );
      for ( const Impl::DependencyEdge &edge : m_impl->dependencyEdges )
      {
        if ( edge.dependent == current )
          stack.append( edge.input );
      }
    }
  }

  m_impl->dependencyEdges.append( Impl::DependencyEdge{ dependent, input } );
  // The edge changes the catalog's dependency state: any UnloadPlan captured
  // before this point is stale (its strongDependents impact is out of date),
  // exactly as with any other catalog mutation.
  m_impl->catalogGeneration++;
  return Result<void>::success();
}

QVector<AssetId> DataManager::strongDependenciesOf( AssetId id ) const
{
  QVector<AssetId> inputs;
  for ( const Impl::DependencyEdge &edge : m_impl->dependencyEdges )
  {
    if ( edge.dependent == id )
      inputs.append( edge.input );
  }
  return inputs;
}

QVector<AssetId> DataManager::strongDependentsOf( AssetId id ) const
{
  QVector<AssetId> dependents;
  for ( const Impl::DependencyEdge &edge : m_impl->dependencyEdges )
  {
    if ( edge.input == id )
      dependents.append( edge.dependent );
  }
  return dependents;
}

Result<AssetId> DataManager::createVirtualRaster(
  const VirtualRasterRecipe &recipe, PersistencePolicy persistence )
{
  if ( QThread::currentThread() != thread() )
    return Result<AssetId>::failure( wrongThreadDiagnostic() );

  // Preflight first: a hard-failure verdict registers nothing.
  const PreflightResult preflight = preflightVirtualRaster( recipe, *this );
  if ( !preflight.canCreate )
    return Result<AssetId>::failure( preflight.diagnostics );

  // Cross-CRS composition needs a warped VRT, which this wave does not
  // generate; refuse honestly rather than writing a geo-wrong artifact.
  if ( preflight.verdict == PreflightVerdict::RequiresReprojection )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "virtual_raster.reprojection_unsupported" ),
                  QStringLiteral( "Composing inputs across differing CRSs is not "
                                  "supported yet (warped virtual rasters are a "
                                  "follow-up)" ),
                  DiagnosticSeverity::Error } );
  }

  // The recipe hash keys both dedup (via the descriptor) and the scratch
  // artifact path: same recipe -> same path -> same SourceKey -> reuse.
  const QJsonDocument recipeDoc( recipe.toJson() );
  const QString recipeHash = QString::fromUtf8(
    QCryptographicHash::hash( recipeDoc.toJson( QJsonDocument::Compact ),
                              QCryptographicHash::Sha1 )
      .toHex() );

  if ( !m_impl->vrtScratchDir || !m_impl->vrtScratchDir->isValid() )
    m_impl->vrtScratchDir = std::make_unique<QTemporaryDir>();
  if ( !m_impl->vrtScratchDir->isValid() )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "virtual_raster.scratch_dir_unavailable" ),
                  QStringLiteral( "Failed to create scratch directory for virtual raster" ),
                  DiagnosticSeverity::Error } );
  }
  const QString vrtPath = QStringLiteral( "%1/vrt_%2.vrt" )
                            .arg( m_impl->vrtScratchDir->path(), recipeHash );

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "vrt" );
  source.canonicalSource = vrtPath;
  source.dataOptions.insert( QStringLiteral( "recipe" ),
                             QString::fromUtf8(
                               recipeDoc.toJson( QJsonDocument::Compact ) ) );

  RegisterRequest request;
  request.source = source;
  request.persistence = persistence;
  const RegisterResult registered = registerSource( request );
  if ( registered.assetId.isNull() )
    return Result<AssetId>::failure( registered.diagnostics );

  // A dedup hit already carries the recipe and its edges.
  if ( registered.reusedExisting )
    return Result<AssetId>::success( registered.assetId );

  // registerSource emitted assetAdded before returning; a DirectConnection
  // slot may have re-entered and unloaded/reaped the just-registered asset.
  const auto recordIt = m_impl->findRecord( registered.assetId );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "virtual_raster.vanished" ),
                  QStringLiteral( "The virtual raster was unloaded while being created" ),
                  DiagnosticSeverity::Error } );
  }
  recordIt->virtualRecipe = recipe;

  // One edge per DISTINCT input asset (a recipe may reference one asset in
  // several bands). On a cycle (a recipe consuming its own dependents), roll
  // the registration back so nothing partial remains.
  QVector<AssetId> distinctInputs;
  for ( const BandRef &bandRef : recipe.inputs )
  {
    if ( !distinctInputs.contains( bandRef.asset ) )
      distinctInputs.append( bandRef.asset );
  }
  for ( const AssetId &input : distinctInputs )
  {
    const Result<void> edge = addStrongDependency( registered.assetId, input );
    if ( !edge )
    {
      // Compensating rollback. A FAILED unload must not vanish: it would
      // leave a partially-registered virtual asset (missing edges) behind a
      // reported failure (#703). Surface the unload diagnostics alongside the
      // triggering ones so the caller sees both the cause and the leftovers.
      const UnloadPlan plan = planUnload( registered.assetId ).confirmedCascade();
      const Result<void> rollback = unload( plan );
      QVector<Diagnostic> diagnostics = edge.diagnostics();
      if ( !rollback )
        diagnostics.append( rollback.diagnostics() );
      return Result<AssetId>::failure( diagnostics );
    }
  }

  return Result<AssetId>::success( registered.assetId );
}

std::optional<VirtualRasterRecipe> DataManager::virtualRasterRecipe(
  AssetId id ) const
{
  const auto recordIt = m_impl->findRecord( id );
  if ( recordIt == m_impl->records.end() )
    return std::nullopt;
  return recordIt->virtualRecipe;
}

Result<AssetId> DataManager::restoreVirtualRaster(
  const RestoreVirtualRasterRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return Result<AssetId>::failure( wrongThreadDiagnostic() );

  if ( request.id.isNull() )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "restore.invalid_asset_id" ),
                  QStringLiteral( "A persisted virtual raster requires a valid id" ),
                  DiagnosticSeverity::Error } );
  }
  if ( request.recipe.inputs.isEmpty() )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "recipe.invalid" ),
                  QStringLiteral( "A persisted virtual raster recipe has no inputs" ),
                  DiagnosticSeverity::Error } );
  }

  // The recipe hash keys the descriptor and the deterministic scratch path,
  // exactly as createVirtualRaster derives them - same recipe -> same path ->
  // same SourceKey -> dedup across save/restore.
  const QJsonDocument recipeDoc( request.recipe.toJson() );
  const QString recipeHash = QString::fromUtf8(
    QCryptographicHash::hash( recipeDoc.toJson( QJsonDocument::Compact ),
                              QCryptographicHash::Sha1 )
      .toHex() );

  if ( !m_impl->vrtScratchDir || !m_impl->vrtScratchDir->isValid() )
    m_impl->vrtScratchDir = std::make_unique<QTemporaryDir>();
  if ( !m_impl->vrtScratchDir->isValid() )
  {
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "virtual_raster.scratch_dir_unavailable" ),
                  QStringLiteral( "Failed to create scratch directory for virtual raster" ),
                  DiagnosticSeverity::Error } );
  }
  const QString vrtPath = QStringLiteral( "%1/vrt_%2.vrt" )
                            .arg( m_impl->vrtScratchDir->path(), recipeHash );

  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "vrt" );
  descriptor.canonicalSource = vrtPath;
  descriptor.dataOptions.insert( QStringLiteral( "recipe" ),
                                 QString::fromUtf8(
                                   recipeDoc.toJson( QJsonDocument::Compact ) ) );
  const SourceKey sourceKey = descriptor.sourceKey();

  // Conflict guards mirror restoreSource: an id already bound to a different
  // source, or this source already bound to a different id, are both refused.
  const auto existing = m_impl->findRecord( request.id );
  if ( existing != m_impl->records.end() )
  {
    if ( existing->sourceKey == sourceKey )
    {
      // Idempotent re-restore of the same virtual asset: re-bind any missing
      // edges whose inputs have since appeared, then return.
      QVector<Diagnostic> edgeDiagnostics;
      restoreVirtualRasterEdges( request, edgeDiagnostics );
      return Result<AssetId>::success( request.id, std::move( edgeDiagnostics ) );
    }
    return Result<AssetId>::failure(
      Diagnostic{ QStringLiteral( "restore.asset_id_conflict" ),
                  QStringLiteral( "The persisted Asset ID is already bound to "
                                  "another source" ),
                  DiagnosticSeverity::Error } );
  }
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.sourceKey == sourceKey )
    {
      return Result<AssetId>::failure(
        Diagnostic{ QStringLiteral( "restore.source_conflict" ),
                    QStringLiteral( "The persisted source is already bound to "
                                    "another Asset ID" ),
                    DiagnosticSeverity::Error } );
    }
  }

  QVector<Diagnostic> diagnostics;

  // Resolve through the provider. The provider looks up each input via the
  // bound AssetLookup; a missing input fails with virtual_raster.input_unavailable,
  // which we treat as a non-dropping Warning: the asset is still recorded in a
  // non-Ready state and its dependency edges are skipped.
  const Result<internal::ResolvedSource> resolved =
    m_impl->providers->resolve( descriptor );
  const bool inputsPresent = bool( resolved );

  AssetSnapshot snapshot{ request.id,
                          request.revision.isValid() ? request.revision
                                                     : AssetRevision::initial(),
                          descriptor,
                          AssetKind::VirtualRaster,
                          inputsPresent ? resolved.value().state
                                        : AssetState::UnavailableSource,
                          // A non-Ready virtual raster with absent inputs cannot
                          // honor relocation (the relocate path re-resolves
                          // inputs); advertise no capabilities until the inputs
                          // return and the asset re-resolves to Ready.
                          inputsPresent ? resolved.value().capabilities
                                        : AssetCapability::None,
                          request.persistence,
                          StorageKind::File,
                          inputsPresent ? resolved.value().displayName
                                        : QStringLiteral( "VRT(unavailable)" ),
                          inputsPresent ? resolved.value().structure : AssetStructure{} };
  if ( resolved )
    diagnostics += resolved.diagnostics();

  Impl::AssetRecord record{ sourceKey, std::move( snapshot ) };
  record.virtualRecipe = request.recipe;
  m_impl->records.push_back( std::move( record ) );
  m_impl->catalogGeneration++;
  emit assetAdded( request.id );

  restoreVirtualRasterEdges( request, diagnostics );

  return Result<AssetId>::success( request.id, std::move( diagnostics ) );
}

void DataManager::restoreVirtualRasterEdges(
  const RestoreVirtualRasterRequest &request, QVector<Diagnostic> &diagnostics )
{
  QVector<AssetId> distinctInputs;
  for ( const BandRef &bandRef : request.recipe.inputs )
  {
    if ( !distinctInputs.contains( bandRef.asset ) )
      distinctInputs.append( bandRef.asset );
  }
  for ( const AssetId &input : distinctInputs )
  {
    if ( m_impl->findRecord( input ) == m_impl->records.end() )
    {
      // A missing input is not a dropping condition: record a Warning and skip
      // the edge. The recipe still references the (persisted) AssetId, so a
      // future restore of that input can re-bind via idempotent re-restore.
      diagnostics.append(
        Diagnostic{ QStringLiteral( "virtual_raster.missing_input" ),
                    QStringLiteral( "A virtual raster input (%1) is missing from "
                                    "the restored project; its dependency edge "
                                    "is skipped" )
                      .arg( input.toString() ),
                    DiagnosticSeverity::Warning } );
      continue;
    }
    const Result<void> edge = addStrongDependency( request.id, input );
    if ( !edge )
      diagnostics += edge.diagnostics();
  }
}

void DataManager::pruneDependencyEdgesOf( AssetId id )
{
  m_impl->dependencyEdges.erase(
    std::remove_if( m_impl->dependencyEdges.begin(), m_impl->dependencyEdges.end(),
                    [&]( const Impl::DependencyEdge &edge ) {
                      return edge.dependent == id || edge.input == id;
                    } ),
    m_impl->dependencyEdges.end() );
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

  // Normal unload is likewise rejected while any strong dependent consumes
  // this asset: unloading the input would silently break the dependent (e.g. a
  // Virtual Raster Asset's composition). A confirmed cascade removes the
  // dependents transitively, deepest-first, before this asset.
  const QVector<AssetId> dependents = strongDependentsOf( confirmedPlan.assetId() );
  if ( !confirmedPlan.cascade() && !dependents.isEmpty() )
  {
    QVector<Diagnostic> diagnostics;
    for ( const AssetId &dependent : dependents )
    {
      diagnostics.append(
        Diagnostic{ QStringLiteral( "unload.has_dependents" ),
                    QStringLiteral( "The asset cannot be unloaded: it is a strong "
                                    "dependency of asset %1; confirm a cascade to "
                                    "remove the dependent as well" )
                      .arg( dependent.toString() ),
                    DiagnosticSeverity::Error } );
    }
    return Result<void>::failure( diagnostics );
  }

  if ( confirmedPlan.cascade() )
  {
    // Remove every transitive dependent, deepest-first (post-order), so a
    // dependent is always removed before the assets it consumes. Leases on
    // dependents (e.g. their display layers) are revoked just like the main
    // asset's below.
    QVector<AssetId> removalOrder;
    QVector<AssetId> visited;
    // The collection pass runs entirely before any mutation, so iterating the
    // live edge list is safe; `visited` guards diamonds (a dependent reachable
    // through two paths is collected once).
    const std::function<void( AssetId )> collectDependents = [&]( AssetId id ) {
      for ( const Impl::DependencyEdge &edge : m_impl->dependencyEdges )
      {
        if ( edge.input != id || visited.contains( edge.dependent ) )
          continue;
        visited.append( edge.dependent );
        collectDependents( edge.dependent );
        removalOrder.append( edge.dependent );
      }
    };
    collectDependents( confirmedPlan.assetId() );

    for ( const AssetId &dependentId : removalOrder )
    {
      // Emit before locating: a connected slot may re-enter the manager and
      // mutate the records (mirroring the main-erase revalidation below).
      emit assetAboutToUnload( dependentId );
      const auto dependentIt = m_impl->findRecord( dependentId );
      if ( dependentIt == m_impl->records.end() )
        continue;
      for ( const LeaseImpact &impact : m_impl->leaseImpacts( dependentId ) )
        revokeLease( impact.lease );
      m_impl->records.erase( dependentIt );
      pruneChildFromCollections( dependentId );
      pruneDependencyEdgesOf( dependentId );
      m_impl->catalogGeneration++;
      emit assetRemoved( dependentId );
    }
  }

  emit assetAboutToUnload( confirmedPlan.assetId() );

  // DATAPY-7: re-validate leaseImpacts after emit — a slot may have re-entered
  // and acquired a new lease which is not in liveLeaseImpacts (captured before
  // emit). A cascade revokes the fresh list (prevents a dangling LeaseRecord on
  // an erased asset); a non-cascade unload refuses instead, matching the
  // pre-emit lease-safety rule above.
  {
    const QVector<LeaseImpact> freshImpacts = m_impl->leaseImpacts( confirmedPlan.assetId() );
    if ( confirmedPlan.cascade() )
    {
      for ( const LeaseImpact &impact : freshImpacts )
        revokeLease( impact.lease );
    }
    else if ( !freshImpacts.isEmpty() )
    {
      return Result<void>::failure( leasedRefusalDiagnostics( QStringLiteral( "unload" ),
                                                              freshImpacts ) );
    }
  }

  // Re-locate the record: the cascade dependent removals above erased records,
  // invalidating the earlier iterator.
  const auto eraseIt = m_impl->findRecord( confirmedPlan.assetId() );
  if ( eraseIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "unload.unknown_asset" ),
                  QStringLiteral( "The asset is no longer registered" ),
                  DiagnosticSeverity::Error } );
  }
  m_impl->records.erase( eraseIt );
  pruneChildFromCollections( confirmedPlan.assetId() );
  pruneDependencyEdgesOf( confirmedPlan.assetId() );
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

  // Reaping an asset that a strong dependent consumes would silently break the
  // dependent, exactly as unload would; refuse with the same rule.
  const QVector<AssetId> dependents = strongDependentsOf( request.id );
  if ( !dependents.isEmpty() )
  {
    for ( const AssetId &dependent : dependents )
    {
      result.diagnostics.append(
        Diagnostic{ QStringLiteral( "reap.has_dependents" ),
                    QStringLiteral( "The asset cannot be reaped: it is a strong "
                                    "dependency of asset %1" )
                      .arg( dependent.toString() ),
                    DiagnosticSeverity::Error } );
    }
    return result;
  }

  // Announce before mutation so the Display Manager removes the layer, exactly
  // as unload does.
  emit assetAboutToUnload( request.id );

  // The emit runs DirectConnection slots that may re-enter the manager and
  // mutate the catalog (unload/reap/register), invalidating recordIt and the
  // pre-emit snapshot reference (same reentrancy family as DATAPY-7, which
  // unload() already handles). Re-locate the record and re-read everything
  // from the fresh state before erasing.
  const auto freshIt = m_impl->findRecord( request.id );
  if ( freshIt == m_impl->records.end() )
  {
    result.diagnostics.append(
      Diagnostic{ QStringLiteral( "reap.unknown_asset" ),
                  QStringLiteral( "The asset was removed while reaping was announced" ),
                  DiagnosticSeverity::Error } );
    return result;
  }

  // A slot may also have acquired a fresh lease on the asset during the emit;
  // revoke it like unload() does so no LeaseRecord dangles on an erased asset.
  const QVector<LeaseImpact> freshImpacts = m_impl->leaseImpacts( request.id );
  for ( const LeaseImpact &impact : freshImpacts )
    revokeLease( impact.lease );

  const QString sourcePath = freshIt->snapshot.source().canonicalSource;
  const bool deletable =
    freshIt->snapshot.capabilities().testFlag( AssetCapability::DeletableSource );

  m_impl->records.erase( freshIt );
  pruneChildFromCollections( request.id );
  pruneDependencyEdgesOf( request.id );
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
  // structure, capabilities, acquisition time, and provenance are all preserved.
  // AssetSnapshot is immutable, so a fresh instance replaces the record (the
  // same pattern commitEdit uses to advance a revision; provenance lives in a
  // separate optional on the record and is untouched here).
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
                          current.structure(),
                          current.acquisitionTime(),
                          current.parentCollectionId() };
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
  // skipped and reported (not force-revoked); the host decides what to do. An
  // asset with strong dependents is likewise not idle - reaping it would be
  // refused, so classify it as skipped up front rather than misreporting a
  // dependent-blocked asset as leased.
  QVector<AssetId> idle;
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.snapshot.persistence() != policy )
      continue;
    if ( !m_impl->leaseImpacts( record.snapshot.id() ).isEmpty() ||
         !strongDependentsOf( record.snapshot.id() ).isEmpty() )
    {
      result.skippedLeased.append( record.snapshot.id() );
    }
    else
    {
      idle.append( record.snapshot.id() );
    }
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

void DataManager::pruneChildFromCollections( AssetId childId )
{
  for ( Impl::CollectionRecord &collection : m_impl->collections )
    collection.childAssetIds.removeAll( childId );
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
  if ( !m_control || !m_control->active.load( std::memory_order_acquire ) )
    return LeaseOutcome::Invalid;

  // Snapshot the manager QPointer once: it may be destroyed concurrently
  // (app shutdown while a foreign-thread lease unwinds). Calling isValid()
  // and then reading .data() again is a TOCTOU window that would dereference
  // a null/dangling QObject below.
  DataManager *manager = m_control->manager.data();
  if ( !manager )
    return LeaseOutcome::Invalid;

  if ( QThread::currentThread() != manager->thread() )
  {
    // Cross-thread release: releaseLease() refuses to mutate the lease
    // table from a foreign thread, and this destructor-bound call is the
    // holder's ONLY release attempt - returning Invalid here would strand
    // the lease (blocking unload/reap of the asset) until process exit.
    // Neutralize the control immediately and marshal the actual record
    // removal to the manager's thread (the queued call is dropped safely
    // if the manager is destroyed first).
    m_control->active.store( false, std::memory_order_release );
    m_control->manager.clear();
    QMetaObject::invokeMethod(
      manager,
      [manager, ref = toRef()]() { manager->releaseLease( ref ); },
      Qt::QueuedConnection );
    return LeaseOutcome::Released;
  }

  const LeaseOutcome outcome = manager->releaseLease( toRef() );
  return outcome;
}

CollectionCreateResult DataManager::createCollection( const CollectionCreateRequest &request )
{
  return restoreCollection( CollectionId::generate(), request );
}

CollectionCreateResult
DataManager::restoreCollection( CollectionId id, const CollectionCreateRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return CollectionCreateResult{ {}, { wrongThreadDiagnostic() } };

  // Guard against a duplicate id (e.g. a double-read or a stale extension),
  // mirroring restoreSource's id-conflict handling.
  if ( m_impl->findCollection( id ) != m_impl->collections.end() )
  {
    return CollectionCreateResult{ {}, { Diagnostic{
      QStringLiteral( "collection.duplicate_id" ),
      QStringLiteral( "A collection with this id is already registered" ),
      DiagnosticSeverity::Error } } };
  }

  Impl::CollectionRecord record;
  record.id = id;
  record.displayName = request.displayName;
  record.metadata = request.metadata;
  m_impl->collections.push_back( std::move( record ) );
  m_impl->catalogGeneration++;
  emit collectionAdded( id );
  return CollectionCreateResult{ id, {} };
}

std::optional<CollectionSnapshot> DataManager::collection( CollectionId id ) const
{
  const auto it = m_impl->findCollection( id );
  if ( it == m_impl->collections.end() )
    return std::nullopt;
  // Reflect only children that still exist (a child reaped independently is
  // removed from the persisted list lazily on read).
  CollectionSnapshot snapshot;
  snapshot.id = it->id;
  snapshot.displayName = it->displayName;
  snapshot.metadata = it->metadata;
  for ( const AssetId &childId : it->childAssetIds )
  {
    if ( m_impl->findRecord( childId ) != m_impl->records.end() )
      snapshot.childAssetIds.append( childId );
  }
  return snapshot;
}

QVector<CollectionId> DataManager::collections() const
{
  QVector<CollectionId> ids;
  ids.reserve( m_impl->collections.size() );
  for ( const Impl::CollectionRecord &c : m_impl->collections )
    ids.append( c.id );
  return ids;
}

TemporalCollectionCreateResult
DataManager::createTemporalCollection( const TemporalCollectionCreateRequest &request )
{
  // Affinity FIRST (like every other mutator): the dedup scan below reads
  // m_impl->temporalCollections, and a foreign-thread caller must get the
  // clean wrongThreadDiagnostic instead of racing the container (adversarial
  // review of #724; restoreTemporalCollection's own guard would only fire
  // after the scan).
  if ( QThread::currentThread() != thread() )
    return TemporalCollectionCreateResult{ {}, false, { wrongThreadDiagnostic() } };

  // Dedup: re-registering the same collection (e.g. an agent re-registering
  // the same descriptor) returns the existing record instead of spamming
  // identical entries into the workspace.
  for ( const Impl::TemporalCollectionRecord_ &existing : m_impl->temporalCollections )
  {
    if ( existing.displayName == request.displayName && existing.descriptor == request.descriptor )
    {
      TemporalCollectionCreateResult result;
      result.collectionId = existing.id;
      result.reusedExisting = true;
      return result;
    }
  }
  return restoreTemporalCollection( CollectionId::generate(), 1, request );
}

TemporalCollectionCreateResult
DataManager::restoreTemporalCollection( CollectionId id, quint64 revision,
                                        const TemporalCollectionCreateRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return TemporalCollectionCreateResult{ {}, false, { wrongThreadDiagnostic() } };

  if ( request.descriptor.trimmed().isEmpty() )
  {
    return TemporalCollectionCreateResult{ {}, false, { Diagnostic{
      QStringLiteral( "temporal_collection.empty_descriptor" ),
      QStringLiteral( "The temporal collection descriptor is empty" ),
      DiagnosticSeverity::Error } } };
  }

  // Guard against a duplicate id (double project read, stale extension),
  // mirroring restoreCollection's id-conflict handling.
  if ( m_impl->findTemporalCollection( id ) != m_impl->temporalCollections.end() )
  {
    return TemporalCollectionCreateResult{ {}, false, { Diagnostic{
      QStringLiteral( "temporal_collection.duplicate_id" ),
      QStringLiteral( "A temporal collection with this id is already registered" ),
      DiagnosticSeverity::Error } } };
  }

  Impl::TemporalCollectionRecord_ record;
  record.id = id;
  record.displayName = request.displayName;
  record.descriptor = request.descriptor;
  record.revision = revision > 0 ? revision : 1;
  m_impl->temporalCollections.push_back( std::move( record ) );
  m_impl->catalogGeneration++;
  emit temporalCollectionAdded( id );
  return TemporalCollectionCreateResult{ id, false, {} };
}

std::optional<TemporalCollectionRecord> DataManager::temporalCollection( CollectionId id ) const
{
  const auto it = m_impl->findTemporalCollection( id );
  if ( it == m_impl->temporalCollections.end() )
    return std::nullopt;
  TemporalCollectionRecord snapshot;
  snapshot.id = it->id;
  snapshot.displayName = it->displayName;
  snapshot.descriptor = it->descriptor;
  snapshot.revision = it->revision;
  snapshot.createdAtUtc = it->createdAtUtc;
  snapshot.updatedAtUtc = it->updatedAtUtc;
  return snapshot;
}

QVector<TemporalCollectionRecord> DataManager::temporalCollections() const
{
  QVector<TemporalCollectionRecord> snapshots;
  snapshots.reserve( m_impl->temporalCollections.size() );
  for ( const Impl::TemporalCollectionRecord_ &c : m_impl->temporalCollections )
  {
    TemporalCollectionRecord snapshot;
    snapshot.id = c.id;
    snapshot.displayName = c.displayName;
    snapshot.descriptor = c.descriptor;
    snapshot.revision = c.revision;
    snapshot.createdAtUtc = c.createdAtUtc;
    snapshot.updatedAtUtc = c.updatedAtUtc;
    snapshots.append( snapshot );
  }
  return snapshots;
}

Result<TemporalCollectionRecord>
DataManager::updateTemporalCollection( CollectionId id, const TemporalCollectionCreateRequest &request )
{
  if ( QThread::currentThread() != thread() )
    return Result<TemporalCollectionRecord>::failure( wrongThreadDiagnostic() );

  const auto it = m_impl->findTemporalCollection( id );
  if ( it == m_impl->temporalCollections.end() )
  {
    return Result<TemporalCollectionRecord>::failure(
      Diagnostic{ QStringLiteral( "temporal_collection.unknown" ),
                  QStringLiteral( "The temporal collection is not registered" ),
                  DiagnosticSeverity::Error } );
  }
  if ( request.descriptor.trimmed().isEmpty() )
  {
    return Result<TemporalCollectionRecord>::failure(
      Diagnostic{ QStringLiteral( "temporal_collection.empty_descriptor" ),
                  QStringLiteral( "The temporal collection descriptor is empty" ),
                  DiagnosticSeverity::Error } );
  }

  it->displayName = request.displayName;
  it->descriptor = request.descriptor;
  it->revision += 1;
  it->updatedAtUtc = QDateTime::currentDateTimeUtc();

  TemporalCollectionRecord snapshot;
  snapshot.id = it->id;
  snapshot.displayName = it->displayName;
  snapshot.descriptor = it->descriptor;
  snapshot.revision = it->revision;
  snapshot.createdAtUtc = it->createdAtUtc;
  snapshot.updatedAtUtc = it->updatedAtUtc;

  emit temporalCollectionChanged( id );
  return Result<TemporalCollectionRecord>::success( snapshot );
}

Result<void> DataManager::removeTemporalCollection( CollectionId id )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto it = m_impl->findTemporalCollection( id );
  if ( it == m_impl->temporalCollections.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "temporal_collection.unknown" ),
                  QStringLiteral( "The temporal collection is not registered" ),
                  DiagnosticSeverity::Error } );
  }
  m_impl->temporalCollections.erase( it );
  m_impl->catalogGeneration++;
  emit temporalCollectionRemoved( id );
  return Result<void>::success( {} );
}

Result<void> DataManager::addChildToCollection( CollectionId collectionId,
                                                AssetId childAssetId )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto collectionIt = m_impl->findCollection( collectionId );
  if ( collectionIt == m_impl->collections.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "collection.unknown" ),
                  QStringLiteral( "The collection is not registered" ),
                  DiagnosticSeverity::Error } );
  }

  const auto assetIt = m_impl->findRecord( childAssetId );
  if ( assetIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "collection.child_unknown" ),
                  QStringLiteral( "The child asset is not registered" ),
                  DiagnosticSeverity::Error } );
  }

  // A child has at most one parent collection (flat one-level pointer).
  // Reparenting would silently steal the child from its current collection and
  // leave a dangling entry there, so refuse instead.
  if ( assetIt->snapshot.parentCollectionId().has_value() &&
       assetIt->snapshot.parentCollectionId() != collectionId )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "collection.child_already_owned" ),
                  QStringLiteral( "The child asset already belongs to another collection" ),
                  DiagnosticSeverity::Error } );
  }

  // Duplicate child (#703): the serializer read-back re-adds persisted <child>
  // entries verbatim, so a duplicated persisted entry would append twice.
  // Re-adding a child already in THIS collection is a successful no-op.
  if ( collectionIt->childAssetIds.contains( childAssetId ) )
    return Result<void>::success();

  collectionIt->childAssetIds.append( childAssetId );
  assetIt->snapshot.m_parentCollectionId = collectionId;
  m_impl->catalogGeneration++;
  return Result<void>::success();
}

Result<void> DataManager::unloadCollection( CollectionId id, bool cascade )
{
  if ( QThread::currentThread() != thread() )
    return Result<void>::failure( wrongThreadDiagnostic() );

  const auto collectionIt = m_impl->findCollection( id );
  if ( collectionIt == m_impl->collections.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "collection.unknown" ),
                  QStringLiteral( "The collection is not registered" ),
                  DiagnosticSeverity::Error } );
  }

  if ( cascade )
  {
    // Refuse while any child holds an active lease, mirroring the asset
    // lease-safety rule. Reaped/already-removed children are skipped.
    for ( const AssetId &childId : collectionIt->childAssetIds )
    {
      if ( m_impl->findRecord( childId ) == m_impl->records.end() )
        continue;
      if ( !m_impl->leaseImpacts( childId ).isEmpty() )
      {
        return Result<void>::failure( leasedRefusalDiagnostics(
          QStringLiteral( "collection" ), m_impl->leaseImpacts( childId ) ) );
      }
    }

    // Refuse while any child is a strong-dependency INPUT consumed by a
    // dependent that lives OUTSIDE this collection. Cascading would erase the
    // child and prune the edge, silently orphaning the external dependent
    // (e.g. a Virtual Raster) - registered but broken. A dependent that is
    // itself a child of this collection is removed by the cascade below, so it
    // is internal and safe. (The inverse - a child that IS a dependent with
    // edges to external inputs - is also safe: pruning the edge is harmless.)
    const QVector<AssetId> children = collectionIt->childAssetIds;
    const auto isExternal = [&]( const AssetId &candidate ) {
      return !children.contains( candidate );
    };
    // Aggregate every external dependent across all children so a multi-child
    // refusal reports them all at once (mirrors unload.has_dependents), rather
    // than forcing a fix-and-retry cycle per dependent.
    QVector<Diagnostic> externalDependentDiagnostics;
    for ( const AssetId &childId : children )
    {
      if ( m_impl->findRecord( childId ) == m_impl->records.end() )
        continue;
      const QVector<AssetId> dependents = strongDependentsOf( childId );
      for ( const AssetId &dependent : dependents )
      {
        if ( isExternal( dependent ) )
        {
          externalDependentDiagnostics.append(
            Diagnostic{ QStringLiteral( "collection.has_external_dependents" ),
                        QStringLiteral( "The collection cannot be cascade-"
                                        "unloaded: child %1 is a strong "
                                        "dependency of %2 which lives outside "
                                        "the collection; unload the dependent "
                                        "first" )
                          .arg( childId.toString(), dependent.toString() ),
                        DiagnosticSeverity::Error } );
        }
      }
    }
    if ( !externalDependentDiagnostics.isEmpty() )
      return Result<void>::failure( externalDependentDiagnostics );

    // Cascade: unload each existing child first, then the collection node.
    // Emit before locating: a connected slot may re-enter the manager and
    // mutate the records (mirroring the DATAPY-7 revalidation in unload()).
    for ( const AssetId &childId : children )
    {
      emit assetAboutToUnload( childId );
      const auto childIt = m_impl->findRecord( childId );
      if ( childIt == m_impl->records.end() )
        continue;
      // A slot may have acquired a lease during the emit; revoking the fresh
      // list prevents a dangling LeaseRecord on the erased asset.
      for ( const LeaseImpact &impact : m_impl->leaseImpacts( childId ) )
        revokeLease( impact.lease );
      m_impl->records.erase( childIt );
      pruneChildFromCollections( childId );
      pruneDependencyEdgesOf( childId );
      m_impl->catalogGeneration++;
      emit assetRemoved( childId );
    }
  }
  else
  {
    // Non-cascade: children become standalone (clear their parent pointer).
    for ( const AssetId &childId : collectionIt->childAssetIds )
    {
      const auto childIt = m_impl->findRecord( childId );
      if ( childIt != m_impl->records.end() )
        childIt->snapshot.m_parentCollectionId = std::nullopt;
    }
  }

  const auto freshCollectionIt = m_impl->findCollection( id );
  if ( freshCollectionIt != m_impl->collections.end() )
  {
    m_impl->collections.erase( freshCollectionIt );
  }
  m_impl->catalogGeneration++;
  emit collectionRemoved( id );
  return Result<void>::success();
}

} // namespace sicnu::data
