/***************************************************************************
 * agent_context_tool.cpp — `workbench:context` read-only SpatialTool
 ***************************************************************************/
#include "agent_context_tool.h"

namespace sicnu::app
{

using sicnu::agent::spatial_tools::SpatialToolResult;

WorkbenchContextTool::WorkbenchContextTool( PayloadProvider provider )
    : m_provider( std::move( provider ) )
{
}

std::string WorkbenchContextTool::displayName() const
{
    return "Workbench Context";
}

std::string WorkbenchContextTool::description() const
{
    return "Read the CURRENT desktop workbench selection and context (read-only): "
           "active workbench id, the typed primary object, every selection list "
           "(layers/assets/results/datasets/experiment runs/models/workflow runs), "
           "availability facts and the command ids the workbench currently allows. "
           "Use it before proposing UI-side actions so they anchor to the user's "
           "real selection. This tool never mutates anything.";
}

std::vector<std::string> WorkbenchContextTool::tags() const
{
    return { "workbench", "context", "selection", "read-only", "gui" };
}

Json::Value WorkbenchContextTool::inputSchema() const
{
    // No parameters: the context is what it is right now. An empty schema
    // keeps the tool honest — there is nothing to select remotely.
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    schema["properties"] = Json::objectValue;
    return schema;
}

Json::Value WorkbenchContextTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );

    Json::Value stringType( Json::objectValue );
    stringType["type"] = "string";
    Json::Value arrayType( Json::objectValue );
    arrayType["type"] = "array";
    Json::Value objectType( Json::objectValue );
    objectType["type"] = "object";

    props["workbenchId"] = stringType;
    props["primaryObject"] = objectType;
    props["facts"] = objectType;
    props["availableCommands"] = arrayType;
    schema["properties"] = props;
    return schema;
}

SpatialToolResult WorkbenchContextTool::execute( const Json::Value &input )
{
    Q_UNUSED( input );
    if ( !m_provider )
        return SpatialToolResult::failure(
            "The workbench shell is not assembled in this process (headless mode); "
            "no UI context is available.",
            "WORKBENCH_UNAVAILABLE", "runtime" );
    Json::Value payload = m_provider();
    if ( payload.isNull() )
        return SpatialToolResult::failure(
            "The workbench shell is not assembled in this process (headless mode); "
            "no UI context is available.",
            "WORKBENCH_UNAVAILABLE", "runtime" );
    return SpatialToolResult::ok( std::move( payload ) );
}

} // namespace sicnu::app
