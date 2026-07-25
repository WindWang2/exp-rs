#include "tms_source_provider.h"

#include "remote_map_provider_common.h"

namespace sicnu::data::providers
{

TmsSourceProvider::TmsSourceProvider( const internal::NetworkProbe *probe )
  : m_probe( probe )
{
}

bool TmsSourceProvider::supports( const SourceDescriptor &source ) const
{
  return source.providerKey == QStringLiteral( "tms" );
}

Result<internal::ResolvedSource> TmsSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  return assembleRemoteMapResolved( source, RemoteMapService::Tms,
                                    QStringLiteral( "tms" ),
                                    QStringLiteral( "TMS tiles" ), m_probe );
}

} // namespace sicnu::data::providers
