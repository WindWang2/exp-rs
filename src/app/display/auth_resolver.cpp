#include "auth_resolver.h"

#include <qgsapplication.h>
#include <qgsauthmanager.h>

namespace sicnu::display
{

namespace {

/// Append the QGIS auth-cfg parameter to `uri` for the given config id. QGIS's
/// remote providers (wms/wmts/xyz/tms) read `authcfg` from the data-source URI
/// and resolve it through QgsAuthManager — so this is the only place credential
/// material is referenced, and it is referenced BY ID, never by value.
QString withAuthConfigParameter( const QString &uri, const QString &authConfigId )
{
  const QChar separator = uri.contains( QChar( '?' ) ) ? QChar( '&' ) : QChar( '?' );
  return uri + separator + QStringLiteral( "authcfg=" ) + authConfigId;
}

/// True when `authConfigId` is a known auth config in the QGIS auth manager.
/// Returns true when the auth manager is unavailable (host not wired) so a
/// valid-looking id is not falsely rejected in a stripped-down environment;
/// the host-side wiring (#66) guarantees the manager is initialized in
/// production. An empty id is an open service and is never looked up.
bool authConfigExists( const QString &authConfigId )
{
  if ( authConfigId.isEmpty() )
    return true;
  const QgsAuthManager *manager = QgsApplication::authManager();
  if ( manager == nullptr )
    return true;
  return manager->configIds().contains( authConfigId );
}

} // namespace

data::Result<QString> QgisAuthResolver::applyAuthConfig( const QString &authConfigId,
                                                          const QString &providerKey,
                                                          const QString &uri ) const
{
  ( void ) providerKey;
  // An open service (no auth config declared) needs no injection.
  if ( authConfigId.isEmpty() )
    return data::Result<QString>::success( uri );

  // A non-empty id must reference a known auth config; a missing/invalid one
  // is a hard failure so the caller surfaces AuthenticationRequired rather than
  // materializing an unauthenticated layer (the load-bearing contract).
  if ( !authConfigExists( authConfigId ) )
  {
    return data::Result<QString>::failure(
      data::Diagnostic{ QStringLiteral( "auth.config_unknown" ),
                        QStringLiteral( "The auth config '%1' is not registered "
                                        "with the QGIS auth manager" )
                          .arg( authConfigId ),
                        data::DiagnosticSeverity::Error } );
  }

  // The credential material is NEVER returned — only the auth-cfg id rides on
  // the URI; QgsAuthManager resolves the id to the actual credential at the
  // provider layer, below this seam.
  return data::Result<QString>::success( withAuthConfigParameter( uri, authConfigId ) );
}

bool QgisAuthResolver::hasAuthConfig( const QString &authConfigId ) const
{
  return authConfigExists( authConfigId );
}

QStringList QgisAuthResolver::knownAuthConfigIds() const
{
  const QgsAuthManager *manager = QgsApplication::authManager();
  if ( manager == nullptr )
    return {};
  return manager->configIds();
}

} // namespace sicnu::display

