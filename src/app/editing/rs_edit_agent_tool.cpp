// rs_edit_agent_tool.cpp — see rs_edit_agent_tool.h for the read-only
// contract and the payload shape.
#include "rs_edit_agent_tool.h"

#include "editing/rs_edit_session.h"
#include "editing/rs_snapping_controller.h"

#include <json/json.h>

using namespace sicnu::agent::spatial_tools;

RsEditAgentTool::RsEditAgentTool( Sources sources, QObject *parent )
  : QObject( parent )
  , mSources( sources )
{
}

std::string RsEditAgentTool::displayName() const
{
    return "Editing state";
}

std::string RsEditAgentTool::description() const
{
    return "Read-only snapshot of the vector editing session: per-layer edit "
           "state (editing, modified, locked, undo/redo depth, feature and "
           "selection counts) and snapping configuration. This tool cannot "
           "modify anything; write operations are explicit app commands.";
}

std::vector<std::string> RsEditAgentTool::tags() const
{
    return { "editing", "session", "state", "read-only" };
}

Json::Value RsEditAgentTool::inputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    schema["properties"] = Json::Value( Json::objectValue );
    schema["required"] = Json::Value( Json::arrayValue );
    return schema;
}

Json::Value RsEditAgentTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );

    Json::Value editing( Json::objectValue );
    editing["type"] = "object";
    Json::Value editingProps( Json::objectValue );
    editingProps["dirty"] = Json::Value( "boolean" );
    Json::Value layers( Json::objectValue );
    layers["type"] = "array";
    editingProps["layers"] = layers;
    Json::Value snapping( Json::objectValue );
    snapping["type"] = "object";
    editingProps["snapping"] = snapping;
    editing["properties"] = editingProps;

    props["editing"] = editing;
    schema["properties"] = props;
    schema["required"] = Json::Value( Json::arrayValue );
    return schema;
}

SpatialToolResult RsEditAgentTool::execute( const Json::Value &input )
{
    if ( !input.isNull() && !input.isObject() )
        return SpatialToolResult::failure( "input must be an object", "INVALID_PARAMETER", "validation" );

    Json::Value output( Json::objectValue );
    output["editing"] = buildPayload();
    return SpatialToolResult::ok( std::move( output ) );
}

Json::Value RsEditAgentTool::buildPayload() const
{
    Json::Value editing( Json::objectValue );

    RsEditSession *session = mSources.session.data();
    Json::Value layers( Json::arrayValue );
    bool dirty = false;
    if ( session )
    {
        const QStringList ids = session->attachedLayerIds();
        for ( const QString &id : ids )
        {
            const RsLayerEditState s = session->state( id );
            Json::Value entry( Json::objectValue );
            entry["id"] = id.toStdString();
            if ( QgsVectorLayer *layer = session->layer( id ) )
                entry["name"] = layer->name().toStdString();
            entry["editing"] = s.editing;
            entry["modified"] = s.modified;
            entry["locked"] = s.locked;
            entry["undoDepth"] = static_cast<Json::Int64>( s.undoDepth );
            entry["redoDepth"] = static_cast<Json::Int64>( s.redoDepth );
            entry["featureCount"] = static_cast<Json::Int64>( s.featureCount );
            entry["selectedCount"] = s.selectedCount;
            layers.append( entry );
            dirty = dirty || s.modified;
        }
    }
    editing["dirty"] = dirty;
    editing["layers"] = layers;

    Json::Value snapping( Json::objectValue );
    if ( RsSnappingController *controller = mSources.snapping.data() )
    {
        snapping["enabled"] = controller->enabled();
        snapping["tolerance"] = controller->tolerance();
        snapping["intersectionSnapping"] = controller->intersectionSnapping();
    }
    else
    {
        snapping["enabled"] = false;
        snapping["tolerance"] = 0.0;
        snapping["intersectionSnapping"] = false;
    }
    editing["snapping"] = snapping;
    return editing;
}
