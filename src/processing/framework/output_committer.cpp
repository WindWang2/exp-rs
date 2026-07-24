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
#include "framework/task_center.h"
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
  // share a filesystem.
  if ( QFile::exists( request.stablePath ) )
    QFile::remove( request.stablePath );

  const QFileInfo stableInfo( request.stablePath );
  QDir().mkpath( stableInfo.absolutePath() );

  if ( !QFile::rename( request.tempPath, request.stablePath ) )
  {
    return CommitResult::failure( diagnostic(
      QStringLiteral( "output.publish_failed" ),
      QStringLiteral( "Failed to publish output to %1 (is the temp path on a "
                      "different filesystem from the stable path?)" )
        .arg( request.stablePath ) ) );
  }

  SourceDescriptor source;
  source.providerKey = providerKeyFor( request.kind );
  source.canonicalSource = request.stablePath;

  RegisterRequest registration;
  registration.source = source;
  registration.persistence = request.persistence;

  const RegisterResult registered = m_dataManager->registerSource( registration );
  if ( registered.assetId.isNull() )
  {
    // Registration failed AFTER the atomic publish succeeded. To honour
    // "the catalog never holds an apparently-valid output from an incomplete
    // task", roll the publish back: remove the just-published stable file so
    // there is no orphaned, apparently-valid output and nothing is registered.
    QFile::remove( request.stablePath );

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

CommitResult OutputCommitter::commitTaskOutput( TaskCenter *taskCenter,
                                                long taskId,
                                                AssetKind kind,
                                                const QString &stablePath,
                                                PersistencePolicy persistence,
                                                bool autoLoad,
                                                const DerivationRecord &derivation )
{
  Q_ASSERT( taskCenter != nullptr );
  const AlgorithmTaskInfo task = taskCenter->getTaskInfo( taskId );

  // Only a successfully completed task may register an output. A failed or
  // cancelled task performs no publish, no registration, and no Derivation
  // Record — its caller discards the temporary output instead.
  if ( task.status != TaskStatus::Completed )
  {
    return CommitResult::failure( diagnostic(
      QStringLiteral( "output.task_not_completed" ),
      QStringLiteral( "Task %1 is not completed (status=%2); refusing to commit "
                      "an output from an incomplete task" )
        .arg( QString::number( taskId ) ) ) );
  }

  if ( task.outputLayerPath.isEmpty() )
  {
    return CommitResult::failure( diagnostic(
      QStringLiteral( "output.no_output" ),
      QStringLiteral( "Task %1 completed without an OUTPUT-keyed result path" )
        .arg( QString::number( taskId ) ) ) );
  }

  AlgorithmOutputRequest request;
  request.kind = kind;
  request.tempPath = task.outputLayerPath;
  request.stablePath = stablePath;
  request.persistence = persistence;
  request.autoLoad = autoLoad;
  request.derivation = derivation;
  request.derivation.taskReference = QString::number( taskId );
  return commit( request );
}

void OutputCommitter::discardTemporary( const QString &tempPath )
{
  if ( !tempPath.isEmpty() )
    QFile::remove( tempPath );
}

} // namespace sicnu
