#include "wms_source_provider.h"

#include "remote_map_provider_common.h"

namespace sicnu::data::providers
{

WmsSourceProvider::WmsSourceProvider( const internal::NetworkProbe *probe )
  : m_probe( probe )
{
}

bool WmsSourceProvider::supports( const SourceDescriptor &source ) const
{
  // Claimed only by explicit provider key — a generic service URL is never
  // silently treated as WMS.
  return source.providerKey == QStringLiteral( "wms" );
}

Result<internal::ResolvedSource> WmsSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  return assembleRemoteMapResolved( source, RemoteMapService::Wms,
                                    QStringLiteral( "wms" ),
                                    QStringLiteral( "WMS service" ), m_probe );
}

} // namespace sicnu::data::providers
