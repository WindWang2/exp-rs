#pragma once

#include <QMap>
#include <QString>

namespace sicnu::display
{

/// Build the QGIS provider URI for a remote-map asset, per service family, from
/// the provider key, the canonical source (service base URL or tile template),
/// the caller's dataOptions, and an already-auth-configured base URI. This is a
/// PURE function (no QGIS, no auth resolution) so it can be unit-tested against
/// expected URI strings for WMS/WMTS/TMS/XYZ.
///
/// Encoding:
/// - WMS/WMTS: a `key=value&...` query string (layers/layer, crs, format,
///   tileMatrixSet, url) — QGIS's wms/wmts provider parses these.
/// - XYZ/TMS: the tile template, with the declared zMin/zMax appended
///   (`?zmin=...&zmax=...`) so the service's declared range is honored at
///   display time (spec: "the tile template with z-range").
QString buildRemoteMapUri( const QString &providerKey,
                           const QString &canonicalSource,
                           const QMap<QString, QString> &dataOptions );

} // namespace sicnu::display
