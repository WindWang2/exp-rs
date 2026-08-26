#include "output_committer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_api.h>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"
#include "gdal/gdal_dataset_wrapper.h"

using namespace sicnu::data;

namespace sicnu
{

namespace
{

Diagnostic diagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

/// Provider key a registered source uses for `kind`. A single point that maps
/// AssetKind to its registration key, so the kind cannot be dispatched
/// inconsistently across the committer.
QString providerKeyFor( AssetKind kind )
{
  switch ( kind )
  {
    case AssetKind::Raster:
    case AssetKind::VirtualRaster:
    case AssetKind::RemoteMap:
      return QStringLiteral( "gdal" );
    case AssetKind::Vector:
      return QStringLiteral( "ogr" );
  }
  Q_UNREACHABLE();
}

/// Human-readable kind label for diagnostics.
QString kindLabel( AssetKind kind )
{
  return kind == AssetKind::Vector ? QStringLiteral( "vector" )
                                   : QStringLiteral( "raster" );
}

/// GDAL open flags for validating `kind`.
unsigned int openFlagsFor( AssetKind kind )
{
  const bool isVector = kind == AssetKind::Vector;
  return GDAL_OF_READONLY | ( isVector ? GDAL_OF_VECTOR : GDAL_OF_RASTER );
}

/// Opens `path` with GDAL as a raster (OF_RASTER) or vector (OF_VECTOR)
/// dataset and reports whether it is a structurally valid, complete output.
bool isStructurallyOpenable( AssetKind kind, const QString &path )
{
  ensureGdalInit();

  GDALDatasetH dataset = GDALOpenEx( path.toUtf8().constData(), openFlagsFor( kind ),
                                     nullptr, nullptr, nullptr );
  if ( !dataset )
    return false;
  GDALClose( dataset );
  return true;
}

} // namespace

OutputCommitter::OutputCommitter( DataManager *dataManager, QObject *parent )
  : QObject( parent )
  , m_dataManager( dataManager )
{
  Q_ASSERT( m_dataManager != nullptr );
}

CommitResult OutputCommitter::commit( const AlgorithmOutputRequest &request )
{
  if ( !QFile::exists( request.tempPath ) )
  {
    return CommitResult::failure( diagnostic(
      QStringLiteral( "output.temp_missing" ),
      QStringLiteral( "Temporary output does not exist: %1" ).arg( request.tempPath ) ) );
  }

  if ( !isStructurallyOpenable( request.kind, request.tempPath ) )
  {
    return CommitResult::failure( diagnostic(
      QStringLiteral( "output.invalid" ),
      QStringLiteral( "Temporary output is not a structurally valid %1 dataset: %2" )
        .arg( kindLabel( request.kind ), request.tempPath ) ) );
  }

  // Atomically publish: remove any stale stable target, then move the validated
  // temp output into place. QFile::rename is atomic when source and destination
  // share a filesystem. Multi-file formats (shapefile .shx/.dbf/.prj/... sidecars
  // beside the primary) move with the primary so the published dataset keeps its
  // attributes and CRS (#462).
  const QFileInfo tempInfo( request.tempPath );
  const QFileInfo stableInfo( request.stablePath );
  QDir().mkpath( stableInfo.absolutePath() );

  // Collect the primary plus every same-stem sidecar that exists in the temp
  // directory (e.g. scratch.shx/.dbf/.prj/.cpg/.qix/.shp.xml for scratch.shp).
  struct PublishPair
  {
    QString from;
    QString to;
  };
  QVector<PublishPair> publishes;
  publishes.append( { request.tempPath, request.stablePath } );
  const QString tempBase = tempInfo.absolutePath() + QLatin1Char( '/' ) + tempInfo.completeBaseName();
  const QString stableBase = stableInfo.absolutePath() + QLatin1Char( '/' ) + stableInfo.completeBaseName();
  const QStringList sidecarSuffixes = { QStringLiteral( ".shx" ), QStringLiteral( ".dbf" ),
                                        QStringLiteral( ".prj" ), QStringLiteral( ".cpg" ),
                                        QStringLiteral( ".sbn" ), QStringLiteral( ".sbx" ),
                                        QStringLiteral( ".qix" ), QStringLiteral( ".shp.xml" ),
                                        QStringLiteral( ".tfw" ), QStringLiteral( ".aux" ) };
  for ( const QString &suffix : sidecarSuffixes )
  {
    const QString from = tempBase + suffix;
    if ( QFile::exists( from ) )
      publishes.append( { from, stableBase + suffix } );
  }

  // Stage the move: stale targets away first, then move/copy each file.
  // Supports cross-filesystem boundaries (QFile::rename fallback to copy+remove).
  // A failure midway rolls everything back to keep the stable tree free of
  // half-published datasets.
  auto moveOrCopy = []( const QString &from, const QString &to ) -> bool {
    if ( QFile::rename( from, to ) )
      return true;
    if ( QFile::copy( from, to ) )
    {
      QFile::remove( from );
      return true;
    }
    return false;
  };

  QStringList renamed;
  for ( const PublishPair &pair : publishes )
  {
    if ( QFile::exists( pair.to ) )
      QFile::remove( pair.to );
    if ( !moveOrCopy( pair.from, pair.to ) )
    {
      for ( const QString &done : renamed )
        QFile::remove( done );
      return CommitResult::failure( diagnostic(
        QStringLiteral( "output.publish_failed" ),
        QStringLiteral( "Failed to publish output to %1" )
          .arg( request.stablePath ) ) );
    }
    renamed.append( pair.to );
  }

  SourceDescriptor source;
  source.providerKey = providerKeyFor( request.kind );
  source.canonicalSource = request.stablePath;

  RegisterRequest registration;
  registration.source = source;
  registration.persistence = request.persistence;
  // The committer owns the published stable path, so the resulting asset may
  // be reaped (catalog removal + file deletion). DeletableSource is the
  // capability that gates physical deletion at reap time.
  registration.additionalCapabilities = AssetCapability::DeletableSource;

  const RegisterResult registered = m_dataManager->registerSource( registration );
  if ( registered.assetId.isNull() )
  {
    // Registration failed AFTER the atomic publish succeeded. To honour
    // "the catalog never holds an apparently-valid output from an incomplete
    // task", roll the publish back: remove the just-published files (primary
    // + sidecars, #462) so there is no orphaned, apparently-valid output and
    // nothing is registered.
    for ( const QString &published : renamed )
      QFile::remove( published );

    const QVector<Diagnostic> detail = registered.diagnostics.isEmpty()
      ? QVector<Diagnostic>{ diagnostic(
          QStringLiteral( "output.register_failed" ),
          QStringLiteral( "Registering the output as a Data Asset failed; the "
                          "publish was rolled back" ) ) }
      : registered.diagnostics;
    return CommitResult::failure( detail );
  }

  // Registration emitted `assetAdded` once (registerSource guarantees it).
  // Provenance is the final step: surface an attach failure as a warning
  // diagnostic alongside the otherwise-successful registration, so provenance
  // can't vanish silently.
  QVector<Diagnostic> warnings;
  const Result<void> attached =
    m_dataManager->attachDerivationRecord( registered.assetId, request.derivation );
  if ( !attached )
  {
    warnings.append( Diagnostic{ QStringLiteral( "output.provenance_missing" ),
                                 QStringLiteral( "Output was registered but its "
                                                 "Derivation Record could not be "
                                                 "attached" ),
                                 DiagnosticSeverity::Warning } );
  }

  if ( request.autoLoad )
    emit displayRequested( registered.assetId );

  return CommitResult::success( registered.assetId, warnings );
}

// commitTaskOutput is defined in output_committer_task_center.cpp (linked via
// sicnu_task_center) so sicnu_processing does not take a hard dependency on
// TaskCenter / JobEngine symbols.

void OutputCommitter::discardTemporary( const QString &tempPath )
{
  if ( !tempPath.isEmpty() )
  {
    QFile::remove( tempPath );
    const QFileInfo fi( tempPath );
    const QString base = fi.path() + QDir::separator() + fi.completeBaseName();
    const QStringList sidecarExts = {
      QStringLiteral( "shx" ),
      QStringLiteral( "dbf" ),
      QStringLiteral( "prj" ),
      QStringLiteral( "cpg" ),
      QStringLiteral( "sbn" ),
      QStringLiteral( "sbx" ),
      QStringLiteral( "qix" ),
      QStringLiteral( "shp.xml" ),
      QStringLiteral( "tfw" ),
      QStringLiteral( "aux" )
    };
    for ( const QString &ext : sidecarExts )
    {
      const QString sidecarPath = base + QStringLiteral( "." ) + ext;
      if ( QFile::exists( sidecarPath ) )
      {
        QFile::remove( sidecarPath );
      }
    }
    const QString auxXml = tempPath + QStringLiteral( ".aux.xml" );
    if ( QFile::exists( auxXml ) )
    {
      QFile::remove( auxXml );
    }
  }
}

} // namespace sicnu
