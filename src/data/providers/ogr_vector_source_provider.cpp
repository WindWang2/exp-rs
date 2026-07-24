#include "ogr_vector_source_provider.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_api.h>

#include "gdal_runtime.h"

namespace sicnu::data::providers
{

namespace
{

const QStringList &vectorExtensions()
{
  static const QStringList extensions{
    QStringLiteral( "geojson" ), QStringLiteral( "json" ),
    QStringLiteral( "shp" ),     QStringLiteral( "gpkg" ),
    QStringLiteral( "kml" ),     QStringLiteral( "gml" ),
    QStringLiteral( "tab" ),     QStringLiteral( "mif" ),
    QStringLiteral( "csv" ),     QStringLiteral( "geoparquet" ),
  };
  return extensions;
}

QString normalizeVectorPath( const QString &rawPath )
{
  if ( rawPath.isEmpty() )
    return rawPath;

  const QFileInfo info( rawPath );
  return info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                            : info.canonicalFilePath();
}

} // namespace

bool OgrVectorSourceProvider::supports( const SourceDescriptor &source ) const
{
  if ( !source.providerKey.isEmpty() &&
       source.providerKey != QStringLiteral( "ogr" ) &&
       source.providerKey != QStringLiteral( "vector" ) )
  {
    return false;
  }

  const QString suffix = QFileInfo( source.canonicalSource ).suffix().toLower();
  return vectorExtensions().contains( suffix );
}

Result<internal::ResolvedSource> OgrVectorSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  const QString normalizedPath = normalizeVectorPath( source.canonicalSource );

  internal::ResolvedSource resolved;
  resolved.kind = AssetKind::Vector;
  resolved.storageKind = StorageKind::File;
  resolved.canonicalSource = normalizedPath;
  resolved.displayName = QFileInfo( normalizedPath ).completeBaseName();

  const QFileInfo info( normalizedPath );
  if ( !info.exists() || !info.isFile() )
  {
    resolved.state = AssetState::Missing;
    resolved.capabilities = AssetCapability::Relocatable;
    return Result<internal::ResolvedSource>::success( std::move( resolved ) );
  }

  ensureGdalRuntime();

  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDatasetH dataset =
    GDALOpenEx( normalizedPath.toUtf8().constData(),
                GDAL_OF_VECTOR | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr );
  CPLPopErrorHandler();

  if ( dataset == nullptr )
  {
    resolved.state = AssetState::Error;
    resolved.capabilities = AssetCapability::Relocatable;
    return Result<internal::ResolvedSource>::success(
      std::move( resolved ),
      { Diagnostic{ QStringLiteral( "source.unreadable" ),
                    QStringLiteral( "OGR could not open the vector source" ),
                    DiagnosticSeverity::Warning } } );
  }

  resolved.state = AssetState::Ready;
  resolved.capabilities = AssetCapability::Renderable | AssetCapability::QueryableFeatures |
                          AssetCapability::EditableFeatures | AssetCapability::Relocatable;

  // Structural metadata only: layer count and total feature count. We do not
  // read geometries or attributes here.
  const int layerCount = GDALDatasetGetLayerCount( dataset );
  if ( layerCount > 0 )
  {
    OGRLayerH layer = GDALDatasetGetLayer( dataset, 0 );
    if ( layer != nullptr )
      resolved.displayName = OGR_L_GetName( layer );
  }

  GDALClose( dataset );
  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
