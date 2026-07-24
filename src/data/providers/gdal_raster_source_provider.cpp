#include "gdal_raster_source_provider.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cpl_conv.h>
#include <gdal.h>

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

  GDALClose( dataset );
  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
