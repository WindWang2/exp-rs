#pragma once

#include <QString>

#include "data/data_result.h"

namespace sicnu::display
{

/// Injectable seam that applies a Data Asset's `authConfigId` to the URI a
/// remote-map layer is built from, at materialization time. This is the first
/// first-party touch of `QgsAuthManager`.
///
/// Credential discipline (load-bearing constraint): the asset carries ONLY an
/// `authConfigId` — never credential material. The resolver produces a single
/// configured URI string carrying the auth-cfg parameter (via
/// `QgsDataSourceUri::setAuthConfigId` / the provider's auth-cfg convention);
/// it NEVER returns a password/secret to the caller. The QGIS layer receives
/// credentials through the existing `QgsAuthManager` path, never through a
/// field on the asset.
///
/// The default implementation is `QgsAuthManager`-backed; tests inject a stub.
class AuthResolver
{
  public:
    virtual ~AuthResolver() = default;

    /// Apply `authConfigId` to `uri` for the given `providerKey`, returning the
    /// configured URI the QGIS layer is built from. An empty `authConfigId`
    /// returns `uri` unchanged (an open service). A missing/invalid config
    /// returns failure so the caller can surface an AuthenticationRequired
    /// state rather than materialize an unauthenticated layer.
    virtual data::Result<QString> applyAuthConfig( const QString &authConfigId,
                                                    const QString &providerKey,
                                                    const QString &uri ) const = 0;
};

/// The production resolver: applies the auth config through `QgsAuthManager` /
/// `QgsDataSourceUri`. Never returns credential material.
class QgisAuthResolver final : public AuthResolver
{
  public:
    data::Result<QString> applyAuthConfig( const QString &authConfigId,
                                            const QString &providerKey,
                                            const QString &uri ) const override;
};

} // namespace sicnu::display
