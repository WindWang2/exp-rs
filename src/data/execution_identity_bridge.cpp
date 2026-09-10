// execution_identity_bridge.cpp — geospatial-backed input identity resolver.
#include "execution_identity_bridge.h"

#include "execution_identity_resolver.h"

#include "geospatial/remote/remote_identity_token.h"

namespace sicnu::data
{

namespace
{

/// QString seam → Qt-free geospatial token. Empty in ⇒ empty out; any probe
/// failure (offline, no strong validator) is "" = uncacheable (fail-closed).
QString geospatialRemoteIdentity( const QString &canonicalPath )
{
  const std::string path = canonicalPath.toStdString();
  if ( path.empty() )
    return QString();
  const std::string token = sicnu::geo::remoteIdentityToken( path );
  return token.empty() ? QString() : QString::fromStdString( token );
}

} // namespace

InputIdentityResolver *installGeospatialInputIdentityResolver()
{
  return setExecutionIdentityResolver( InputIdentityResolver( geospatialRemoteIdentity ) );
}

} // namespace sicnu::data
