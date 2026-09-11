// execution_identity_bridge.cpp — geospatial-backed input identity resolver.
#include "execution_identity_bridge.h"

#include "execution_identity_resolver.h"

#include "geospatial/remote/remote_identity_token.h"

namespace sicnu::data
{

namespace
{

/// QString seam → Qt-free geospatial token. Empty in ⇒ empty out; ANY probe
/// failure (offline, no strong validator, non-remote spelling such as
/// /vsimem/ or OGR connection strings — the probe throws InvalidArgument for
/// those) folds into "" = uncacheable. The admission path must never see an
/// exception from identity work.
QString geospatialRemoteIdentity( const QString &canonicalPath )
{
  const std::string path = canonicalPath.toStdString();
  if ( path.empty() )
    return QString();
  try
  {
    // Shortened probe budget: fingerprinting runs on the submission path and
    // may cover several inputs; a dead origin must stall admission briefly,
    // not for the full interactive budget. (Remote inputs are usually
    // registered and never reach the resolver — this is the fallback path.)
    sicnu::geo::RemoteIdentityTokenOptions options;
    options.timeoutSeconds = 5;
    options.connectTimeoutSeconds = 3;
    options.maxRetries = 0;
    const std::string token = sicnu::geo::remoteIdentityToken( path, options );
    return token.empty() ? QString() : QString::fromStdString( token );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    return QString();
  }
}

} // namespace

InputIdentityResolver *installGeospatialInputIdentityResolver()
{
  return setExecutionIdentityResolver( InputIdentityResolver( geospatialRemoteIdentity ) );
}

} // namespace sicnu::data
