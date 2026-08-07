#include "gdal_raster_source_provider.h"

#include <algorithm>
#include <array>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cpl_conv.h>
#include <gdal.h>

#include "data/band_role.h"
#include "gdal_runtime.h"

namespace sicnu::data::providers
{

namespace
{

/// Raster file extensions GDAL handles locally. `.hdr` is admitted because it is
/// the ENVI sidecar users select; the data file is resolved below.
const QStringList &rasterExtensions()
{
  static const QStringList extensions{
    QStringLiteral( "tif" ),  QStringLiteral( "tiff" ), QStringLiteral( "img" ),
    QStringLiteral( "vrt" ),  QStringLiteral( "jp2" ),  QStringLiteral( "png" ),
    QStringLiteral( "jpg" ),  QStringLiteral( "jpeg" ), QStringLiteral( "asc" ),
    QStringLiteral( "dat" ),  QStringLiteral( "hdr" ),  QStringLiteral( "bil" ),
    QStringLiteral( "bsq" ),  QStringLiteral( "bip" ),  QStringLiteral( "raw" ),
    QStringLiteral( "nc" ),   QStringLiteral( "hdf" ),  QStringLiteral( "h5" ),
    QStringLiteral( "kea" ),
  };
  return extensions;
}

/// True when `path` (typically extensionless) has an ENVI `.hdr`/`.HDR` sibling.
/// Matches the GF_1 + GF_1.HDR layout that users open without an extension.
bool hasEnviHeaderSibling( const QString &path )
{
  const QFileInfo info( path );
  const QString absolute = info.absoluteFilePath();
  const QString stem = QDir( info.absolutePath() ).filePath( info.fileName() );
  return QFile::exists( absolute + QStringLiteral( ".hdr" ) ) ||
         QFile::exists( absolute + QStringLiteral( ".HDR" ) ) ||
         QFile::exists( stem + QStringLiteral( ".hdr" ) ) ||
         QFile::exists( stem + QStringLiteral( ".HDR" ) );
}

/// Resolve the ENVI binary data file for a `.hdr` selection. A `.hdr` is only a
/// sidecar; GDAL opens the paired binary. Returns the original path unchanged
/// when no sibling data file exists (GDAL will then report the open error).
QString resolveEnviDataPath( const QString &path )
{
  const QFileInfo info( path );
  if ( info.suffix().toLower() != QStringLiteral( "hdr" ) &&
       info.suffix().toLower() != QStringLiteral( "HDR" ) )
  {
    return path;
  }

  const QString dir = info.absolutePath();
  const QString base = info.completeBaseName(); // strips the .hdr suffix
  const QStringList candidates{
    QDir( dir ).filePath( base ),
    QDir( dir ).filePath( base + QStringLiteral( ".dat" ) ),
    QDir( dir ).filePath( base + QStringLiteral( ".img" ) ),
    QDir( dir ).filePath( base + QStringLiteral( ".bil" ) ),
    QDir( dir ).filePath( base + QStringLiteral( ".bsq" ) ),
    QDir( dir ).filePath( base + QStringLiteral( ".bip" ) ),
    QDir( dir ).filePath( base + QStringLiteral( ".raw" ) ),
  };
  for ( const QString &candidate : candidates )
  {
    const QFileInfo candidateInfo( candidate );
    if ( candidateInfo.exists() && candidateInfo.isFile() )
      return candidateInfo.absoluteFilePath();
  }
  return path;
}

/// Canonicalize a source path: follow symlinks, collapse `.`/`..`, and rewrite
/// an ENVI `.hdr` selection to its paired binary data file. This is the identity
/// that drives SourceKey deduplication.
QString normalizeRasterPath( const QString &rawPath )
{
  if ( rawPath.isEmpty() )
    return rawPath;

  const QString enviResolved = resolveEnviDataPath( rawPath );
  const QFileInfo info( enviResolved );
  return info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                            : info.canonicalFilePath();
}

} // namespace

bool GdalRasterSourceProvider::supports( const SourceDescriptor &source ) const
{
  // An explicit provider hint must match this adapter, or be left empty.
  if ( !source.providerKey.isEmpty() &&
       source.providerKey != QStringLiteral( "gdal" ) &&
       source.providerKey != QStringLiteral( "raster" ) )
  {
    return false;
  }

  const QFileInfo info( source.canonicalSource );
  const QString suffix = info.suffix().toLower();
  if ( rasterExtensions().contains( suffix ) )
    return true;

  // Extensionless ENVI binary (GF_1 + GF_1.HDR) is also a local raster.
  return suffix.isEmpty() && hasEnviHeaderSibling( source.canonicalSource );
}

Result<internal::ResolvedSource> GdalRasterSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  const QString normalizedPath = normalizeRasterPath( source.canonicalSource );

  internal::ResolvedSource resolved;
  resolved.kind = AssetKind::Raster;
  resolved.storageKind = StorageKind::File;
  resolved.canonicalSource = normalizedPath;
  resolved.canonicalProviderKey = QStringLiteral( "gdal" );
  resolved.displayName = QFileInfo( normalizedPath ).completeBaseName();

  const QFileInfo info( normalizedPath );
  if ( !info.exists() || !info.isFile() )
  {
    resolved.state = AssetState::Missing;
    resolved.capabilities = AssetCapability::Relocatable;
    return Result<internal::ResolvedSource>::success( std::move( resolved ) );
  }

  // Suppress GDAL's stderr noise during probing; we report Missing/Error ourselves.
  ensureGdalRuntime();

  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDatasetH dataset = GDALOpen( normalizedPath.toUtf8().constData(), GA_ReadOnly );
  CPLPopErrorHandler();

  if ( dataset == nullptr )
  {
    resolved.state = AssetState::Error;
    resolved.capabilities = AssetCapability::Relocatable;
    return Result<internal::ResolvedSource>::success(
      std::move( resolved ),
      { Diagnostic{ QStringLiteral( "source.unreadable" ),
                    QStringLiteral( "GDAL could not open the raster source" ),
                    DiagnosticSeverity::Warning } } );
  }

  // Structural metadata only — no histogram/statistics computed here.
  resolved.state = AssetState::Ready;
  resolved.capabilities = AssetCapability::Renderable | AssetCapability::ReadablePixels |
                          AssetCapability::BandMetadata | AssetCapability::BandStatistics |
                          AssetCapability::Relocatable;

  RasterStructure structure;
  if ( GDALDriverH driver = GDALGetDatasetDriver( dataset ) )
    structure.driverName = QString::fromUtf8( GDALGetDriverShortName( driver ) );
  structure.width = GDALGetRasterXSize( dataset );
  structure.height = GDALGetRasterYSize( dataset );
  structure.bandCount = GDALGetRasterCount( dataset );
  structure.crsWkt = QString::fromUtf8( GDALGetProjectionRef( dataset ) );

  structure.hasGeoTransform =
    GDALGetGeoTransform( dataset, structure.geoTransform.data() ) == CE_None;
  if ( structure.hasGeoTransform )
  {
    const auto corner = [&]( double pixelX, double pixelY ) {
      const auto &transform = structure.geoTransform;
      return std::array<double, 2>{
        transform[0] + pixelX * transform[1] + pixelY * transform[2],
        transform[3] + pixelX * transform[4] + pixelY * transform[5] };
    };
    const std::array<std::array<double, 2>, 4> corners{
      corner( 0.0, 0.0 ),
      corner( structure.width, 0.0 ),
      corner( 0.0, structure.height ),
      corner( structure.width, structure.height ),
    };
    structure.extent.minimumX = corners.front()[0];
    structure.extent.maximumX = corners.front()[0];
    structure.extent.minimumY = corners.front()[1];
    structure.extent.maximumY = corners.front()[1];
    for ( const auto &point : corners )
    {
      structure.extent.minimumX = std::min( structure.extent.minimumX, point[0] );
      structure.extent.maximumX = std::max( structure.extent.maximumX, point[0] );
      structure.extent.minimumY = std::min( structure.extent.minimumY, point[1] );
      structure.extent.maximumY = std::max( structure.extent.maximumY, point[1] );
    }
    structure.extent.valid = true;
  }

  structure.bands.reserve( structure.bandCount );
  for ( int bandNumber = 1; bandNumber <= structure.bandCount; ++bandNumber )
  {
    GDALRasterBandH band = GDALGetRasterBand( dataset, bandNumber );
    if ( band == nullptr )
      continue;

    RasterBandStructure bandStructure;
    bandStructure.number = bandNumber;
    bandStructure.dataType =
      QString::fromUtf8( GDALGetDataTypeName( GDALGetRasterDataType( band ) ) );
    int hasNoData = 0;
    const double noData = GDALGetRasterNoDataValue( band, &hasNoData );
    if ( hasNoData )
      bandStructure.noDataValue = noData;
    bandStructure.colorInterpretation = QString::fromUtf8(
      GDALGetColorInterpretationName( GDALGetRasterColorInterpretation( band ) ) );
    // Product-stacked rasters carry the semantic role as band metadata; plain
    // rasters leave it Unknown.
    const char *roleItem = GDALGetMetadataItem( band, "SICNU_BAND_ROLE", nullptr );
    if ( roleItem && roleItem[0] )
      bandStructure.role = bandRoleFromString( QString::fromUtf8( roleItem ) );
    structure.bands.append( std::move( bandStructure ) );
  }
  resolved.structure = std::move( structure );

  GDALClose( dataset );
  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
