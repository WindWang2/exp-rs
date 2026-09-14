/***************************************************************************
 * object_identity.cpp — unified object identity: pure rules + projections
 ***************************************************************************/
#include "object_identity.h"

#include "selection_context.h"

#include <qgsmaplayer.h>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/governance/governance_store.h"
#include "data/governance/workspace_service.h"

namespace sicnu::app
{

QString objectKindToken( ObjectKind kind )
{
    switch ( kind )
    {
        case ObjectKind::Layer:
            return QStringLiteral( "layer" );
        case ObjectKind::Asset:
            return QStringLiteral( "asset" );
        case ObjectKind::Result:
            return QStringLiteral( "result" );
        case ObjectKind::Dataset:
            return QStringLiteral( "dataset" );
        case ObjectKind::ExperimentRun:
            return QStringLiteral( "experiment_run" );
        case ObjectKind::Model:
            return QStringLiteral( "model" );
        case ObjectKind::WorkflowRun:
            return QStringLiteral( "workflow_run" );
        case ObjectKind::None:
            break;
    }
    return QStringLiteral( "none" );
}

namespace ContextRules
{

WorkbenchObjectRef primaryObject( const SelectionContextSnapshot &snapshot )
{
    // Priority contract (see header). Each entry names the first id of the
    // matching selection list; the lists keep their panel's selection order.
    if ( !snapshot.selectedWorkflowRunIds.isEmpty() )
        return { ObjectKind::WorkflowRun, snapshot.selectedWorkflowRunIds.first(), QString() };
    if ( !snapshot.selectedExperimentIds.isEmpty() )
        return { ObjectKind::ExperimentRun, snapshot.selectedExperimentIds.first(), QString() };
    if ( !snapshot.selectedDatasetIds.isEmpty() )
        return { ObjectKind::Dataset, snapshot.selectedDatasetIds.first(), QString() };
    if ( !snapshot.selectedModelIds.isEmpty() )
        return { ObjectKind::Model, snapshot.selectedModelIds.first(), QString() };
    if ( !snapshot.selectedResultIds.isEmpty() )
        return { ObjectKind::Result, snapshot.selectedResultIds.first(), QString() };
    if ( !snapshot.selectedAssetIds.isEmpty() )
        return { ObjectKind::Asset, snapshot.selectedAssetIds.first(), QString() };

    if ( snapshot.activeLayer )
        return { ObjectKind::Layer, snapshot.activeLayer->id(), snapshot.activeLayer->name() };
    if ( !snapshot.selectedLayers.isEmpty() && snapshot.selectedLayers.first() )
        return { ObjectKind::Layer, snapshot.selectedLayers.first()->id(),
                 snapshot.selectedLayers.first()->name() };
    return {};
}

QgsMapLayer *resolvePrimaryLayer( const SelectionContextSnapshot &snapshot )
{
    const WorkbenchObjectRef primary = primaryObject( snapshot );
    if ( primary.kind != ObjectKind::Layer )
        return nullptr;
    if ( snapshot.activeLayer && snapshot.activeLayer->id() == primary.id )
        return snapshot.activeLayer;
    for ( QgsMapLayer *layer : snapshot.selectedLayers )
    {
        if ( layer && layer->id() == primary.id )
            return layer;
    }
    return nullptr;
}

} // namespace ContextRules

QVector<sicnu::data::AssetId> resolveSelectionAssetTargets(
    const SelectionContextSnapshot &snapshot,
    sicnu::data::DataManager *dataManager,
    sicnu::workspace::WorkspaceService *workspace )
{
    QVector<sicnu::data::AssetId> targets;
    if ( !dataManager )
        return targets;

    const auto pushTarget = [&]( const sicnu::data::AssetId &id ) {
        if ( id.isNull() || targets.contains( id ) || targets.size() >= kObjectLinkMaxTargets )
            return;
        targets.append( id );
    };

    // 1) Data Manager asset selection.
    for ( const QString &idText : snapshot.selectedAssetIds )
    {
        if ( const auto id = sicnu::data::AssetId::fromString( idText ) )
            pushTarget( *id );
    }

    // 2) Governance Results/History selection → GovernedAsset → asset id.
    if ( workspace && targets.isEmpty() )
    {
        for ( const QString &entityId : snapshot.selectedResultIds )
        {
            if ( targets.size() >= kObjectLinkMaxTargets )
                break;
            const std::optional<sicnu::workspace::GovernedAsset> governed =
                workspace->store().assetById( entityId );
            if ( !governed )
                continue;
            if ( const auto id = sicnu::data::AssetId::fromString( governed->assetId ) )
                pushTarget( *id );
            else if ( !governed->canonicalSource.isEmpty() )
                if ( const auto byPath = dataManager->findByPath( governed->canonicalSource ) )
                    pushTarget( byPath->id() );
        }
    }

    // 3) Layer selection: the layer's source path through the catalog.
    if ( targets.isEmpty() )
    {
        QList<QgsMapLayer *> layers;
        if ( snapshot.activeLayer )
            layers.append( snapshot.activeLayer );
        for ( QgsMapLayer *layer : snapshot.selectedLayers )
            if ( layer && layers.size() < kObjectLinkMaxTargets )
                layers.append( layer );
        for ( QgsMapLayer *layer : layers )
        {
            if ( targets.size() >= kObjectLinkMaxTargets )
                break;
            if ( !layer || layer->source().isEmpty() )
                continue;
            const std::optional<sicnu::data::AssetSnapshot> asset =
                dataManager->findByPath( layer->source() );
            if ( asset )
                pushTarget( asset->id() );
        }
    }

    return targets;
}

Json::Value workbenchContextToJson( const SelectionContextSnapshot &snapshot,
                                    const QStringList &availableCommandIds )
{
    Json::Value root( Json::objectValue );

    root["workbenchId"] = snapshot.workbenchId.toStdString();

    const ContextRules::ContextFacts facts = ContextRules::prerequisiteFacts( snapshot );
    Json::Value factsJson( Json::objectValue );
    factsJson["hasLayerSelection"] = facts.hasLayerSelection;
    factsJson["hasRaster"] = facts.hasRaster;
    factsJson["hasVector"] = facts.hasVector;
    factsJson["hasSar"] = facts.hasSar;
    factsJson["editable"] = facts.editable;
    factsJson["editing"] = facts.editing;
    factsJson["hasGovernanceResult"] = facts.hasGovernanceResult;
    factsJson["hasGovernanceAsset"] = facts.hasGovernanceAsset;
    factsJson["hasExperimentSelection"] = facts.hasExperiment;
    factsJson["hasDatasetSelection"] = facts.hasDataset;
    factsJson["hasModelSelection"] = facts.hasModel;
    factsJson["hasWorkflowRunSelection"] = facts.hasWorkflowRun;
    factsJson["hasBrokenLayer"] = facts.hasBrokenLayer;
    factsJson["hasInFlightTask"] = facts.hasInFlightTask;
    root["facts"] = factsJson;

    const WorkbenchObjectRef primary = ContextRules::primaryObject( snapshot );
    if ( !primary.isNull() )
    {
        Json::Value primaryJson( Json::objectValue );
        primaryJson["kind"] = objectKindToken( primary.kind ).toStdString();
        primaryJson["id"] = primary.id.toStdString();
        if ( !primary.displayName.isEmpty() )
            primaryJson["displayName"] = primary.displayName.toStdString();
        root["primaryObject"] = primaryJson;
    }

    const auto idArray = []( const QStringList &ids ) {
        Json::Value arr( Json::arrayValue );
        for ( const QString &id : ids )
            arr.append( id.toStdString() );
        return arr;
    };
    root["selectedLayers"] = idArray( ContextRules::selectedLayerIds( snapshot ) );
    root["selectedAssets"] = idArray( snapshot.selectedAssetIds );
    root["selectedResults"] = idArray( snapshot.selectedResultIds );
    root["selectedDatasets"] = idArray( snapshot.selectedDatasetIds );
    root["selectedExperimentRuns"] = idArray( snapshot.selectedExperimentIds );
    root["selectedModels"] = idArray( snapshot.selectedModelIds );
    root["selectedWorkflowRuns"] = idArray( snapshot.selectedWorkflowRunIds );

    Json::Value commands( Json::arrayValue );
    for ( const QString &id : availableCommandIds )
        commands.append( id.toStdString() );
    root["availableCommands"] = commands;

    return root;
}

} // namespace sicnu::app
