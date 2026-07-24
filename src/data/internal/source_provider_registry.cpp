#include "source_provider_registry.h"

#include <utility>

#include "../data_manager.h"

namespace sicnu::data::internal
{

void SourceProviderRegistry::add( std::unique_ptr<SourceProvider> provider )
{
  m_providers.push_back( std::move( provider ) );
}

Result<ResolvedSource> SourceProviderRegistry::resolve( const SourceDescriptor &source ) const
{
  for ( const auto &provider : m_providers )
  {
    if ( provider->supports( source ) )
      return provider->resolve( source );
  }

  return Result<ResolvedSource>::failure(
    Diagnostic{ QStringLiteral( "source.unsupported" ),
                QStringLiteral( "No data source provider supports this source" ),
                DiagnosticSeverity::Error } );
}

std::unique_ptr<DataManager> SourceProviderRegistry::createDataManager()
{
  return std::unique_ptr<DataManager>(
    new DataManager( std::make_unique<SourceProviderRegistry>( std::move( *this ) ) ) );
}

} // namespace sicnu::data::internal
