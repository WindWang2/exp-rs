#include "data_manager.h"

#include <utility>

#include "internal/source_provider_registry.h"

namespace sicnu::data
{

struct DataManager::Impl
{
  struct AssetRecord
  {
    SourceKey sourceKey;
    AssetSnapshot snapshot;
  };

  explicit Impl( std::unique_ptr<internal::SourceProviderRegistry> sourceProviders )
    : providers( std::move( sourceProviders ) )
  {
  }

  std::unique_ptr<internal::SourceProviderRegistry> providers;
  QVector<AssetRecord> records;
};

DataManager::DataManager( QObject *parent )
  : DataManager( std::make_unique<internal::SourceProviderRegistry>(), parent )
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
  const SourceKey sourceKey = request.source.sourceKey();
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.sourceKey == sourceKey )
      return RegisterResult{ record.snapshot.id(), true, {} };
  }

  const Result<internal::ResolvedSource> resolved = m_impl->providers->resolve( request.source );
  if ( !resolved )
    return RegisterResult{ {}, false, resolved.diagnostics() };

  const AssetId id = AssetId::generate();
  const internal::ResolvedSource &source = resolved.value();
  AssetSnapshot snapshot{ id,
                          AssetRevision::initial(),
                          request.source,
                          source.kind,
                          source.state,
                          source.capabilities,
                          request.persistence,
                          source.storageKind,
                          source.displayName };
  m_impl->records.push_back( Impl::AssetRecord{ sourceKey, std::move( snapshot ) } );

  emit assetAdded( id );
  return RegisterResult{ id, false, resolved.diagnostics() };
}

std::optional<AssetSnapshot> DataManager::asset( AssetId id ) const
{
  for ( const Impl::AssetRecord &record : m_impl->records )
  {
    if ( record.snapshot.id() == id )
      return record.snapshot;
  }

  return std::nullopt;
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

} // namespace sicnu::data
