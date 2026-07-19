#include "rs_classification_project.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
constexpr int kCurrentVersion = 1;

QString modeOrDefault( const QString &mode )
{
  if ( mode == QLatin1String( "expert" ) )
    return QStringLiteral( "expert" );
  // Empty or anything else → wizard (default).
  if ( mode.isEmpty() || mode == QLatin1String( "wizard" ) )
    return QStringLiteral( "wizard" );
  return mode;
}
} // namespace

bool RsClassificationProject::save( const QString &path,
                                    const RsClassificationProjectData &data )
{
  if ( path.isEmpty() )
    return false;

  QJsonObject root;
  root.insert( QStringLiteral( "version" ),
               data.version > 0 ? data.version : kCurrentVersion );
  root.insert( QStringLiteral( "workflowStep" ), data.workflowStep );
  root.insert( QStringLiteral( "workflowMode" ), modeOrDefault( data.workflowMode ) );
  root.insert( QStringLiteral( "sourceRasterPath" ), data.sourceRasterPath );
  root.insert( QStringLiteral( "roisPath" ), data.roisPath );
  root.insert( QStringLiteral( "classifiedRasterPath" ), data.classifiedRasterPath );
  root.insert( QStringLiteral( "postProcessRasterPath" ), data.postProcessRasterPath );
  root.insert( QStringLiteral( "postProcessVectorPath" ), data.postProcessVectorPath );
  root.insert( QStringLiteral( "evaluateReviewed" ), data.evaluateReviewed );
  root.insert( QStringLiteral( "accuracySource" ), data.accuracySource );
  if ( data.overallAccuracy >= 0.0 )
    root.insert( QStringLiteral( "overallAccuracy" ), data.overallAccuracy );
  if ( data.kappa >= 0.0 )
    root.insert( QStringLiteral( "kappa" ), data.kappa );

  QFile f( path );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  f.write( QJsonDocument( root ).toJson( QJsonDocument::Indented ) );
  return true;
}

bool RsClassificationProject::load( const QString &path,
                                    RsClassificationProjectData &data )
{
  // Reset to defaults so missing keys stay backward-compatible.
  data = RsClassificationProjectData{};

  if ( path.isEmpty() )
    return false;

  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return false;

  const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
  if ( !doc.isObject() )
    return false;

  const QJsonObject root = doc.object();

  data.version = root.value( QStringLiteral( "version" ) ).toInt( kCurrentVersion );
  data.workflowStep = root.value( QStringLiteral( "workflowStep" ) ).toInt( 0 );
  data.workflowMode = root.value( QStringLiteral( "workflowMode" ) ).toString();
  if ( data.workflowMode.isEmpty() )
    data.workflowMode = QStringLiteral( "wizard" );

  data.sourceRasterPath =
    root.value( QStringLiteral( "sourceRasterPath" ) ).toString();
  data.roisPath = root.value( QStringLiteral( "roisPath" ) ).toString();
  data.classifiedRasterPath =
    root.value( QStringLiteral( "classifiedRasterPath" ) ).toString();
  data.postProcessRasterPath =
    root.value( QStringLiteral( "postProcessRasterPath" ) ).toString();
  data.postProcessVectorPath =
    root.value( QStringLiteral( "postProcessVectorPath" ) ).toString();

  data.evaluateReviewed =
    root.value( QStringLiteral( "evaluateReviewed" ) ).toBool( false );
  data.accuracySource =
    root.value( QStringLiteral( "accuracySource" ) ).toString();

  if ( root.contains( QStringLiteral( "overallAccuracy" ) ) )
    data.overallAccuracy =
      root.value( QStringLiteral( "overallAccuracy" ) ).toDouble( -1.0 );
  if ( root.contains( QStringLiteral( "kappa" ) ) )
    data.kappa = root.value( QStringLiteral( "kappa" ) ).toDouble( -1.0 );

  return true;
}
