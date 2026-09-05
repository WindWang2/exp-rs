#include "ogr_vector_source_provider.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

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

bool sourceSupportsEditing( const QString &path )
{
  if ( !QFileInfo( path ).isWritable() )
    return false;

  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDatasetH dataset =
    GDALOpenEx( path.toUtf8().constData(),
                GDAL_OF_VECTOR | GDAL_OF_UPDATE,
                nullptr,
                nullptr,
                nullptr );
  CPLPopErrorHandler();
  if ( dataset == nullptr )
    return false;

  bool editable = false;
  const int layerCount = GDALDatasetGetLayerCount( dataset );
  for ( int layerIndex = 0; layerIndex < layerCount; ++layerIndex )
  {
    OGRLayerH layer = GDALDatasetGetLayer( dataset, layerIndex );
    if ( layer != nullptr &&
         ( OGR_L_TestCapability( layer, OLCSequentialWrite ) ||
           OGR_L_TestCapability( layer, OLCRandomWrite ) ) )
    {
      editable = true;
      break;
    }
  }
  GDALClose( dataset );
  return editable;
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
  resolved.canonicalProviderKey = QStringLiteral( "ogr" );
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
                          AssetCapability::Relocatable;

  VectorStructure structure;
  if ( GDALDriverH driver = GDALGetDatasetDriver( dataset ) )
    structure.driverName = QString::fromUtf8( GDALGetDriverShortName( driver ) );
  structure.layerCount = GDALDatasetGetLayerCount( dataset );
  structure.layers.reserve( structure.layerCount );

  for ( int layerIndex = 0; layerIndex < structure.layerCount; ++layerIndex )
  {
    OGRLayerH layer = GDALDatasetGetLayer( dataset, layerIndex );
    if ( layer == nullptr )
      continue;

    VectorLayerStructure layerStructure;
    layerStructure.name = QString::fromUtf8( OGR_L_GetName( layer ) );
    layerStructure.featureCount = OGR_L_GetFeatureCount( layer, false );
    layerStructure.geometryType =
      QString::fromUtf8( OGRGeometryTypeToName( OGR_L_GetGeomType( layer ) ) );

    if ( OGRSpatialReferenceH spatialReference = OGR_L_GetSpatialRef( layer ) )
    {
      char *wkt = nullptr;
      if ( OSRExportToWkt( spatialReference, &wkt ) == OGRERR_NONE && wkt != nullptr )
        layerStructure.crsWkt = QString::fromUtf8( wkt );
      CPLFree( wkt );
    }

    OGREnvelope envelope;
    // bForce=true: drivers like GeoJSON do not cache extents; a full scan is
    // cheap for provider resolution and required on GDAL 3.8 (CI).
    if ( OGR_L_GetExtent( layer, &envelope, true ) == OGRERR_NONE )
    {
      layerStructure.extent = SpatialExtent{ envelope.MinX,
                                             envelope.MinY,
                                             envelope.MaxX,
                                             envelope.MaxY,
                                             true };
    }

    if ( layerIndex == 0 )
      resolved.displayName = OGR_L_GetName( layer );
    structure.layers.append( std::move( layerStructure ) );
  }
  resolved.structure = std::move( structure );

  GDALClose( dataset );
  if ( sourceSupportsEditing( normalizedPath ) )
    resolved.capabilities |= AssetCapability::EditableFeatures;
  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
