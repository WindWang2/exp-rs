#include "artifact_gc.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <unordered_set>

namespace sicnu::processing {

namespace {

const QStringList kSidecarSuffixes = {
  QStringLiteral( ".shx" ), QStringLiteral( ".dbf" ), QStringLiteral( ".prj" ),
  QStringLiteral( ".cpg" ), QStringLiteral( ".sbn" ), QStringLiteral( ".sbx" ),
  QStringLiteral( ".qix" ), QStringLiteral( ".shp.xml" ), QStringLiteral( ".tfw" ),
  QStringLiteral( ".aux" ), QStringLiteral( ".aux.xml" )
};

} // namespace

ArtifactGC::ArtifactGC( sicnu::data::DataManager *dataManager )
  : m_dataManager( dataManager )
{
}

QStringList ArtifactGC::inspectReapable( const sicnu::workflow::WorkflowRun &run,
                                        bool retainFinalOutputs ) const
{
  const auto &plans = run.getStepPlans();
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
  for ( const auto &[k, v] : run.getArtifacts() )
  {
    if ( !v.empty() )
      artifactPaths.insert( v );
  }

  QStringList reapable;
  for ( size_t i = 0; i < plans.size(); ++i )
  {
    const auto &plan = plans[i];
    const QString outPath = QString::fromStdString( plan.outputLayerPath );
    if ( outPath.isEmpty() || !QFile::exists( outPath ) )
      continue;

    const bool isLeaf = ( consumedStepIds.find( plan.stepId ) == consumedStepIds.end() );
    const bool isArtifact = ( artifactPaths.find( plan.outputLayerPath ) != artifactPaths.end() );
    const bool isLastStep = ( i == plans.size() - 1 );

    if ( retainFinalOutputs && ( isLeaf || isArtifact || isLastStep ) )
    {
      continue; // Retain final output
    }

    reapable.append( outPath );
  }

  return reapable;
}

QStringList ArtifactGC::removeFilesWithSidecars( const QStringList &filePaths )
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
      if ( QFile::exists( sidecar ) )
      {
        if ( QFile::remove( sidecar ) )
          removed.append( sidecar );
      }
    }

    if ( QFile::exists( path ) )
    {
      if ( QFile::remove( path ) )
        removed.append( path );
    }
  }
  return removed;
}

GCSweepReport ArtifactGC::sweepRun( const sicnu::workflow::WorkflowRun &run,
                                    bool retainFinalOutputs )
{
  GCSweepReport report;
  const QStringList reapable = inspectReapable( run, retainFinalOutputs );

  const auto &plans = run.getStepPlans();
  for ( const auto &plan : plans )
  {
    const QString path = QString::fromStdString( plan.outputLayerPath );
    if ( !path.isEmpty() && QFile::exists( path ) )
    {
      if ( !reapable.contains( path ) )
        report.retainedFiles.append( path );
    }
  }

  report.reapedFiles = removeFilesWithSidecars( reapable );
  report.reapedCount = report.reapedFiles.size();
  return report;
}

} // namespace sicnu::processing
