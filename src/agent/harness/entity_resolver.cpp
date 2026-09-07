// src/agent/harness/entity_resolver.cpp
#include "entity_resolver.h"

#include "agent/workspace_state.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>

namespace sicnu::agent::harness {

using sicnu::agent::AgentServices;
using sicnu::agent::WorkspaceEntityRegistry;

namespace {

bool looksLikePath( const QString &ref )
{
  if ( ref.startsWith( QLatin1Char( '/' ) ) || ref.startsWith( QLatin1Char( '\\' ) ) )
    return true;
  if ( ref.contains( QLatin1Char( '/' ) ) || ref.contains( QLatin1Char( '\\' ) ) )
    return true;
  // Drive letters (Windows) and VSI/remote schemes.
  if ( ref.size() >= 2 && ref[1] == QLatin1Char( ':' ) )
    return true;
  const QString lowered = ref.toLower();
  return lowered.startsWith( QStringLiteral( "/vsi" ) ) ||
         lowered.startsWith( QStringLiteral( "http://" ) ) ||
         lowered.startsWith( QStringLiteral( "https://" ) ) ||
         lowered.startsWith( QStringLiteral( "file://" ) ) ||
         lowered.startsWith( QStringLiteral( "pg:" ) );
}

bool fileExists( const QString &path )
{
  if ( path.isEmpty() )
    return false;
  const QString lowered = path.toLower();
  if ( lowered.startsWith( QStringLiteral( "/vsi" ) ) ||
       lowered.startsWith( QStringLiteral( "http://" ) ) ||
       lowered.startsWith( QStringLiteral( "https://" ) ) )
    return true; // remote references resolve at GDAL open time, not here
  return QFileInfo::exists( path );
}

sicnu::data::DataManager *dataManagerOrNull() { return AgentServices::instance().dataManager(); }

} // namespace

Json::Value ResolvedDataset::toJson() const
{
  Json::Value v( Json::objectValue );
  v["path"] = path.toStdString();
  if ( !canonicalPath.isEmpty() )
    v["canonical_path"] = canonicalPath.toStdString();
  if ( !assetId.isEmpty() )
    v["asset_id"] = assetId.toStdString();
  if ( !assetEntityId.isEmpty() )
    v["asset_entity_id"] = assetEntityId.toStdString();
  if ( !displayName.isEmpty() )
    v["display_name"] = displayName.toStdString();
  if ( revision > 0 )
    v["revision"] = static_cast<Json::Int64>( revision );
  v["resolution"] = resolution.toStdString();
  return v;
}

std::optional<ResolvedDataset> resolveDatasetRef( const QString &rawRef, HarnessError *error )
{
  const QString ref = rawRef.trimmed();
  const auto fail = [error]( const HarnessError &e ) -> std::optional<ResolvedDataset>
  {
    if ( error )
      *error = e;
    return std::nullopt;
  };

  if ( ref.isEmpty() )
    return fail( HarnessError::make( error_codes::kInvalidParameter,
                                     "Dataset reference is empty" ) );

  sicnu::data::DataManager *manager = dataManagerOrNull();

  // 1. Stable workspace entity id ("asset-3").
  if ( ref.contains( QLatin1Char( '-' ) ) && !looksLikePath( ref ) )
  {
    const QString naturalKey = WorkspaceEntityRegistry::instance().naturalKeyFor( ref );
    if ( !naturalKey.isEmpty() )
    {
      ResolvedDataset r;
      r.path = naturalKey;
      r.canonicalPath = naturalKey;
      r.assetEntityId = ref;
      r.resolution = QStringLiteral( "entity_id" );
      if ( manager )
      {
        if ( auto snapshot = manager->findByPath( naturalKey ) )
        {
          r.assetId = snapshot->id().toString();
          r.displayName = snapshot->displayName();
          r.revision = static_cast<long long>( snapshot->revision().value() );
        }
      }
      if ( !fileExists( r.path ) )
      {
        Json::Value details( Json::objectValue );
        details["entity_id"] = ref.toStdString();
        details["path"] = r.path.toStdString();
        return fail( HarnessError::make(
          error_codes::kDatasetNotFound, "Dataset behind entity id no longer exists on disk",
          details ) );
      }
      return r;
    }
  }

  // 2. Governed asset UUID.
  if ( !looksLikePath( ref ) && manager )
  {
    if ( const auto assetId = sicnu::data::AssetId::fromString( ref ) )
    {
      if ( auto snapshot = manager->asset( *assetId ) )
      {
        ResolvedDataset r;
        r.path = snapshot->source().canonicalSource;
        r.canonicalPath = r.path;
        r.assetId = snapshot->id().toString();
        r.displayName = snapshot->displayName();
        r.revision = static_cast<long long>( snapshot->revision().value() );
        r.assetEntityId = WorkspaceEntityRegistry::instance().idFor(
          QStringLiteral( "asset" ), r.path );
        r.resolution = QStringLiteral( "asset_uuid" );
        return r;
      }
      Json::Value details( Json::objectValue );
      details["asset_id"] = ref.toStdString();
      return fail( HarnessError::makeWithAction(
        error_codes::kDatasetNotFound, "No registered asset with this UUID",
        "project.search", Json::Value() ) );
    }
  }

  // 3. Concrete path: absolute, workspace-relative, or remote. Path-form
  // references resolve without a catalog so read-only grounding works on
  // files the caller already knows (never guessed — an explicit path *is*
  // the identification).
  if ( looksLikePath( ref ) )
  {
    QString candidate = ref;
    if ( !fileExists( candidate ) && QDir::isRelativePath( candidate ) )
    {
      // Workspace-relative path resolution is the caller's (MCP) policy;
      // here relative paths are tried as-is only.
    }
    if ( !fileExists( candidate ) )
    {
      Json::Value details( Json::objectValue );
      details["path"] = candidate.toStdString();
      return fail( HarnessError::make(
        error_codes::kDatasetNotFound, "File not found on disk", details ) );
    }
    ResolvedDataset r;
    r.path = candidate;
    r.canonicalPath = QDir( candidate ).canonicalPath();
    if ( r.canonicalPath.isEmpty() ) // remote/VSI paths canonicalize to empty
      r.canonicalPath = candidate;
    if ( manager )
    {
      if ( auto snapshot = manager->findByPath( r.canonicalPath ) )
      {
        r.assetId = snapshot->id().toString();
        r.displayName = snapshot->displayName();
        r.revision = static_cast<long long>( snapshot->revision().value() );
        r.assetEntityId = ref.startsWith( QLatin1String( "asset-" ) )
                            ? ref
                            : WorkspaceEntityRegistry::instance().idFor(
                                QStringLiteral( "asset" ), r.canonicalPath );
      }
    }
    if ( r.assetEntityId.isEmpty() )
      r.assetEntityId = WorkspaceEntityRegistry::instance().idFor(
        QStringLiteral( "asset" ), r.canonicalPath );
    r.resolution = QStringLiteral( "path" );
    return r;
  }

  // 4. Display name: exact (then case-insensitive) match against registered
  // assets. Zero or several hits are typed outcomes — never a silent pick.
  if ( manager )
  {
    QVector<sicnu::data::AssetSnapshot> matches;
    const QVector<sicnu::data::AssetSnapshot> all = manager->assets();
    for ( const auto &snapshot : all )
    {
      if ( snapshot.displayName() == ref )
      {
        matches = { snapshot };
        break;
      }
    }
    if ( matches.isEmpty() )
    {
      const QString lowered = ref.toLower();
      for ( const auto &snapshot : all )
      {
        if ( snapshot.displayName().toLower() == lowered )
          matches.append( snapshot );
      }
    }
    if ( matches.isEmpty() )
    {
      Json::Value details( Json::objectValue );
      details["reference"] = ref.toStdString();
      details["candidates"] = datasetCandidates( ref );
      return fail( HarnessError::makeWithAction(
        error_codes::kDatasetNotFound,
        "No registered dataset with this display name; inspect the workspace first",
        "spatial.workspace_summary", Json::Value() ) );
    }
    if ( matches.size() > 1 )
    {
      Json::Value details( Json::objectValue );
      details["reference"] = ref.toStdString();
      Json::Value candidates( Json::arrayValue );
      int listed = 0;
      for ( const auto &snapshot : matches )
      {
        if ( listed++ >= 8 )
          break;
        Json::Value candidate( Json::objectValue );
        candidate["asset_id"] = snapshot.id().toString().toStdString();
        candidate["display_name"] = snapshot.displayName().toStdString();
        candidate["path"] = snapshot.source().canonicalSource.toStdString();
        candidates.append( candidate );
      }
      details["candidates"] = candidates;
      HarnessError err = HarnessError::make(
        error_codes::kEntityAmbiguous,
        "Display name matches several datasets; pass an exact asset id or path",
        details );
      err.recoverable = true;
      return fail( err );
    }
    ResolvedDataset r;
    r.path = matches.first().source().canonicalSource;
    r.canonicalPath = r.path;
    r.assetId = matches.first().id().toString();
    r.displayName = matches.first().displayName();
    r.revision = static_cast<long long>( matches.first().revision().value() );
    r.assetEntityId = WorkspaceEntityRegistry::instance().idFor(
      QStringLiteral( "asset" ), r.canonicalPath );
    r.resolution = QStringLiteral( "display_name" );
    return r;
  }

  Json::Value details( Json::objectValue );
  details["reference"] = ref.toStdString();
  return fail( HarnessError::makeWithAction(
    error_codes::kDatasetNotFound,
    "Reference matches no entity id, UUID, path, or registered display name",
    "spatial.workspace_summary", Json::Value() ) );
}

Json::Value datasetCandidates( const QString &rawRef )
{
  Json::Value candidates( Json::arrayValue );
  sicnu::data::DataManager *manager = dataManagerOrNull();
  if ( !manager )
    return candidates;
  const QString needle = rawRef.trimmed().toLower();
  QVector<sicnu::data::AssetSnapshot> all = manager->assets();
  std::sort( all.begin(), all.end(), []( const auto &a, const auto &b )
             { return a.displayName() < b.displayName(); } );
  int listed = 0;
  for ( const auto &snapshot : all )
  {
    if ( listed >= 8 )
      break;
    const QString name = snapshot.displayName();
    if ( !needle.isEmpty() && !name.toLower().contains( needle ) )
      continue;
    Json::Value candidate( Json::objectValue );
    candidate["asset_id"] = snapshot.id().toString().toStdString();
    candidate["display_name"] = name.toStdString();
    candidate["path"] = snapshot.source().canonicalSource.toStdString();
    candidates.append( candidate );
    ++listed;
  }
  return candidates;
}

} // namespace sicnu::agent::harness
