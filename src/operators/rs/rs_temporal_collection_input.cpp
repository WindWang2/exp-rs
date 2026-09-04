// src/operators/rs/rs_temporal_collection_input.cpp
#include "rs_temporal_collection_input.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/algorithms/temporal/temporal_workspace.h"

#include <algorithm>
#include "operators/framework/rs_operator_error.h"

#include <QFile>
#include <QUuid>

#include "data/data_manager.h"

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
  TemporalCollection collection;
  QString err;
  if ( !TemporalCollection::fromInlineScenes(
         params["scenes"], &collection, &err,
         params.isMember( "times" ) ? params["times"] : Json::Value(),
         params.isMember( "bands" ) ? params["bands"] : Json::Value(),
         QStringLiteral( "inline" ) ) )
  {
    throw RSOperatorError( ErrorCode::InvalidParameter, err.toStdString() );
  }
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
    // A workspace record id addresses a TemporalCollection registered in the
    // DataManager (project-persistent, revision-identifiable). It takes
    // precedence over the file-path reading because it carries provenance
    // identity; a UUID that does not resolve is a hard error (never a silent
    // reinterpretation as a relative path).
    const QUuid workspaceId( descriptorPath.trimmed() );
    if ( !workspaceId.isNull() )
    {
      const auto id = sicnu::data::CollectionId::fromString( descriptorPath.trimmed() );
      sicnu::data::DataManager *catalog = temporal::workspaceCatalog();
      if ( !id || !catalog )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "'collection' looks like a workspace id but no workspace "
                               "catalog is wired" );
      QString wsErr;
      if ( !temporal::loadCollectionFromWorkspace( *catalog, *id, &collection, &wsErr ) )
        throw RSOperatorError( ErrorCode::InvalidInputData, wsErr.toStdString() );
    }
    else
    {
      if ( !QFile::exists( descriptorPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                               "collection descriptor not found: " + descriptorPath.toStdString() );
      QString err;
      if ( !TemporalCollection::load( descriptorPath, &collection, &err ) )
        throw RSOperatorError( ErrorCode::InvalidInputData, err.toStdString() );
    }
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
