#include "data_manager.h"

#include <algorithm>
#include <utility>

#include "internal/source_provider_registry.h"
#include "providers/gdal_raster_source_provider.h"
#include "providers/ogr_vector_source_provider.h"

namespace sicnu::data
{

struct DataManager::Impl
{
  struct AssetRecord
  {
    SourceKey sourceKey;
    AssetSnapshot snapshot;
  };

  struct LeaseRecord
  {
    AssetId assetId;
    quint64 token = 0;
    LeaseKind kind = LeaseKind::View;
    QString purpose;
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
                          source.displayName };
  m_impl->records.push_back( Impl::AssetRecord{ sourceKey, std::move( snapshot ) } );
  m_impl->catalogGeneration++;

  emit assetAdded( id );
  return RegisterResult{ id, false, resolved.diagnostics() };
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
  m_impl->leases.push_back(
    Impl::LeaseRecord{ asset.id, token, use.kind, use.purpose } );
  m_impl->catalogGeneration++;

  return Result<AssetLease>::success(
    AssetLease{ asset.id, token, use.kind, use.purpose, this } );
}

int DataManager::leaseCount( AssetId id ) const
{
  return static_cast<int>(
    std::count_if( m_impl->leases.begin(), m_impl->leases.end(),
                   [&]( const Impl::LeaseRecord &lease ) { return lease.assetId == id; } ) );
}

QVector<LeaseRef> DataManager::leases( AssetId id ) const
{
  QVector<LeaseRef> result;
  for ( const Impl::LeaseRecord &lease : m_impl->leases )
  {
    if ( lease.assetId == id )
      result.append( LeaseRef{ lease.assetId, lease.token, lease.kind } );
  }
  return result;
}

UnloadPlan DataManager::planUnload( AssetId id ) const
{
  UnloadPlan plan;
  plan.assetId = id;
  const auto recordIt = m_impl->findRecord( id );
  if ( recordIt != m_impl->records.end() )
    plan.revision = recordIt->snapshot.revision();
  plan.catalogGeneration = m_impl->catalogGeneration;

  for ( const Impl::LeaseRecord &lease : m_impl->leases )
  {
    if ( lease.assetId != id )
      continue;
    plan.activeLeases.append( LeaseImpact{ LeaseRef{ lease.assetId, lease.token, lease.kind },
                                           lease.purpose } );
  }
  return plan;
}

Result<void> DataManager::unload( const UnloadPlan &confirmedPlan )
{
  const auto recordIt = m_impl->findRecord( confirmedPlan.assetId );
  if ( recordIt == m_impl->records.end() )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "unload.unknown_asset" ),
                  QStringLiteral( "The asset is no longer registered" ),
                  DiagnosticSeverity::Error } );
  }

  if ( confirmedPlan.catalogGeneration != m_impl->catalogGeneration )
  {
    return Result<void>::failure(
      Diagnostic{ QStringLiteral( "unload.stale_plan" ),
                  QStringLiteral( "The unload plan was produced against an outdated catalog" ),
                  DiagnosticSeverity::Error } );
  }

  // Normal unload is rejected while any active lease exists. A confirmed cascade
  // plan revokes those leases atomically rather than silently dropping them.
  if ( !confirmedPlan.canUnload() )
  {
    Diagnostic block{ QStringLiteral( "unload.leased" ),
                      QStringLiteral( "The asset is referenced by %1 active lease(s)" )
                        .arg( confirmedPlan.activeLeases.size() ),
                      DiagnosticSeverity::Error };
    QVector<Diagnostic> diagnostics{ std::move( block ) };
    for ( const LeaseImpact &impact : confirmedPlan.activeLeases )
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

  emit assetAboutToUnload( confirmedPlan.assetId );

  if ( confirmedPlan.cascade )
  {
    for ( const LeaseImpact &impact : confirmedPlan.activeLeases )
      detachLease( impact.lease );
  }

  m_impl->records.erase( recordIt );
  m_impl->catalogGeneration++;

  emit assetRemoved( confirmedPlan.assetId );
  return Result<void>::success();
}

LeaseOutcome DataManager::releaseLease( const LeaseRef &lease )
{
  const auto it =
    std::find_if( m_impl->leases.begin(), m_impl->leases.end(),
                  [&]( const Impl::LeaseRecord &record ) { return record.token == lease.token; } );
  if ( it == m_impl->leases.end() )
    return LeaseOutcome::Invalid;

  m_impl->leases.erase( it );
  m_impl->catalogGeneration++;
  return LeaseOutcome::Released;
}

void DataManager::detachLease( const LeaseRef &lease )
{
  const auto it =
    std::find_if( m_impl->leases.begin(), m_impl->leases.end(),
                  [&]( const Impl::LeaseRecord &record ) { return record.token == lease.token; } );
  if ( it != m_impl->leases.end() )
    m_impl->leases.erase( it );
}

LeaseOutcome AssetLease::release()
{
  if ( m_manager == nullptr || m_token == 0 )
    return LeaseOutcome::Invalid;

  const LeaseOutcome outcome = m_manager->releaseLease( toRef() );
  m_manager = nullptr;
  m_token = 0;
  return outcome;
}

void AssetLease::detach()
{
  m_manager = nullptr;
  m_token = 0;
}

} // namespace sicnu::data
