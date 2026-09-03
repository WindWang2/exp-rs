#include "gdal_raster_source_provider.h"

#include <algorithm>
#include <array>

#include <QDir>
#include <QFileInfo>
#include <QUrl>
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

/// True for network-hosted raster sources that GDAL opens through its /vsi*
/// virtual-file layer or an http(s) URL directly. Remote identity must never
/// touch QFileInfo: on Windows a "/vsicurl/..." string is not absolute and
/// absoluteFilePath() would silently prepend a drive letter, corrupting the
/// source (the local-existence probe then misclassified it as Missing).
bool isRemoteRasterSource( const QString &path )
{
  if ( path.isEmpty() )
    return false;
  if ( path.startsWith( QStringLiteral( "/vsi" ), Qt::CaseInsensitive ) )
    return true; // /vsicurl/, /vsis3/, /vsigs/, /vsiaz/, /vsihdfs/ ...
  return path.startsWith( QStringLiteral( "http://" ), Qt::CaseInsensitive ) ||
         path.startsWith( QStringLiteral( "https://" ), Qt::CaseInsensitive );
}

/// Remote sources open through /vsicurl/: a bare http(s) href is prefixed.
QString normalizeRemoteRasterPath( const QString &path )
{
  if ( path.startsWith( QStringLiteral( "http://" ), Qt::CaseInsensitive ) ||
       path.startsWith( QStringLiteral( "https://" ), Qt::CaseInsensitive ) )
    return QStringLiteral( "/vsicurl/" ) + path;
  return path;
}

/// One-time process defaults for remote reads: bounded timeouts/retries so a
/// dead host fails in seconds, never overriding an explicit user setting.
void configureRemoteHttpDefaults()
{
  static const struct { const char *key; const char *value; } kDefaults[] = {
    { "GDAL_HTTP_TIMEOUT", "30" },
    { "GDAL_HTTP_CONNECT_TIMEOUT", "10" },
    { "GDAL_HTTP_MAX_RETRY", "3" },
    { "GDAL_HTTP_RETRY_DELAY", "1" },
    { "GDAL_HTTP_VERSION", "2" },
  };
  for ( const auto &d : kDefaults )
  {
    if ( !CPLGetConfigOption( d.key, nullptr ) )
      CPLSetConfigOption( d.key, d.value );
  }
}

/// Canonicalize a source path: follow symlinks, collapse `.`/`..`, and rewrite
/// an ENVI `.hdr` selection to its paired binary data file. This is the identity
/// that drives SourceKey deduplication. Remote sources bypass QFileInfo
/// entirely (see isRemoteRasterSource).
QString normalizeRasterPath( const QString &rawPath )
{
  if ( rawPath.isEmpty() )
    return rawPath;

  if ( isRemoteRasterSource( rawPath ) )
    return normalizeRemoteRasterPath( rawPath );

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

  // Remote raster sources (STAC COGs etc.): same extension contract, but the
  // QFileInfo suffix read on a URL string is harmless while the local
  // existence probe in resolve() is not.
  if ( isRemoteRasterSource( source.canonicalSource ) )
    return rasterExtensions().contains( suffix );

  // Extensionless ENVI binary (GF_1 + GF_1.HDR) is also a local raster.
  return suffix.isEmpty() && hasEnviHeaderSibling( source.canonicalSource );
}

Result<internal::ResolvedSource> GdalRasterSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  const QString normalizedPath = normalizeRasterPath( source.canonicalSource );
  const bool remote = isRemoteRasterSource( normalizedPath );

  internal::ResolvedSource resolved;
  resolved.kind = AssetKind::Raster;
  resolved.storageKind = remote ? StorageKind::Remote : StorageKind::File;
  resolved.canonicalSource = normalizedPath;
  resolved.canonicalProviderKey = QStringLiteral( "gdal" );
  resolved.displayName = remote ? QUrl( normalizedPath ).fileName()
                                : QFileInfo( normalizedPath ).completeBaseName();

  if ( !remote )
  {
    const QFileInfo info( normalizedPath );
    if ( !info.exists() || !info.isFile() )
    {
      resolved.state = AssetState::Missing;
      resolved.capabilities = AssetCapability::Relocatable;
      return Result<internal::ResolvedSource>::success( std::move( resolved ) );
    }
  }

  // Suppress GDAL's stderr noise during probing; we report Missing/Error ourselves.
  ensureGdalRuntime();
  if ( remote )
    configureRemoteHttpDefaults();

  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDatasetH dataset = GDALOpen( normalizedPath.toUtf8().constData(), GA_ReadOnly );
  CPLPopErrorHandler();

  if ( dataset == nullptr )
  {
    // A remote source that cannot be opened right now is Missing (unreachable
    // host, expired link), not a hard Error: display re-resolves on demand.
    resolved.state = remote ? AssetState::Missing : AssetState::Error;
    resolved.capabilities = AssetCapability::Relocatable;
    return Result<internal::ResolvedSource>::success(
      std::move( resolved ),
      { Diagnostic{ QStringLiteral( "source.unreadable" ),
                    remote ? QStringLiteral( "GDAL could not open the remote raster source" )
                           : QStringLiteral( "GDAL could not open the raster source" ),
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
