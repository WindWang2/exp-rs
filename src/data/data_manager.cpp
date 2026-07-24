#include "data_manager.h"

#include <algorithm>
#include <utility>

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
  AssetSnapshot snapshot{ id,
                          AssetRevision::initial(),
                          normalizedDescriptor,
                          source.kind,
                          source.state,
                          source.capabilities,
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

  const quint64 token = m_impl->nextLeaseToken++;
  auto control = std::make_shared<internal::AssetLeaseControl>(
    internal::AssetLeaseControl{ asset.id, token, use.kind, use.purpose, this, true } );
  m_impl->leases.push_back( Impl::LeaseRecord{ control } );
  m_impl->catalogGeneration++;

  return Result<AssetLease>::success( AssetLease{ std::move( control ) } );
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
    Diagnostic block{ QStringLiteral( "unload.leased" ),
                      QStringLiteral( "The asset is referenced by %1 active lease(s)" )
                        .arg( liveLeaseImpacts.size() ),
                      DiagnosticSeverity::Error };
    QVector<Diagnostic> diagnostics{ std::move( block ) };
    for ( const LeaseImpact &impact : liveLeaseImpacts )
    {
      diagnostics.append(
        Diagnostic{ QStringLiteral( "unload.lease" ),
                    QStringLiteral( "Held by %1 lease" )
                      .arg( impact.lease.kind == LeaseKind::View
                              ? QStringLiteral( "a view" )
                              : impact.lease.kind == LeaseKind::Task
                                  ? QStringLiteral( "a task" )
                                  : QStringLiteral( "an edit" ) ),
                    DiagnosticSeverity::Info } );
    }
    return Result<void>::failure( std::move( diagnostics ) );
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
