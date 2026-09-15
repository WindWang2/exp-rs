/***************************************************************************
 * mission_context.cpp — D18 MissionContext serialization & builders
 ***************************************************************************/
#include "app/workbench/mission_context.h"

#include "app/workbench/selection_context.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>

#include <utility>

namespace sicnu::app
{
namespace
{

QJsonArray refsToJson( const QVector<WorkbenchObjectRef> &refs )
{
    QJsonArray arr;
    for ( const auto &r : refs )
        arr.append( workbenchObjectRefToJson( r ) );
    return arr;
}

QVector<WorkbenchObjectRef> refsFromJson( const QJsonArray &arr )
{
    QVector<WorkbenchObjectRef> out;
    out.reserve( arr.size() );
    for ( const QJsonValue &v : arr )
    {
        if ( !v.isObject() )
            continue;
        const auto ref = workbenchObjectRefFromJson( v.toObject() );
        if ( !ref.isNull() )
            out.push_back( ref );
    }
    return out;
}

QJsonObject spatialToJson( const SpatialContext &s )
{
    QJsonObject o;
    o.insert( QStringLiteral( "aoi_wkt" ), s.aoiWkt );
    o.insert( QStringLiteral( "crs" ), s.crs );
    o.insert( QStringLiteral( "aoi_asset_id" ), s.aoiAssetId );
    if ( s.hasExtent )
    {
        QJsonArray e;
        e.append( s.xmin );
        e.append( s.ymin );
        e.append( s.xmax );
        e.append( s.ymax );
        o.insert( QStringLiteral( "extent" ), e );
    }
    return o;
}

SpatialContext spatialFromJson( const QJsonObject &o )
{
    SpatialContext s;
    s.aoiWkt = o.value( QStringLiteral( "aoi_wkt" ) ).toString();
    s.crs = o.value( QStringLiteral( "crs" ) ).toString();
    s.aoiAssetId = o.value( QStringLiteral( "aoi_asset_id" ) ).toString();
    const QJsonArray e = o.value( QStringLiteral( "extent" ) ).toArray();
    if ( e.size() == 4 )
    {
        s.xmin = e.at( 0 ).toDouble();
        s.ymin = e.at( 1 ).toDouble();
        s.xmax = e.at( 2 ).toDouble();
        s.ymax = e.at( 3 ).toDouble();
        s.hasExtent = true;
    }
    return s;
}

QJsonObject temporalToJson( const TemporalContext &t )
{
    QJsonObject o;
    o.insert( QStringLiteral( "start_iso" ), t.startIso );
    o.insert( QStringLiteral( "end_iso" ), t.endIso );
    o.insert( QStringLiteral( "collection_id" ), t.collectionId );
    o.insert( QStringLiteral( "active_acquisition_id" ), t.activeAcquisitionId );
    o.insert( QStringLiteral( "active_result_id" ), t.activeResultId );
    return o;
}

TemporalContext temporalFromJson( const QJsonObject &o )
{
    TemporalContext t;
    t.startIso = o.value( QStringLiteral( "start_iso" ) ).toString();
    t.endIso = o.value( QStringLiteral( "end_iso" ) ).toString();
    t.collectionId = o.value( QStringLiteral( "collection_id" ) ).toString();
    t.activeAcquisitionId = o.value( QStringLiteral( "active_acquisition_id" ) ).toString();
    t.activeResultId = o.value( QStringLiteral( "active_result_id" ) ).toString();
    return t;
}

QJsonObject mapToJson( const MapViewContext &m )
{
    QJsonObject o;
    o.insert( QStringLiteral( "crs" ), m.crs );
    o.insert( QStringLiteral( "scale" ), m.scale );
    if ( m.hasExtent )
    {
        QJsonArray e;
        e.append( m.xmin );
        e.append( m.ymin );
        e.append( m.xmax );
        e.append( m.ymax );
        o.insert( QStringLiteral( "extent" ), e );
    }
    return o;
}

MapViewContext mapFromJson( const QJsonObject &o )
{
    MapViewContext m;
    m.crs = o.value( QStringLiteral( "crs" ) ).toString();
    m.scale = o.value( QStringLiteral( "scale" ) ).toDouble();
    const QJsonArray e = o.value( QStringLiteral( "extent" ) ).toArray();
    if ( e.size() == 4 )
    {
        m.xmin = e.at( 0 ).toDouble();
        m.ymin = e.at( 1 ).toDouble();
        m.xmax = e.at( 2 ).toDouble();
        m.ymax = e.at( 3 ).toDouble();
        m.hasExtent = true;
    }
    return m;
}

QJsonObject selectionToJson( const MissionSelection &s )
{
    QJsonObject o;
    o.insert( QStringLiteral( "workbench_id" ), s.workbenchId );
    o.insert( QStringLiteral( "primary" ), workbenchObjectRefToJson( s.primary ) );
    o.insert( QStringLiteral( "layer_ids" ), QJsonArray::fromStringList( s.layerIds ) );
    o.insert( QStringLiteral( "asset_ids" ), QJsonArray::fromStringList( s.assetIds ) );
    o.insert( QStringLiteral( "result_ids" ), QJsonArray::fromStringList( s.resultIds ) );
    o.insert( QStringLiteral( "dataset_ids" ), QJsonArray::fromStringList( s.datasetIds ) );
    o.insert( QStringLiteral( "experiment_ids" ), QJsonArray::fromStringList( s.experimentIds ) );
    o.insert( QStringLiteral( "model_ids" ), QJsonArray::fromStringList( s.modelIds ) );
    o.insert( QStringLiteral( "workflow_run_ids" ), QJsonArray::fromStringList( s.workflowRunIds ) );
    return o;
}

QStringList stringListFromJson( const QJsonValue &v )
{
    QStringList out;
    const QJsonArray a = v.toArray();
    for ( const QJsonValue &x : a )
        out.push_back( x.toString() );
    return out;
}

MissionSelection selectionFromJson( const QJsonObject &o )
{
    MissionSelection s;
    s.workbenchId = o.value( QStringLiteral( "workbench_id" ) ).toString();
    s.primary = workbenchObjectRefFromJson( o.value( QStringLiteral( "primary" ) ).toObject() );
    s.layerIds = stringListFromJson( o.value( QStringLiteral( "layer_ids" ) ) );
    s.assetIds = stringListFromJson( o.value( QStringLiteral( "asset_ids" ) ) );
    s.resultIds = stringListFromJson( o.value( QStringLiteral( "result_ids" ) ) );
    s.datasetIds = stringListFromJson( o.value( QStringLiteral( "dataset_ids" ) ) );
    s.experimentIds = stringListFromJson( o.value( QStringLiteral( "experiment_ids" ) ) );
    s.modelIds = stringListFromJson( o.value( QStringLiteral( "model_ids" ) ) );
    s.workflowRunIds = stringListFromJson( o.value( QStringLiteral( "workflow_run_ids" ) ) );
    return s;
}

QJsonObject activeWorkflowToJson( const ActiveWorkflowRef &w )
{
    QJsonObject o;
    o.insert( QStringLiteral( "workflow_id" ), w.workflowId );
    o.insert( QStringLiteral( "name" ), w.name );
    o.insert( QStringLiteral( "schema_version" ), w.schemaVersion );
    o.insert( QStringLiteral( "fingerprint" ), w.fingerprint );
    o.insert( QStringLiteral( "runner" ), w.runner );
    return o;
}

ActiveWorkflowRef activeWorkflowFromJson( const QJsonObject &o )
{
    ActiveWorkflowRef w;
    w.workflowId = o.value( QStringLiteral( "workflow_id" ) ).toString();
    w.name = o.value( QStringLiteral( "name" ) ).toString();
    w.schemaVersion = o.value( QStringLiteral( "schema_version" ) ).toString();
    w.fingerprint = o.value( QStringLiteral( "fingerprint" ) ).toString();
    w.runner = o.value( QStringLiteral( "runner" ) ).toString();
    return w;
}

QJsonArray truncateArray( const QJsonArray &in, int maxItems )
{
    if ( maxItems < 0 || in.size() <= maxItems )
        return in;
    QJsonArray out;
    for ( int i = 0; i < maxItems; ++i )
        out.append( in.at( i ) );
    return out;
}

/// Canonical object for hashing: drop volatile metadata keys.
QJsonObject canonicalForFingerprint( const MissionContext &ctx )
{
    QJsonObject doc = missionContextToJson( ctx );
    doc.remove( QStringLiteral( "metadata" ) );
    // mission_id is identity of the session container, not scientific content
    doc.remove( QStringLiteral( "mission_id" ) );
    return doc;
}

} // namespace

QJsonObject workbenchObjectRefToJson( const WorkbenchObjectRef &ref )
{
    QJsonObject o;
    o.insert( QStringLiteral( "kind" ), objectKindToken( ref.kind ) );
    o.insert( QStringLiteral( "id" ), ref.id );
    o.insert( QStringLiteral( "display_name" ), ref.displayName );
    return o;
}

WorkbenchObjectRef workbenchObjectRefFromJson( const QJsonObject &obj )
{
    WorkbenchObjectRef ref;
    const QString token = obj.value( QStringLiteral( "kind" ) ).toString();
    if ( token == QLatin1String( "layer" ) )
        ref.kind = ObjectKind::Layer;
    else if ( token == QLatin1String( "asset" ) )
        ref.kind = ObjectKind::Asset;
    else if ( token == QLatin1String( "result" ) )
        ref.kind = ObjectKind::Result;
    else if ( token == QLatin1String( "dataset" ) )
        ref.kind = ObjectKind::Dataset;
    else if ( token == QLatin1String( "experiment_run" ) )
        ref.kind = ObjectKind::ExperimentRun;
    else if ( token == QLatin1String( "model" ) )
        ref.kind = ObjectKind::Model;
    else if ( token == QLatin1String( "workflow_run" ) )
        ref.kind = ObjectKind::WorkflowRun;
    else
        ref.kind = ObjectKind::None;
    ref.id = obj.value( QStringLiteral( "id" ) ).toString();
    ref.displayName = obj.value( QStringLiteral( "display_name" ) ).toString();
    if ( ref.kind == ObjectKind::None || ref.id.isEmpty() )
        return {};
    return ref;
}

QJsonObject missionContextToJson( const MissionContext &ctx )
{
    QJsonObject doc;
    doc.insert( QStringLiteral( "kind" ), QString::fromUtf8( kMissionContextKind ) );
    doc.insert( QStringLiteral( "schema_version" ), QString::fromUtf8( kMissionContextSchemaVersion ) );
    doc.insert( QStringLiteral( "mission_id" ), ctx.missionId );
    doc.insert( QStringLiteral( "mission_name" ), ctx.missionName );
    doc.insert( QStringLiteral( "project_ref" ), ctx.projectRef );
    doc.insert( QStringLiteral( "notes" ), ctx.notes );
    doc.insert( QStringLiteral( "spatial" ), spatialToJson( ctx.spatial ) );
    doc.insert( QStringLiteral( "temporal" ), temporalToJson( ctx.temporal ) );
    doc.insert( QStringLiteral( "map" ), mapToJson( ctx.map ) );
    doc.insert( QStringLiteral( "selection" ), selectionToJson( ctx.selection ) );
    doc.insert( QStringLiteral( "active_workflow" ), activeWorkflowToJson( ctx.activeWorkflow ) );
    doc.insert( QStringLiteral( "datasets" ), refsToJson( ctx.datasets ) );
    doc.insert( QStringLiteral( "assets" ), refsToJson( ctx.assets ) );
    doc.insert( QStringLiteral( "layers" ), refsToJson( ctx.layers ) );
    doc.insert( QStringLiteral( "results" ), refsToJson( ctx.results ) );
    doc.insert( QStringLiteral( "models" ), refsToJson( ctx.models ) );
    doc.insert( QStringLiteral( "workflow_runs" ), refsToJson( ctx.workflowRuns ) );
    doc.insert( QStringLiteral( "experiments" ), refsToJson( ctx.experiments ) );
    doc.insert( QStringLiteral( "artifacts" ), refsToJson( ctx.artifacts ) );
    doc.insert( QStringLiteral( "cartography_products" ), refsToJson( ctx.cartographyProducts ) );
    doc.insert( QStringLiteral( "agent_decisions" ), refsToJson( ctx.agentDecisions ) );
    doc.insert( QStringLiteral( "metadata" ), ctx.metadata );
    return doc;
}

bool missionContextFromJson( const QJsonObject &doc, MissionContext &out, QString *error )
{
    const QString kind = doc.value( QStringLiteral( "kind" ) ).toString();
    if ( kind != QLatin1String( kMissionContextKind ) )
    {
        if ( error )
            *error = QStringLiteral( "expected kind mission_context, got '%1'" ).arg( kind );
        return false;
    }
    const QString ver = doc.value( QStringLiteral( "schema_version" ) ).toString();
    if ( ver != QLatin1String( kMissionContextSchemaVersion ) )
    {
        if ( error )
            *error = QStringLiteral( "unsupported mission_context schema_version '%1'" ).arg( ver );
        return false;
    }

    MissionContext ctx;
    ctx.missionId = doc.value( QStringLiteral( "mission_id" ) ).toString();
    ctx.missionName = doc.value( QStringLiteral( "mission_name" ) ).toString();
    ctx.projectRef = doc.value( QStringLiteral( "project_ref" ) ).toString();
    ctx.notes = doc.value( QStringLiteral( "notes" ) ).toString();
    ctx.spatial = spatialFromJson( doc.value( QStringLiteral( "spatial" ) ).toObject() );
    ctx.temporal = temporalFromJson( doc.value( QStringLiteral( "temporal" ) ).toObject() );
    ctx.map = mapFromJson( doc.value( QStringLiteral( "map" ) ).toObject() );
    ctx.selection = selectionFromJson( doc.value( QStringLiteral( "selection" ) ).toObject() );
    ctx.activeWorkflow = activeWorkflowFromJson( doc.value( QStringLiteral( "active_workflow" ) ).toObject() );
    ctx.datasets = refsFromJson( doc.value( QStringLiteral( "datasets" ) ).toArray() );
    ctx.assets = refsFromJson( doc.value( QStringLiteral( "assets" ) ).toArray() );
    ctx.layers = refsFromJson( doc.value( QStringLiteral( "layers" ) ).toArray() );
    ctx.results = refsFromJson( doc.value( QStringLiteral( "results" ) ).toArray() );
    ctx.models = refsFromJson( doc.value( QStringLiteral( "models" ) ).toArray() );
    ctx.workflowRuns = refsFromJson( doc.value( QStringLiteral( "workflow_runs" ) ).toArray() );
    ctx.experiments = refsFromJson( doc.value( QStringLiteral( "experiments" ) ).toArray() );
    ctx.artifacts = refsFromJson( doc.value( QStringLiteral( "artifacts" ) ).toArray() );
    ctx.cartographyProducts = refsFromJson( doc.value( QStringLiteral( "cartography_products" ) ).toArray() );
    ctx.agentDecisions = refsFromJson( doc.value( QStringLiteral( "agent_decisions" ) ).toArray() );
    ctx.metadata = doc.value( QStringLiteral( "metadata" ) ).toObject();
    out = std::move( ctx );
    return true;
}

QString missionContentFingerprint( const MissionContext &ctx )
{
    if ( ctx.isNull() && ctx.datasets.isEmpty() && ctx.assets.isEmpty() && ctx.layers.isEmpty()
         && ctx.results.isEmpty() && ctx.activeWorkflow.isNull() )
    {
        // Still hash empty scientific payload for stability of "blank mission".
    }
    const QJsonObject canonical = canonicalForFingerprint( ctx );
    const QByteArray bytes = QJsonDocument( canonical ).toJson( QJsonDocument::Compact );
    const QByteArray hash = QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 );
    return QString::fromLatin1( hash.toHex() );
}

QJsonObject missionSummaryJson( const MissionContext &ctx, int maxListItems )
{
    QJsonObject o;
    o.insert( QStringLiteral( "mission" ), ctx.missionName.isEmpty() ? ctx.missionId : ctx.missionName );
    o.insert( QStringLiteral( "mission_id" ), ctx.missionId );
    o.insert( QStringLiteral( "project_ref" ), ctx.projectRef );
    o.insert( QStringLiteral( "fingerprint" ), missionContentFingerprint( ctx ) );
    o.insert( QStringLiteral( "selection" ), selectionToJson( ctx.selection ) );
    o.insert( QStringLiteral( "active_workflow" ), activeWorkflowToJson( ctx.activeWorkflow ) );
    o.insert( QStringLiteral( "active_temporal_context" ), temporalToJson( ctx.temporal ) );
    o.insert( QStringLiteral( "spatial" ), spatialToJson( ctx.spatial ) );
    o.insert( QStringLiteral( "visible_layers" ), truncateArray( refsToJson( ctx.layers ), maxListItems ) );
    o.insert( QStringLiteral( "recent_results" ), truncateArray( refsToJson( ctx.results ), maxListItems ) );
    o.insert( QStringLiteral( "datasets" ), truncateArray( refsToJson( ctx.datasets ), maxListItems ) );
    o.insert( QStringLiteral( "pending_failures" ), QJsonArray() ); // filled by run observers later
    return o;
}

MissionContext missionContextFromSelection( const SelectionContextSnapshot &snapshot, MissionContext base )
{
    MissionContext ctx = std::move( base );
    ctx.selection.workbenchId = snapshot.workbenchId;
    ctx.selection.primary = ContextRules::primaryObject( snapshot );
    ctx.selection.layerIds = ContextRules::selectedLayerIds( snapshot );
    ctx.selection.assetIds = snapshot.selectedAssetIds;
    ctx.selection.resultIds = snapshot.selectedResultIds;
    ctx.selection.datasetIds = snapshot.selectedDatasetIds;
    ctx.selection.experimentIds = snapshot.selectedExperimentIds;
    ctx.selection.modelIds = snapshot.selectedModelIds;
    ctx.selection.workflowRunIds = snapshot.selectedWorkflowRunIds;

    auto pushUnique = []( QVector<WorkbenchObjectRef> &dst, ObjectKind kind, const QStringList &ids ) {
        for ( const QString &id : ids )
        {
            if ( id.isEmpty() )
                continue;
            WorkbenchObjectRef ref;
            ref.kind = kind;
            ref.id = id;
            bool exists = false;
            for ( const auto &e : dst )
            {
                if ( e.kind == kind && e.id == id )
                {
                    exists = true;
                    break;
                }
            }
            if ( !exists )
                dst.push_back( ref );
        }
    };

    pushUnique( ctx.assets, ObjectKind::Asset, snapshot.selectedAssetIds );
    pushUnique( ctx.results, ObjectKind::Result, snapshot.selectedResultIds );
    pushUnique( ctx.datasets, ObjectKind::Dataset, snapshot.selectedDatasetIds );
    pushUnique( ctx.experiments, ObjectKind::ExperimentRun, snapshot.selectedExperimentIds );
    pushUnique( ctx.models, ObjectKind::Model, snapshot.selectedModelIds );
    pushUnique( ctx.workflowRuns, ObjectKind::WorkflowRun, snapshot.selectedWorkflowRunIds );
    pushUnique( ctx.layers, ObjectKind::Layer, ctx.selection.layerIds );

    if ( snapshot.hasTemporal && ctx.temporal.collectionId.isEmpty() )
        ctx.metadata.insert( QStringLiteral( "has_temporal_hint" ), true );

    return ctx;
}

void ensureMissionId( MissionContext &ctx )
{
    if ( ctx.missionId.isEmpty() )
        ctx.missionId = QUuid::createUuid().toString( QUuid::WithoutBraces );
}

} // namespace sicnu::app
