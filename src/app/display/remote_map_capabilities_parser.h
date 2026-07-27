#pragma once

#include <QByteArray>

#include "data/data_asset.h"

namespace sicnu::display
{

/// Parse a WMS GetCapabilities XML document into the registration-essential
/// RemoteMapStructure fields (layerNames, crsList, imageFormat, extent). A PURE
/// function (no QGIS, no network) so the bulk of the #66 probe's logic is
/// unit-testable against fixture XML strings.
///
/// Handles WMS 1.3.0 (`<CRS>` + `<EX_GeographicBoundingBox>`) and 1.1.1
/// (`<SRS>` + `<LatLonBoundingBox>`) — the two encodings real services ship.
/// Returns a structure with `valid == false` on malformed or non-WMS XML.
///
/// The `service` field is left at its default (the probe stamps it from the
/// requesting provider, matching the NetworkProbe contract). WMS TileMatrixSet
/// concerns do not apply (WMS has none).
data::RemoteMapStructure parseWmsCapabilities( const QByteArray &xml );

/// Parse a WMTS GetCapabilities XML document. The first #66 slice extracts
/// reachability + the layer's `<ows:Identifier>` + the advertised tile
/// `<Format>` only. TileMatrixSet parsing (pixelSizeX/Y from ScaleDenominator,
/// service-discovered z-range) is a documented follow-up: a WMTS asset still
/// registers Ready with the fields parsed here, and its z-range comes from the
/// caller's options until the TileMatrix parse lands.
///
/// Returns a structure with `valid == false` on malformed or non-WMTS XML.
data::RemoteMapStructure parseWmtsCapabilities( const QByteArray &xml );

} // namespace sicnu::display
