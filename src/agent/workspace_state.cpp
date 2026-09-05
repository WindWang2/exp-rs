// src/agent/workspace_state.cpp
#include "workspace_state.h"

#include <algorithm>

#include <QSet>
#include <qgsmasterlayoutinterface.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgslayout.h>
#include <qgslayoutmanager.h>
#include <qgsprintlayout.h>
#include <qgsrectangle.h>

#include "contracts/spatial_contracts.h"
#include "data/asset_types.h"
#include "data/band_role.h"
#include "data/data_manager.h"
#include "operators/framework/model_catalog.h"
#include "processing/framework/task_center.h"

namespace sicnu::agent {

namespace {

constexpr int kMaxAssets = 200;
constexpr int kMaxLayers = 200;
constexpr int kMaxModels = 25;
constexpr int kMaxRunningTasks = 25;
constexpr int kMaxWorkflowRuns = 20;

QString assetKindLabel( sicnu::data::AssetKind kind )
{
  using sicnu::data::AssetKind;
  switch ( kind )
  {
    case AssetKind::Raster:
      return QStringLiteral( "raster" );
    case AssetKind::Vector:
      return QStringLiteral( "vector" );
    case AssetKind::RemoteMap:
      return QStringLiteral( "remote_map" );
    case AssetKind::VirtualRaster:
      return QStringLiteral( "virtual_raster" );
  }
  return QStringLiteral( "unknown" );
}

const char *taskStatusLabel( sicnu::TaskStatus status )
{
  using TS = sicnu::TaskStatus;
  switch ( status )
  {
    case TS::Queued:
      return "queued";
    case TS::Running:
      return "running";
    case TS::Paused:
      return "paused";
    case TS::Completed:
      return "completed";
    case TS::Failed:
      return "failed";
    case TS::Canceled:
      return "canceled";
    case TS::WaitingResource:
      return "waiting_resource";
    case TS::Dispatching:
      return "dispatching";
    case TS::Cancelling:
      return "cancelling";
    default:
      return "unknown";
  }
}

} // namespace

// ---------------------------------------------------------------------------
// WorkspaceEntityRegistry
// ---------------------------------------------------------------------------

WorkspaceEntityRegistry &WorkspaceEntityRegistry::instance()
{
  static WorkspaceEntityRegistry registry;
  return registry;
}

AgentServices &AgentServices::instance()
{
  static AgentServices services;
  return services;
}

void WorkspaceEntityRegistry::loadPersisted( const QString &kind )
{
  QgsProject *project = QgsProject::instance();
  if ( !project )
    return;
  const QStringList entries =
    project->readListEntry( QStringLiteral( "sicnu" ), QStringLiteral( "entityIds/%1" ).arg( kind ) );
  for ( const QString &encoded : entries )
  {
    const int sep = encoded.indexOf( QStringLiteral( "|" ) );
    if ( sep <= 0 )
      continue;
    const QString key = encoded.left( sep );
    const QString id = encoded.mid( sep + 1 );
    if ( mIdByKey.contains( QStringLiteral( "%1\1%2" ).arg( kind, key ) ) )
      continue;
    mIdByKey.insert( QStringLiteral( "%1\1%2" ).arg( kind, key ), id );
    mKeyById.insert( id, QStringLiteral( "%1\1%2" ).arg( kind, key ) );
    bool ok = false;
    const int ordinal = id.mid( kind.size() + 1 ).toInt( &ok );
    if ( ok && ordinal >= mNextCounter.value( kind, 1 ) )
      mNextCounter[kind] = ordinal + 1;
  }
}

void WorkspaceEntityRegistry::persist( const QString &kind )
{
  QgsProject *project = QgsProject::instance();
  if ( !project )
    return;
  QStringList entries;
  const QString prefix = kind + QChar( '\1' );
  for ( auto it = mIdByKey.constBegin(); it != mIdByKey.constEnd(); ++it )
  {
    if ( !it.key().startsWith( prefix ) )
      continue;
    const QString key = it.key().mid( prefix.size() );
    entries.append( QStringLiteral( "%1|%2" ).arg( key, it.value() ) );
  }
  project->writeEntry( QStringLiteral( "sicnu" ),
                       QStringLiteral( "entityIds/%1" ).arg( kind ), entries );
}

QString WorkspaceEntityRegistry::idFor( const QString &kind, const QString &naturalKey )
{
  if ( kind.isEmpty() || naturalKey.isEmpty() )
    return QString();
  const QString composite = QStringLiteral( "%1\1%2" ).arg( kind, naturalKey );
  QMutexLocker lock( &mMutex );
  if ( !mIdByKey.contains( composite ) )
    loadPersisted( kind );
  if ( mIdByKey.contains( composite ) )
    return mIdByKey.value( composite );

  const int ordinal = mNextCounter.value( kind, 1 );
  const QString id = QStringLiteral( "%1-%2" ).arg( kind ).arg( ordinal );
  mNextCounter[kind] = ordinal + 1;
  mIdByKey.insert( composite, id );
  mKeyById.insert( id, composite );
  persist( kind );
  return id;
}

QString WorkspaceEntityRegistry::naturalKeyFor( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  const QString composite = mKeyById.value( id );
  const int sep = composite.indexOf( QChar( '\1' ) );
  return sep < 0 ? QString() : composite.mid( sep + 1 );
}

void WorkspaceEntityRegistry::clearInProcess()
{
  QMutexLocker lock( &mMutex );
  mIdByKey.clear();
  mKeyById.clear();
  mNextCounter.clear();
}

// ---------------------------------------------------------------------------
// Workflow runs provider seam
// ---------------------------------------------------------------------------

namespace {
WorkflowRunsProvider &workflowRunsProviderSlot()
{
  static WorkflowRunsProvider provider;
  return provider;
}
} // namespace

void setWorkflowRunsProvider( WorkflowRunsProvider provider )
{
  workflowRunsProviderSlot() = std::move( provider );
}

// ---------------------------------------------------------------------------
// buildWorkspaceState
// ---------------------------------------------------------------------------

Json::Value buildWorkspaceState( data::DataManager *dataManager, QgsMapCanvas *canvas,
                                 const QString &activeLayerName, int recentOutputsLimit )
{
  Json::Value body( Json::objectValue );
  recentOutputsLimit = std::clamp( recentOutputsLimit, 1, 50 );

  // --- project ---------------------------------------------------------
  QgsProject *project = QgsProject::instance();
  Json::Value projectJson( Json::objectValue );
  if ( project )
  {
    const QString fileName = project->fileName();
    if ( !fileName.isEmpty() )
      projectJson["file"] = fileName.toStdString();
    const QString title = project->title();
    if ( !title.isEmpty() )
      projectJson["title"] = title.toStdString();
  }
  body["project"] = projectJson;

  // --- view ------------------------------------------------------------
  Json::Value view( Json::objectValue );
  if ( canvas )
  {
    view["crs"] = canvas->mapSettings().destinationCrs().authid().toStdString();
    const QgsRectangle extent = canvas->extent();
    if ( !extent.isEmpty() && !extent.isNull() )
    {
      Json::Value ext( Json::objectValue );
      ext["xmin"] = extent.xMinimum();
      ext["ymin"] = extent.yMinimum();
      ext["xmax"] = extent.xMaximum();
      ext["ymax"] = extent.yMaximum();
      view["extent"] = ext;
    }
    view["scale"] = canvas->scale();
  }
  body["view"] = view;

  // --- assets (catalog-first, same precedence as data:list_layers) ------
  WorkspaceEntityRegistry &entities = WorkspaceEntityRegistry::instance();
  Json::Value assets( Json::arrayValue );
  QVector<sicnu::data::AssetSnapshot> assetSnapshots;
  if ( dataManager )
    assetSnapshots = dataManager->assets();
  const int assetLimit = std::min<int>( assetSnapshots.size(), kMaxAssets );
  for ( int i = 0; i < assetLimit; ++i )
  {
    const auto &asset = assetSnapshots.at( i );
    Json::Value a( Json::objectValue );
    a["id"] = entities.idFor( QStringLiteral( "asset" ), asset.source().canonicalSource )
                .toStdString();
    a["asset_id"] = asset.id().toString().toStdString();
    a["revision"] = static_cast<Json::UInt64>( asset.revision().value() );
    a["name"] = asset.displayName().toStdString();
    a["kind"] = assetKindLabel( asset.kind() ).toStdString();
    a["path"] = asset.source().canonicalSource.toStdString();

    const auto &structure = asset.structure();
    if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &structure ) )
    {
      a["width"] = raster->width;
      a["height"] = raster->height;
      a["band_count"] = raster->bandCount;
      Json::Value roles( Json::arrayValue );
      for ( const auto &band : raster->bands )
        roles.append( band.role == sicnu::data::BandRole::Unknown
                        ? ""
                        : sicnu::data::bandRoleToString( band.role ).toStdString() );
      a["band_roles"] = roles;
    }
    else if ( const auto *vector = std::get_if<sicnu::data::VectorStructure>( &structure ) )
    {
      a["layer_count"] = vector->layerCount;
    }
    assets.append( a );
  }
  body["assets"] = assets;
  body["assets_truncated"] = assetSnapshots.size() > kMaxAssets;

  // --- layers (display state; catalog merge for asset linkage) ----------
  Json::Value layersJson( Json::arrayValue );
  QSet<QString> visibleLayerIds;
  if ( canvas )
  {
    for ( const QgsMapLayer *layer : canvas->layers() )
      if ( layer )
        visibleLayerIds.insert( layer->id() );
  }
  if ( project )
  {
    int count = 0;
    for ( auto it = project->mapLayers().constBegin();
          it != project->mapLayers().constEnd() && count < kMaxLayers; ++it )
    {
      const QgsMapLayer *layer = it.value();
      if ( !layer || !layer->isValid() )
        continue;
      ++count;
      Json::Value l( Json::objectValue );
      l["id"] = entities.idFor( QStringLiteral( "layer" ), layer->id() ).toStdString();
      l["name"] = layer->name().toStdString();
      const bool isRaster = qobject_cast<const QgsRasterLayer *>( layer ) != nullptr;
      l["type"] = isRaster ? "raster" : "vector";
      l["visible"] = visibleLayerIds.contains( layer->id() ) ||
                     visibleLayerIds.isEmpty(); // headless: no canvas → all "visible"
      l["is_active"] = layer->name() == activeLayerName ||
                       ( canvas && canvas->currentLayer() == layer );
      if ( auto *vector = qobject_cast<const QgsVectorLayer *>( layer ) )
        l["selected_features"] = vector->selectedFeatureCount();
      // Link to catalog asset by source path when present.
      for ( const auto &asset : assetSnapshots )
      {
        if ( asset.source().canonicalSource == layer->source() )
        {
          l["asset_ref"] =
            entities.idFor( QStringLiteral( "asset" ), asset.source().canonicalSource ).toStdString();
          break;
        }
      }
      layersJson.append( l );
    }
  }
  body["layers"] = layersJson;

  // --- active ------------------------------------------------------------
  Json::Value active( Json::objectValue );
  const QgsMapLayer *currentLayer = canvas ? canvas->currentLayer() : nullptr;
  if ( currentLayer )
  {
    active["layer_id"] = entities.idFor( QStringLiteral( "layer" ), currentLayer->id() ).toStdString();
    active["layer_name"] = currentLayer->name().toStdString();
  }
  else if ( !activeLayerName.isEmpty() )
  {
    active["layer_name"] = activeLayerName.toStdString();
  }
  body["active"] = active;

  // --- temporal collections ----------------------------------------------
  Json::Value collections( Json::arrayValue );
  if ( dataManager )
  {
    for ( const auto &record : dataManager->temporalCollections() )
    {
      Json::Value c( Json::objectValue );
      c["id"] = entities.idFor( QStringLiteral( "collection" ), record.id.toString() ).toStdString();
      c["collection_id"] = record.id.toString().toStdString();
      c["name"] = record.displayName.toStdString();
      c["revision"] = static_cast<Json::Int>( record.revision );
      collections.append( c );
    }
  }
  body["temporal_collections"] = collections;

  // --- layouts -------------------------------------------------------------
  Json::Value layouts( Json::arrayValue );
  if ( project && project->layoutManager() )
  {
    for ( QgsMasterLayoutInterface *master : project->layoutManager()->layouts() )
    {
      auto *layout = dynamic_cast<QgsPrintLayout *>( master );
      if ( !layout )
        continue;
      Json::Value l( Json::objectValue );
      l["id"] = entities.idFor( QStringLiteral( "layout" ), layout->name() ).toStdString();
      l["name"] = layout->name().toStdString();
      layouts.append( l );
    }
  }
  body["layouts"] = layouts;

  // --- charts (Phase L registry; empty until charts are created) -----------
  Json::Value charts( Json::arrayValue );
  body["charts"] = charts;

  // --- models (summary only; selection lives in spatial:select_model) ------
  Json::Value models( Json::arrayValue );
  {
    const auto allModels = sicnu::operators::ModelCatalog::instance().models();
    int modelCount = 0;
    for ( const auto &model : allModels )
    {
      if ( modelCount++ >= kMaxModels )
        break;
      Json::Value m( Json::objectValue );
      m["name"] = model.name;
      m["task"] = model.task;
      m["readiness"] = sicnu::operators::modelReadinessName( model.readiness );
      models.append( m );
    }
  }
  body["models"] = models;

  // --- tasks: running + recent outputs --------------------------------------
  Json::Value running( Json::arrayValue );
  Json::Value recentOutputs( Json::arrayValue );
  {
    const auto tasks = sicnu::TaskCenter::instance().allTasks();
    int runningCount = 0;
    for ( auto it = tasks.crbegin(); it != tasks.crend() && runningCount < kMaxRunningTasks; ++it )
    {
      const auto &task = *it;
      const bool isActive = task.status == sicnu::TaskStatus::Running ||
                            task.status == sicnu::TaskStatus::Queued ||
                            task.status == sicnu::TaskStatus::Dispatching ||
                            task.status == sicnu::TaskStatus::WaitingResource ||
                            task.status == sicnu::TaskStatus::Cancelling;
      if ( !isActive )
        continue;
      ++runningCount;
      Json::Value t( Json::objectValue );
      t["task_id"] = std::to_string( task.taskId );
      t["algorithm"] = task.algorithmId.toStdString();
      t["status"] = taskStatusLabel( task.status );
      t["progress"] = task.progressPercentage;
      t["source"] = task.source.toStdString();
      running.append( t );
    }
    int outputCount = 0;
    for ( auto it = tasks.crbegin();
          it != tasks.crend() && outputCount < recentOutputsLimit; ++it )
    {
      const auto &task = *it;
      if ( task.status != sicnu::TaskStatus::Completed ||
           task.outputLayerPath.isEmpty() )
        continue;
      ++outputCount;
      Json::Value o( Json::objectValue );
      o["task_id"] = std::to_string( task.taskId );
      o["algorithm"] = task.algorithmId.toStdString();
      o["output_path"] = task.outputLayerPath.toStdString();
      o["output_ref"] = entities.idFor( QStringLiteral( "asset" ), task.outputLayerPath ).toStdString();
      recentOutputs.append( o );
    }
  }
  body["running_tasks"] = running;
  body["recent_outputs"] = recentOutputs;

  // --- workflow runs (provider seam) ----------------------------------------
  Json::Value workflowRuns( Json::arrayValue );
  if ( workflowRunsProviderSlot() )
  {
    Json::Value provided = workflowRunsProviderSlot()();
    if ( provided.isArray() )
    {
      const int runLimit = std::min<int>( provided.size(), kMaxWorkflowRuns );
      for ( int i = 0; i < runLimit; ++i )
        workflowRuns.append( provided[i] );
    }
  }
  body["workflow_runs"] = workflowRuns;

  return contracts::makeEnvelope( "workspace_state", std::move( body ) );
}

} // namespace sicnu::agent
