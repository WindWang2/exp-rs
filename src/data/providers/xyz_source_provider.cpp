#include "xyz_source_provider.h"

namespace sicnu::data::providers
{

namespace
{

Diagnostic diagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

/// Parse a `zMin`/`zMax` option pair (defaulting to 0/0 when absent or invalid).
std::pair<int, int> parseZRange( const QMap<QString, QString> &options )
{
  bool ok = false;
  const int zMin = options.value( QStringLiteral( "zMin" ) ).toInt( &ok );
  const int parsedMin = ok ? zMin : 0;
  const int zMax = options.value( QStringLiteral( "zMax" ) ).toInt( &ok );
  const int parsedMax = ok ? zMax : 0;
  return { parsedMin, parsedMax };
}

} // namespace

XyzSourceProvider::XyzSourceProvider( const internal::NetworkProbe *probe )
  : m_probe( probe )
{
}

bool XyzSourceProvider::supports( const SourceDescriptor &source ) const
{
  // XYZ must be claimed explicitly by provider key — a generic URL is never
  // silently treated as a tile service (a http(s) path could be a WMS, a
  // downloadable raster, or nothing of the sort).
  return source.providerKey == QStringLiteral( "xyz" );
}

Result<internal::ResolvedSource> XyzSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  if ( source.canonicalSource.isEmpty() )
  {
    // A claimed XYZ descriptor with no URL template is malformed (unlike a
    // transiently-unreachable service, which resolves Offline). Use a distinct
    // code from the registry's "source.unsupported" so the failure is
    // attributable to the descriptor, not to provider selection.
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "source.invalid" ),
                  QStringLiteral( "An XYZ tile service requires a URL template" ) ) );
  }

  // Use the injected probe, or the conservative Offline default when the host
  // has not wired one (keeps src/data network-free).
  const internal::NoNetworkProbe noNetworkFallback;
  const internal::NetworkProbe &probe =
    m_probe != nullptr ? *m_probe : noNetworkFallback;

  const internal::ProbeOutcome outcome =
    probe.probe( RemoteMapService::Xyz, source.canonicalSource, source.dataOptions );

  internal::ResolvedSource resolved;
  resolved.kind = AssetKind::RemoteMap;
  resolved.state = outcome.state;
  resolved.storageKind = StorageKind::Remote;
  // Canonical identity is the template as given; the provider key is fixed.
  resolved.canonicalSource = source.canonicalSource;
  resolved.canonicalProviderKey = QStringLiteral( "xyz" );
  // Honest capabilities for a web map: renderable + cacheable only. A remote
  // map never claims pixel readback, statistics, or feature query (parent
  // spec line 109) even though QGIS represents it with a QgsRasterLayer.
  resolved.capabilities = AssetCapability::Renderable | AssetCapability::OfflineCacheable;
  resolved.displayName = QStringLiteral( "XYZ tiles" );

  RemoteMapStructure structure = outcome.structure;
  structure.service = RemoteMapService::Xyz;
  const auto [zMin, zMax] = parseZRange( source.dataOptions );
  structure.zMin = zMin;
  structure.zMax = zMax;
  resolved.structure = structure;

  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
