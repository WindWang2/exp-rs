#include "wmts_source_provider.h"

#include "remote_map_provider_common.h"

namespace sicnu::data::providers
{

WmtsSourceProvider::WmtsSourceProvider( const internal::NetworkProbe *probe )
  : m_probe( probe )
{
}

bool WmtsSourceProvider::supports( const SourceDescriptor &source ) const
{
  return source.providerKey == QStringLiteral( "wmts" );
}

Result<internal::ResolvedSource> WmtsSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  return assembleRemoteMapResolved( source, RemoteMapService::Wmts,
                                    QStringLiteral( "wmts" ),
                                    QStringLiteral( "WMTS service" ), m_probe );
}

} // namespace sicnu::data::providers
