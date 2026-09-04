#include "artifact_gc.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <mutex>
#include <unordered_set>

namespace sicnu::workflow {

namespace {

const QStringList kSidecarSuffixes = {
  QStringLiteral( ".shx" ), QStringLiteral( ".dbf" ), QStringLiteral( ".prj" ),
  QStringLiteral( ".cpg" ), QStringLiteral( ".sbn" ), QStringLiteral( ".sbx" ),
  QStringLiteral( ".qix" ), QStringLiteral( ".shp.xml" ), QStringLiteral( ".tfw" ),
  QStringLiteral( ".aux" ), QStringLiteral( ".aux.xml" )
};

std::mutex &protectionMutex()
{
  static std::mutex mutex;
  return mutex;
}

ArtifactGC::ProtectedArtifactProvider &protectionProvider()
{
  static ArtifactGC::ProtectedArtifactProvider provider;
  return provider;
}

} // namespace

void ArtifactGC::installProtectedArtifactProvider( ProtectedArtifactProvider provider )
{
  std::lock_guard<std::mutex> locker( protectionMutex() );
  protectionProvider() = std::move( provider );
}

QStringList ArtifactGC::protectedArtifacts()
{
  std::lock_guard<std::mutex> locker( protectionMutex() );
  if ( !protectionProvider() )
    return QStringList();
  return protectionProvider()();
}

const QStringList &ArtifactGC::sidecarSuffixes()
{
  return kSidecarSuffixes;
}

namespace {

/// Remove a single existing file via a two-phase rename-then-delete. Returns
/// the path on success, an empty string on failure (with the reason appended
/// to @a errors).
QString tryRemoveFile( const QString &path, QStringList *errors )
{
  if ( !QFile::exists( path ) )
    return QString();

  const QString staging = path + QStringLiteral( ".gctrash" );
  QFile::remove( staging );
  if ( QFile::rename( path, staging ) )
  {
    if ( QFile::remove( staging ) )
      return path;
    QFile::rename( staging, path ); // best-effort restore; direct retry below
  }

  QFile file( path );
  if ( file.exists() )
  {
    if ( file.remove() )
      return path;
    if ( errors )
      errors->append( QStringLiteral( "%1: %2" ).arg( path, file.errorString() ) );
    return QString();
  }

  if ( errors )
  {
    errors->append( QStringLiteral( "%1: staged delete failed and file could not be restored" ).arg( staging ) );
  }
  return QString();
}

bool isPathUnderAnyRoot( const QString &canonicalPath, const QSet<QString> &canonicalRoots )
{
  for ( const QString &root : canonicalRoots )
  {
    if ( canonicalPath == root || canonicalPath.startsWith( root + QLatin1Char( '/' ) ) )
      return true;
  }
  return false;
}

} // namespace

QStringList ArtifactGC::inspectReapable( const sicnu::workflow::WorkflowRun &run,
                                        bool retainFinalOutputs ) const
{
  // Only fully-completed runs may be swept. Failed/Canceled/Interrupted runs
  // keep their intermediates: retry and resume depend on them.
  if ( run.state() != sicnu::workflow::WorkflowRunState::Completed )
    return QStringList();

  const std::vector<sicnu::workflow::StepPlan> plans = run.stepPlans();
  if ( plans.empty() )
    return QStringList();

  // Determine dependencies: which steps are consumed as inputs by downstream steps
  std::unordered_set<std::string> consumedStepIds;
  for ( const auto &plan : plans )
  {
    for ( const auto &dep : plan.dependencies )
      consumedStepIds.insert( dep );
  }

  // Determine artifact steps
  std::unordered_set<std::string> artifactPaths;
  for ( const auto &[k, v] : run.artifacts() )
  {
    if ( !v.empty() )
      artifactPaths.insert( v );
  }

  // Pass 1: retained final outputs (DAG leaves and declared artifacts) define
  // the run workspace. Their canonical directories are the only roots under
  // which reaping is allowed; without at least one retained output there is
  // no established workspace and nothing is reaped.
  // Workspace roots (#697): when finals are retained they alone define the
  // workspace (a tampered intermediate pointing OUTSIDE it is then refused —
  // the gating test pins this). With retainFinalOutputs=false the flag used
  // to populate nothing and pass 2 reaped NOTHING (effectively inverted);
  // instead every Completed output's directory becomes a root and finals
  // reap alongside intermediates.
  QSet<QString> workspaceRoots;
  for ( const auto &plan : plans )
  {
    const QString outPath = QString::fromStdString( plan.outputLayerPath );
    if ( outPath.isEmpty() || !QFile::exists( outPath ) )
      continue;
    if ( plan.status != "Completed" )
      continue; // never derive the workspace from an unproduced output

    const bool isLeaf = ( consumedStepIds.find( plan.stepId ) == consumedStepIds.end() );
    const bool isArtifact = ( artifactPaths.find( plan.outputLayerPath ) != artifactPaths.end() );
    if ( retainFinalOutputs && !( isLeaf || isArtifact ) )
      continue; // retained mode: only the finals anchor the workspace

    const QFileInfo fi( outPath );
    const QString dir = QFileInfo( fi.absolutePath() ).canonicalFilePath();
    if ( !dir.isEmpty() )
      workspaceRoots.insert( dir );
  }
  if ( workspaceRoots.isEmpty() )
    return QStringList();

  // Pass 2: intermediates are reapable only when the step completed without a
  // cache hit and the file lies inside the workspace. Note that "final" is a
  // DAG property here (leaf / artifact), not array position: plan order is
  // not guaranteed to survive a checkpoint round-trip.
  // Cache lifecycle (#726): a file the execution result cache still claims is
  // what a future identical execution reuses — it must outlive this run's
  // finalization. The claim set is canonical-path matched so symlinked or
  // relative spellings of the same file are protected too.
  QSet<QString> cacheProtected;
  for ( const QString &protectedPath : protectedArtifacts() )
  {
    if ( protectedPath.isEmpty() )
      continue;
    const QString canonicalProtected = QFileInfo( protectedPath ).canonicalFilePath();
    cacheProtected.insert( canonicalProtected.isEmpty() ? QFileInfo( protectedPath ).absoluteFilePath()
                                                        : canonicalProtected );
  }
  auto isCacheProtected = [ &cacheProtected ]( const QString &path ) {
    const QString canonical = QFileInfo( path ).canonicalFilePath();
    return cacheProtected.contains( canonical.isEmpty() ? QFileInfo( path ).absoluteFilePath()
                                                        : canonical );
  };

  QStringList reapable;
  for ( const auto &plan : plans )
  {
    const QString outPath = QString::fromStdString( plan.outputLayerPath );
    if ( outPath.isEmpty() || !QFile::exists( outPath ) )
      continue;

    const bool isLeaf = ( consumedStepIds.find( plan.stepId ) == consumedStepIds.end() );
    const bool isArtifact = ( artifactPaths.find( plan.outputLayerPath ) != artifactPaths.end() );
    if ( retainFinalOutputs && ( isLeaf || isArtifact ) )
      continue; // retained final output (false: finals reap too, #697)

    if ( plan.status != "Completed" )
      continue; // not (fully) produced - retry may need it

    if ( plan.cacheHit )
      continue; // checkpoint-era flag: shared asset owned by the execution result cache

    if ( isCacheProtected( outPath ) )
      continue; // live cache claim (#726): the cache can still serve this artifact

    const QString canonical = QFileInfo( outPath ).canonicalFilePath();
    if ( canonical.isEmpty() || !isPathUnderAnyRoot( canonical, workspaceRoots ) )
      continue; // outside the run workspace - never delete

    reapable.append( outPath );
  }

  return reapable;
}

QStringList ArtifactGC::removeFilesWithSidecars( const QStringList &filePaths, QStringList *errors )
{
  QStringList removed;
  for ( const QString &path : filePaths )
  {
    if ( path.isEmpty() )
      continue;

    const QFileInfo fi( path );
    const QString base = fi.absolutePath() + QLatin1Char( '/' ) + fi.completeBaseName();

    for ( const QString &suffix : kSidecarSuffixes )
    {
      const QString sidecar = base + suffix;
      const QString removedSidecar = tryRemoveFile( sidecar, errors );
      if ( !removedSidecar.isEmpty() )
        removed.append( removedSidecar );
    }

    const QString removedMain = tryRemoveFile( path, errors );
    if ( !removedMain.isEmpty() )
      removed.append( removedMain );
  }
  return removed;
}

GCSweepReport ArtifactGC::sweepRun( const sicnu::workflow::WorkflowRun &run,
                                    bool retainFinalOutputs )
{
  GCSweepReport report;
  const QStringList reapable = inspectReapable( run, retainFinalOutputs );

  const std::vector<sicnu::workflow::StepPlan> plans = run.stepPlans();
  for ( const auto &plan : plans )
  {
    const QString path = QString::fromStdString( plan.outputLayerPath );
    if ( !path.isEmpty() && QFile::exists( path ) )
    {
      if ( !reapable.contains( path ) )
        report.retainedFiles.append( path );
    }
  }

  if ( !reapable.isEmpty() )
  {
    report.reapedFiles = removeFilesWithSidecars( reapable, &report.errors );
    report.reapedCount = report.reapedFiles.size();
  }
  return report;
}

} // namespace sicnu::workflow
