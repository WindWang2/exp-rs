/***************************************************************************
 * exprs/plugin_manifest.cpp
 ***************************************************************************/
#include "exprs/plugin_manifest.h"

#include <fstream>
#include <sstream>

namespace exprs {

namespace {
std::string requireString( const Json::Value &object, const char *key, std::string &error )
{
    const Json::Value &value = object[key];
    if ( !value.isString() || value.asString().empty() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a non-empty string";
        return {};
    }
    return value.asString();
}
} // namespace

// ---------------------------------------------------------------------------
// ManifestPort
// ---------------------------------------------------------------------------
Json::Value ManifestPort::toJson() const
{
    Json::Value json( Json::objectValue );
    json["name"] = name;
    json["type"] = type;
    json["required"] = required;
    if ( !description.empty() )
        json["description"] = description;
    if ( !defaultValue.isNull() )
        json["default"] = defaultValue;
    if ( !enumOptions.isNull() )
        json["enum"] = enumOptions;
    if ( hasMin )
        json["min"] = minValue;
    if ( hasMax )
        json["max"] = maxValue;
    if ( !fileFormat.empty() )
        json["file_format"] = fileFormat;
    return json;
}

bool ManifestPort::fromJson( const Json::Value &json, ManifestPort &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "port entry must be an object";
        return false;
    }
    out = ManifestPort();
    out.name = requireString( json, "name", error );
    if ( out.name.empty() )
        return false;
    if ( json.isMember( "type" ) && json["type"].isString() )
        out.type = json["type"].asString();
    out.required = json.get( "required", false ).asBool();
    if ( json.isMember( "description" ) && json["description"].isString() )
        out.description = json["description"].asString();
    if ( json.isMember( "default" ) )
        out.defaultValue = json["default"];
    if ( json.isMember( "enum" ) && json["enum"].isArray() )
        out.enumOptions = json["enum"];
    if ( json.isMember( "min" ) && json["min"].isNumeric() )
    {
        out.hasMin = true;
        out.minValue = json["min"].asDouble();
    }
    if ( json.isMember( "max" ) && json["max"].isNumeric() )
    {
        out.hasMax = true;
        out.maxValue = json["max"].asDouble();
    }
    if ( json.isMember( "file_format" ) && json["file_format"].isString() )
        out.fileFormat = json["file_format"].asString();
    return true;
}

// ---------------------------------------------------------------------------
// ManifestExternalTool
// ---------------------------------------------------------------------------
Json::Value ManifestExternalTool::toJson() const
{
    Json::Value json( Json::objectValue );
    Json::Value argvJson( Json::arrayValue );
    for ( const std::string &arg : argv )
        argvJson.append( arg );
    json["argv"] = argvJson;
    if ( !environment.isNull() && environment.isObject() )
        json["environment"] = environment;
    json["inherit_environment"] = inheritEnvironment;
    if ( !workingDirectoryParam.empty() )
        json["working_directory"] = workingDirectoryParam;
    json["timeout_seconds"] = timeoutSeconds;
    json["stdout_limit_bytes"] = stdoutLimitBytes;
    json["stderr_limit_bytes"] = stderrLimitBytes;
    return json;
}

bool ManifestExternalTool::fromJson( const Json::Value &json, ManifestExternalTool &out,
                                     std::string &error )
{
    if ( !json.isObject() )
    {
        error = "'external' must be an object";
        return false;
    }
    out = ManifestExternalTool();
    const Json::Value &argvJson = json["argv"];
    if ( !argvJson.isArray() || argvJson.empty() )
    {
        error = "'external.argv' must be a non-empty array";
        return false;
    }
    for ( const Json::Value &arg : argvJson )
    {
        if ( !arg.isString() )
        {
            error = "'external.argv' entries must be strings";
            return false;
        }
        out.argv.push_back( arg.asString() );
    }
    if ( json.isMember( "environment" ) && json["environment"].isObject() )
        out.environment = json["environment"];
    out.inheritEnvironment = json.get( "inherit_environment", false ).asBool();
    if ( json.isMember( "working_directory" ) && json["working_directory"].isString() )
        out.workingDirectoryParam = json["working_directory"].asString();
    if ( json.isMember( "timeout_seconds" ) && json["timeout_seconds"].isNumeric() )
        out.timeoutSeconds = json["timeout_seconds"].asInt();
    if ( json.isMember( "stdout_limit_bytes" ) && json["stdout_limit_bytes"].isNumeric() )
        out.stdoutLimitBytes = json["stdout_limit_bytes"].asInt();
    if ( json.isMember( "stderr_limit_bytes" ) && json["stderr_limit_bytes"].isNumeric() )
        out.stderrLimitBytes = json["stderr_limit_bytes"].asInt();
    if ( out.timeoutSeconds <= 0 )
        out.timeoutSeconds = 3600;
    return true;
}

// ---------------------------------------------------------------------------
// ManifestOperator
// ---------------------------------------------------------------------------
Json::Value ManifestOperator::toJson() const
{
    Json::Value json( Json::objectValue );
    json["id"] = id;
    json["display_name"] = displayName;
    if ( !group.empty() )
        json["group"] = group;
    if ( !description.empty() )
        json["description"] = description;
    json["memory_policy"] = memoryPolicy;
    json["determinism_grade"] = determinismGrade;
    json["supports_cancel"] = supportsCancel;
    if ( !schema.isNull() )
        json["schema"] = schema;
    Json::Value inputsJson( Json::arrayValue );
    for ( const ManifestPort &port : inputs )
        inputsJson.append( port.toJson() );
    json["inputs"] = inputsJson;
    Json::Value outputsJson( Json::arrayValue );
    for ( const ManifestPort &port : outputs )
        outputsJson.append( port.toJson() );
    json["outputs"] = outputsJson;
    if ( !metadata.isNull() )
        json["metadata"] = metadata;
    if ( hasExternalTool )
        json["external"] = external.toJson();
    return json;
}

bool ManifestOperator::fromJson( const Json::Value &json, ManifestOperator &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "operator entry must be an object";
        return false;
    }
    out = ManifestOperator();
    out.id = requireString( json, "id", error );
    if ( out.id.empty() )
        return false;
    out.displayName = requireString( json, "display_name", error );
    if ( out.displayName.empty() )
        return false;
    if ( json.isMember( "group" ) && json["group"].isString() )
        out.group = json["group"].asString();
    if ( json.isMember( "description" ) && json["description"].isString() )
        out.description = json["description"].asString();
    if ( json.isMember( "memory_policy" ) && json["memory_policy"].isString() )
        out.memoryPolicy = json["memory_policy"].asString();
    if ( json.isMember( "determinism_grade" ) && json["determinism_grade"].isString() )
        out.determinismGrade = json["determinism_grade"].asString();
    out.supportsCancel = json.get( "supports_cancel", true ).asBool();
    if ( json.isMember( "schema" ) )
        out.schema = json["schema"];
    if ( json.isMember( "metadata" ) )
        out.metadata = json["metadata"];
    for ( const Json::Value &portJson : json["inputs"] )
    {
        ManifestPort port;
        if ( !ManifestPort::fromJson( portJson, port, error ) )
            return false;
        out.inputs.push_back( port );
    }
    for ( const Json::Value &portJson : json["outputs"] )
    {
        ManifestPort port;
        if ( !ManifestPort::fromJson( portJson, port, error ) )
            return false;
        out.outputs.push_back( port );
    }
    if ( json.isMember( "external" ) )
    {
        out.hasExternalTool = true;
        if ( !ManifestExternalTool::fromJson( json["external"], out.external, error ) )
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Simple section structs
// ---------------------------------------------------------------------------
Json::Value ManifestDataProvider::toJson() const
{
    Json::Value json( Json::objectValue );
    json["id"] = id;
    json["display_name"] = displayName;
    if ( !description.empty() )
        json["description"] = description;
    Json::Value schemesJson( Json::arrayValue );
    for ( const std::string &scheme : schemes )
        schemesJson.append( scheme );
    json["schemes"] = schemesJson;
    if ( !capabilities.isNull() )
        json["capabilities"] = capabilities;
    return json;
}

bool ManifestDataProvider::fromJson( const Json::Value &json, ManifestDataProvider &out,
                                     std::string &error )
{
    if ( !json.isObject() )
    {
        error = "data provider entry must be an object";
        return false;
    }
    out = ManifestDataProvider();
    out.id = requireString( json, "id", error );
    if ( out.id.empty() )
        return false;
    out.displayName = requireString( json, "display_name", error );
    if ( out.displayName.empty() )
        return false;
    if ( json.isMember( "description" ) && json["description"].isString() )
        out.description = json["description"].asString();
    for ( const Json::Value &scheme : json["schemes"] )
    {
        if ( scheme.isString() )
            out.schemes.push_back( scheme.asString() );
    }
    if ( json.isMember( "capabilities" ) )
        out.capabilities = json["capabilities"];
    return true;
}

Json::Value ManifestModelRuntime::toJson() const
{
    Json::Value json( Json::objectValue );
    json["framework"] = framework;
    json["display_name"] = displayName;
    if ( !description.empty() )
        json["description"] = description;
    json["gpu"] = gpu;
    return json;
}

bool ManifestModelRuntime::fromJson( const Json::Value &json, ManifestModelRuntime &out,
                                     std::string &error )
{
    if ( !json.isObject() )
    {
        error = "model runtime entry must be an object";
        return false;
    }
    out = ManifestModelRuntime();
    out.framework = requireString( json, "framework", error );
    if ( out.framework.empty() )
        return false;
    out.displayName = requireString( json, "display_name", error );
    if ( out.displayName.empty() )
        return false;
    if ( json.isMember( "description" ) && json["description"].isString() )
        out.description = json["description"].asString();
    out.gpu = json.get( "gpu", false ).asBool();
    return true;
}

Json::Value ManifestAgentTool::toJson() const
{
    Json::Value json( Json::objectValue );
    json["id"] = id;
    json["display_name"] = displayName;
    if ( !category.empty() )
        json["category"] = category;
    if ( !description.empty() )
        json["description"] = description;
    json["input_schema"] = inputSchema;
    if ( !outputSchema.isNull() )
        json["output_schema"] = outputSchema;
    return json;
}

bool ManifestAgentTool::fromJson( const Json::Value &json, ManifestAgentTool &out,
                                  std::string &error )
{
    if ( !json.isObject() )
    {
        error = "agent tool entry must be an object";
        return false;
    }
    out = ManifestAgentTool();
    out.id = requireString( json, "id", error );
    if ( out.id.empty() )
        return false;
    out.displayName = requireString( json, "display_name", error );
    if ( out.displayName.empty() )
        return false;
    if ( json.isMember( "category" ) && json["category"].isString() )
        out.category = json["category"].asString();
    if ( json.isMember( "description" ) && json["description"].isString() )
        out.description = json["description"].asString();
    if ( json.isMember( "input_schema" ) && json["input_schema"].isObject() )
        out.inputSchema = json["input_schema"];
    if ( json.isMember( "output_schema" ) && json["output_schema"].isObject() )
        out.outputSchema = json["output_schema"];
    if ( out.inputSchema.isNull() )
    {
        error = "agent tool '" + out.id + "' requires an object 'input_schema'";
        return false;
    }
    return true;
}

Json::Value ManifestUi::toJson() const
{
    Json::Value json( Json::objectValue );
    json["dock"] = dock;
    json["menu_actions"] = menuActions;
    json["settings_page"] = settingsPage;
    if ( !dockTitle.empty() )
        json["dock_title"] = dockTitle;
    if ( !settingsPageTitle.empty() )
        json["settings_page_title"] = settingsPageTitle;
    return json;
}

bool ManifestUi::fromJson( const Json::Value &json, ManifestUi &out, std::string &error )
{
    if ( !json.isObject() )
    {
        error = "'ui' must be an object";
        return false;
    }
    out = ManifestUi();
    out.dock = json.get( "dock", false ).asBool();
    out.menuActions = json.get( "menu_actions", false ).asBool();
    out.settingsPage = json.get( "settings_page", false ).asBool();
    if ( json.isMember( "dock_title" ) && json["dock_title"].isString() )
        out.dockTitle = json["dock_title"].asString();
    if ( json.isMember( "settings_page_title" ) && json["settings_page_title"].isString() )
        out.settingsPageTitle = json["settings_page_title"].asString();
    return true;
}

Json::Value ManifestCartography::toJson() const
{
    Json::Value json( Json::objectValue );
    json["layout_items"] = layoutItems;
    json["layout_item_ids"] = layoutItemIds;
    return json;
}

bool ManifestCartography::fromJson( const Json::Value &json, ManifestCartography &out,
                                    std::string &error )
{
    if ( !json.isObject() )
    {
        error = "'cartography' must be an object";
        return false;
    }
    out = ManifestCartography();
    out.layoutItems = json.get( "layout_items", false ).asBool();
    if ( json.isMember( "layout_item_ids" ) )
        out.layoutItemIds = json["layout_item_ids"];
    return true;
}

Json::Value ManifestPythonSection::toJson() const
{
    Json::Value json( Json::objectValue );
    json["module"] = module;
    if ( !package.empty() )
        json["package"] = package;
    return json;
}

bool ManifestPythonSection::fromJson( const Json::Value &json, ManifestPythonSection &out,
                                      std::string &error )
{
    if ( !json.isObject() )
    {
        error = "'python' must be an object";
        return false;
    }
    out = ManifestPythonSection();
    out.module = requireString( json, "module", error );
    if ( out.module.empty() )
        return false;
    if ( json.isMember( "package" ) && json["package"].isString() )
        out.package = json["package"].asString();
    return true;
}

// ---------------------------------------------------------------------------
// PluginManifest
// ---------------------------------------------------------------------------
std::string entrypointKindName( PluginEntrypointKind kind )
{
    switch ( kind )
    {
    case PluginEntrypointKind::Native:
        return "native";
    case PluginEntrypointKind::Python:
        return "python";
    case PluginEntrypointKind::Manifest:
        return "manifest";
    }
    return "native";
}

bool entrypointKindFromName( const std::string &name, PluginEntrypointKind &out )
{
    if ( name == "native" )
    {
        out = PluginEntrypointKind::Native;
        return true;
    }
    if ( name == "python" )
    {
        out = PluginEntrypointKind::Python;
        return true;
    }
    if ( name == "manifest" )
    {
        out = PluginEntrypointKind::Manifest;
        return true;
    }
    return false;
}

bool PluginManifest::hasCapability( const std::string &capability ) const
{
    for ( const std::string &entry : capabilities )
    {
        if ( entry == capability )
            return true;
    }
    return false;
}

Json::Value PluginManifest::permissionsToJson() const
{
    Json::Value array( Json::arrayValue );
    for ( PluginPermission permission : permissions )
        array.append( pluginPermissionName( permission ) );
    return array;
}

Json::Value PluginManifest::toJson() const
{
    Json::Value json( Json::objectValue );
    json["manifest_version"] = manifestVersion;
    json["id"] = id;
    json["name"] = name;
    json["version"] = version;
    json["api_version"] = apiVersion;
    json["abi_version"] = abiVersion;
    if ( !description.empty() )
        json["description"] = description;
    if ( !vendor.empty() )
        json["vendor"] = vendor;
    if ( !license.empty() )
        json["license"] = license;
    if ( !platforms.empty() )
    {
        Json::Value platformsJson( Json::arrayValue );
        for ( const std::string &platform : platforms )
            platformsJson.append( platform );
        json["platforms"] = platformsJson;
    }
    if ( !entrypoint.empty() )
        json["entrypoint"] = entrypoint;
    json["entrypoint_kind"] = entrypointKindName( entrypointKind );
    if ( entrypointKind == PluginEntrypointKind::Python )
        json["python"] = python.toJson();
    Json::Value caps( Json::arrayValue );
    for ( const std::string &capability : capabilities )
        caps.append( capability );
    json["capabilities"] = caps;
    json["permissions"] = permissionsToJson();
    if ( !dependencies.empty() )
    {
        Json::Value deps( Json::arrayValue );
        for ( const std::string &dependency : dependencies )
            deps.append( dependency );
        json["dependencies"] = deps;
    }
    Json::Value operatorsJson( Json::arrayValue );
    for ( const ManifestOperator &op : operators )
        operatorsJson.append( op.toJson() );
    json["operators"] = operatorsJson;
    if ( !dataProviders.empty() )
    {
        Json::Value providers( Json::arrayValue );
        for ( const ManifestDataProvider &provider : dataProviders )
            providers.append( provider.toJson() );
        json["data_providers"] = providers;
    }
    if ( !modelRuntimes.empty() )
    {
        Json::Value runtimes( Json::arrayValue );
        for ( const ManifestModelRuntime &runtime : modelRuntimes )
            runtimes.append( runtime.toJson() );
        json["model_runtimes"] = runtimes;
    }
    if ( !agentTools.empty() )
    {
        Json::Value tools( Json::arrayValue );
        for ( const ManifestAgentTool &tool : agentTools )
            tools.append( tool.toJson() );
        json["agent_tools"] = tools;
    }
    if ( hasUi )
        json["ui"] = ui.toJson();
    if ( hasCartography )
        json["cartography"] = cartography.toJson();
    return json;
}

bool PluginManifest::fromJson( const Json::Value &json, PluginManifest &out,
                               PluginDiagnostic &error )
{
    if ( !json.isObject() )
    {
        error.code = PluginDiagnosticCode::ManifestInvalidJson;
        error.message = "manifest root must be a JSON object";
        return false;
    }
    out = PluginManifest();
    out.manifestVersion = json.get( "manifest_version", 0 ).asInt();
    out.id = json.get( "id", "" ).asString();
    out.name = json.get( "name", "" ).asString();
    out.version = json.get( "version", "" ).asString();
    out.apiVersion = json.get( "api_version", "" ).asString();
    out.abiVersion = json.get( "abi_version", 0 ).asInt();
    out.description = json.get( "description", "" ).asString();
    out.vendor = json.get( "vendor", "" ).asString();
    out.license = json.get( "license", "" ).asString();
    for ( const Json::Value &platform : json["platforms"] )
    {
        if ( platform.isString() )
            out.platforms.push_back( platform.asString() );
    }
    out.entrypoint = json.get( "entrypoint", "" ).asString();
    std::string kindError;
    const std::string kindName = json.get( "entrypoint_kind", "native" ).asString();
    if ( !entrypointKindFromName( kindName, out.entrypointKind ) )
    {
        error.code = PluginDiagnosticCode::ManifestInvalidField;
        error.field = "entrypoint_kind";
        error.message = "unknown entrypoint_kind '" + kindName + "'";
        return false;
    }
    if ( json.isMember( "python" ) )
    {
        std::string pythonError;
        if ( !ManifestPythonSection::fromJson( json["python"], out.python, pythonError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "python";
            error.message = pythonError;
            return false;
        }
    }
    for ( const Json::Value &capability : json["capabilities"] )
    {
        if ( capability.isString() )
            out.capabilities.push_back( capability.asString() );
    }
    out.permissions = parsePermissions( json["permissions"], out.warnings );
    for ( const Json::Value &dependency : json["dependencies"] )
    {
        if ( dependency.isString() )
            out.dependencies.push_back( dependency.asString() );
    }
    for ( const Json::Value &opJson : json["operators"] )
    {
        std::string opError;
        ManifestOperator op;
        if ( !ManifestOperator::fromJson( opJson, op, opError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "operators";
            error.message = opError;
            return false;
        }
        out.operators.push_back( std::move( op ) );
    }
    for ( const Json::Value &providerJson : json["data_providers"] )
    {
        std::string providerError;
        ManifestDataProvider provider;
        if ( !ManifestDataProvider::fromJson( providerJson, provider, providerError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "data_providers";
            error.message = providerError;
            return false;
        }
        out.dataProviders.push_back( std::move( provider ) );
    }
    for ( const Json::Value &runtimeJson : json["model_runtimes"] )
    {
        std::string runtimeError;
        ManifestModelRuntime runtime;
        if ( !ManifestModelRuntime::fromJson( runtimeJson, runtime, runtimeError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "model_runtimes";
            error.message = runtimeError;
            return false;
        }
        out.modelRuntimes.push_back( std::move( runtime ) );
    }
    for ( const Json::Value &toolJson : json["agent_tools"] )
    {
        std::string toolError;
        ManifestAgentTool tool;
        if ( !ManifestAgentTool::fromJson( toolJson, tool, toolError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "agent_tools";
            error.message = toolError;
            return false;
        }
        out.agentTools.push_back( std::move( tool ) );
    }
    if ( json.isMember( "ui" ) )
    {
        std::string uiError;
        if ( !ManifestUi::fromJson( json["ui"], out.ui, uiError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "ui";
            error.message = uiError;
            return false;
        }
        out.hasUi = true;
    }
    if ( json.isMember( "cartography" ) )
    {
        std::string cartographyError;
        if ( !ManifestCartography::fromJson( json["cartography"], out.cartography, cartographyError ) )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidField;
            error.field = "cartography";
            error.message = cartographyError;
            return false;
        }
        out.hasCartography = true;
    }
    return true;
}

bool loadManifestFromFile( const std::string &manifestPath, PluginManifest &out,
                           PluginDiagnostic &error )
{
    error = PluginDiagnostic{};
    error.file = manifestPath;
    std::ifstream input( manifestPath );
    if ( !input )
    {
        error.code = PluginDiagnosticCode::ManifestUnreadable;
        error.message = "cannot open manifest file";
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    Json::Value root;
    Json::Value parseErrors;
    Json::Reader reader;
    if ( !reader.parse( buffer.str(), root, false ) )
    {
        error.code = PluginDiagnosticCode::ManifestInvalidJson;
        error.message = "invalid JSON: " + reader.getFormattedErrorMessages();
        return false;
    }
    if ( root.isMember( "manifest_version" ) && root["manifest_version"].asInt() != 1 )
    {
        error.code = PluginDiagnosticCode::ManifestUnknownVersion;
        error.field = "manifest_version";
        error.message = "unsupported manifest_version "
                        + std::to_string( root["manifest_version"].asInt() )
                        + " (this host understands 1)";
        return false;
    }
    if ( !PluginManifest::fromJson( root, out, error ) )
        return false;
    error.pluginId = out.id;
    return true;
}

} // namespace exprs
