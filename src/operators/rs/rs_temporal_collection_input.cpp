// src/operators/rs/rs_temporal_collection_input.cpp
#include "rs_temporal_collection_input.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"

#include <algorithm>
#include "operators/framework/rs_operator_error.h"

#include <QFile>

namespace sicnu::operators::rs::temporal_input
{

using namespace params;
using temporal::TemporalCollection;
using temporal::TemporalSceneRef;

namespace
{

void applyGlobalBandOverrides( TemporalCollection &collection, const Json::Value &bands )
{
  if ( !bands.isObject() || bands.empty() )
    return;
  for ( TemporalSceneRef &s : collection.scenes() )
  {
    for ( auto it = bands.begin(); it != bands.end(); ++it )
    {
      const QString role = QString::fromStdString( it.name() );
      const int band = ( *it ).asInt();
      if ( band > 0 )
        s.bandOverrides[role] = band;
    }
  }
}

TemporalCollection fromInlineScenes( const Json::Value &params )
{
  const Json::Value &scenesJson = params["scenes"];
  TemporalCollection collection;
  collection.setName( "inline" );
  const int sceneCount = static_cast<int>( scenesJson.size() );
  QVector<TemporalSceneRef> scenes;
  scenes.reserve( sceneCount );

  for ( int i = 0; i < sceneCount; ++i )
  {
    const Json::Value &entry = scenesJson[i];
    QString path;
    QString explicitTime;
    TemporalSceneRef s;

    if ( entry.isString() )
    {
      path = QString::fromStdString( entry.asString() );
    }
    else if ( entry.isObject() && entry.isMember( "path" ) && entry["path"].isString() )
    {
      path = QString::fromStdString( entry["path"].asString() );
      if ( entry.isMember( "time" ) && entry["time"].isString() )
        explicitTime = QString::fromStdString( entry["time"].asString() );
      if ( entry.isMember( "bands" ) && entry["bands"].isObject() )
      {
        for ( auto it = entry["bands"].begin(); it != entry["bands"].end(); ++it )
          s.bandOverrides[QString::fromStdString( it.name() )] = ( *it ).asInt();
      }
      if ( entry.isMember( "quality_band" ) )
        s.qualityBand = entry["quality_band"].asInt();
      if ( entry.isMember( "mask_band" ) )
        s.maskBand = entry["mask_band"].asInt();
      if ( entry.isMember( "asset_id" ) && entry["asset_id"].isString() )
        s.assetId = QString::fromStdString( entry["asset_id"].asString() );
      if ( entry.isMember( "asset_revision" ) && entry["asset_revision"].isString() )
        s.assetRevision = QString::fromStdString( entry["asset_revision"].asString() );
    }
    else
    {
      throw RSOperatorError( ErrorCode::TypeMismatch,
                             "scenes[" + std::to_string( i ) +
                                 "] must be a path string or an object with 'path'" );
    }

    if ( !fileExists( path.toStdString() ) )
      throw RSOperatorError( ErrorCode::FileNotFound,
                             "scene not found: " + path.toStdString() );

    QString err;
    if ( !temporal::inspectScene( path, explicitTime, &s, &err ) )
      throw RSOperatorError( ErrorCode::GdalError, err.toStdString() );
    if ( !s.time.valid && !explicitTime.isEmpty() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "invalid explicit time '" + explicitTime.toStdString() +
                                 "' for " + path.toStdString() );
    s.originalIndex = i;
    scenes.push_back( std::move( s ) );
  }

  // Optional parallel time list for the bare-path shorthand.
  if ( params.isMember( "times" ) && params["times"].isArray() && !params["times"].empty() )
  {
    if ( static_cast<int>( params["times"].size() ) != sceneCount )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "'times' length must match 'scenes' length" );
    for ( int i = 0; i < sceneCount; ++i )
    {
      const Json::Value &t = params["times"][i];
      if ( !t.isString() )
        throw RSOperatorError( ErrorCode::TypeMismatch, "times[] entries must be strings" );
      const QString iso = QString::fromStdString( t.asString() );
      if ( iso.isEmpty() )
        continue;
      scenes[i].time = temporal::parseAcquisitionTime( iso );
      scenes[i].timeSource = QStringLiteral( "explicit" );
      if ( !scenes[i].time.valid )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "invalid time '" + iso.toStdString() + "' for scene " +
                                   std::to_string( i ) );
    }
  }

  collection.scenes() = scenes;
  collection.sortScenes();
  applyGlobalBandOverrides( collection, params.isMember( "bands" ) ? params["bands"] : Json::Value() );
  return collection;
}

} // namespace

temporal::DuplicatePolicy parseDuplicatePolicy( const Json::Value &params )
{
  if ( !params.isMember( "duplicate_policy" ) )
    return temporal::DuplicatePolicy::KeepAll;
  bool ok = false;
  const auto policy = temporal::duplicatePolicyFromString(
    QString::fromStdString( getString( params, "duplicate_policy", "keep_all" ) ), &ok );
  if ( !ok )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "duplicate_policy must be 'keep_all' or 'reject'" );
  return policy;
}

TemporalCollection parseCollection( const Json::Value &params )
{
  if ( !params.isObject() )
    throw RSOperatorError( ErrorCode::InvalidParameter, "parameters must be a JSON object" );

  TemporalCollection collection;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
  {
    if ( params["scenes"].empty() )
      throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                             "'scenes' must contain at least one raster" );
    collection = fromInlineScenes( params );
  }
  else if ( params.isMember( "collection" ) && params["collection"].isString() )
  {
    const QString descriptorPath = QString::fromStdString( params["collection"].asString() );
    if ( !QFile::exists( descriptorPath ) )
      throw RSOperatorError( ErrorCode::FileNotFound,
                             "collection descriptor not found: " + descriptorPath.toStdString() );
    QString err;
    if ( !TemporalCollection::load( descriptorPath, &collection, &err ) )
      throw RSOperatorError( ErrorCode::InvalidInputData, err.toStdString() );
    applyGlobalBandOverrides( collection,
                              params.isMember( "bands" ) ? params["bands"] : Json::Value() );
  }
  else
  {
    throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                           "provide 'scenes' (array) or 'collection' (descriptor path)" );
  }

  const auto policy = parseDuplicatePolicy( params );
  collection.setDuplicatePolicy( policy );
  QStringList dropped;
  collection.applyDuplicatePolicy( policy, &dropped );
  if ( policy == temporal::DuplicatePolicy::Reject && !dropped.isEmpty() )
  {
    // The user chose 'reject' for explicitness — silently dropping scenes
    // would be the exact opposite. Fail with the offending acquisitions.
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "duplicate acquisition instants with duplicate_policy=reject: " +
                               dropped.join( QStringLiteral( ", " ) ).toStdString() );
  }
  return collection;
}

PreparedTemporalRun prepareTemporalRun( const Json::Value &params, RSOperatorContext &context,
                                        const std::vector<QString> &requiredRoles,
                                        const QString &analysisRole, int analysisBandOverride )
{
  PreparedTemporalRun run;
  run.collection = parseCollection( params );

  temporal::PreflightOptions options;
  for ( const QString &role : requiredRoles )
    if ( !role.isEmpty() )
      options.requiredBandRoles.push_back( role );
  if ( !analysisRole.isEmpty() && !options.requiredBandRoles.contains( analysisRole ) )
    options.requiredBandRoles.push_back( analysisRole );

  run.preflight = temporal::runPreflight( run.collection, options, analysisRole,
                                          analysisBandOverride );
  if ( !run.preflight.ok() )
  {
    const auto blocking = run.preflight.firstBlocking();
    const int blockingCount = static_cast<int>( std::count_if(
      run.preflight.issues.begin(), run.preflight.issues.end(),
      []( const temporal::PreflightIssue &i ) { return i.blocking; } ) );
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "temporal preflight failed [" + blocking.code.toStdString() + "]: " +
                               blocking.message.toStdString() + " (" +
                               std::to_string( blockingCount ) + " blocking issue(s))" );
  }
  for ( const auto &issue : run.preflight.issues )
  {
    if ( !issue.blocking )
      context.logWarning( "[temporal preflight] " + issue.code.toStdString() + ": " +
                          issue.message.toStdString() );
  }

  // NOTE: per-scene band numbers are resolved on the streaming reader (which
  // owns the open dataset handles) via TemporalTileReader::bandForRole —
  // re-opening every scene here would double the metadata pass.
  (void)analysisBandOverride;
  return run;
}

} // namespace sicnu::operators::rs::temporal_input
