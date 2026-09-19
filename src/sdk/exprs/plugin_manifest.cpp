/***************************************************************************
 * exprs/plugin_manifest.cpp
 ***************************************************************************/
#include "exprs/plugin_manifest.h"

#include "exprs/json_reader.h"

#include <fstream>
#include <sstream>

namespace exprs {

namespace {
std::string requireString( const Json::Value &object, const char *key, std::string &error )
{
    if ( !object.isObject() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' read from a non-object value";
        return {};
    }
    const Json::Value &value = object[key];
    if ( !value.isString() || value.asString().empty() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a non-empty string";
        return {};
    }
    return value.asString();
}

/// Refuses a present-but-wrong-typed optional section (access/quotas/...).
/// JSON null stays legal (absent section); anything that is neither null nor
/// an object is a typed manifest failure, never a silently dropped subtree
/// that later deserializes into an abort.
bool objectSection( const Json::Value &json, const char *key, Json::Value &out,
                    std::string &error )
{
    if ( !json.isMember( key ) )
        return true;
    const Json::Value &value = json[key];
    if ( value.isNull() )
        return true;
    if ( !value.isObject() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be an object";
        return false;
    }
    out = value;
    return true;
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
    if ( !jsonread::readString( json, "type", out.type, error ) )
        return false;
    if ( !jsonread::readBool( json, "required", out.required, error ) )
        return false;
    if ( !jsonread::readString( json, "description", out.description, error ) )
        return false;
    if ( json.isMember( "default" ) )
        out.defaultValue = json["default"];
    if ( json.isMember( "enum" ) && json["enum"].isArray() )
        out.enumOptions = json["enum"];
    if ( !jsonread::member( json, "min" ).isNull() )
    {
        if ( !jsonread::readDouble( json, "min", out.minValue, error ) )
            return false;
        out.hasMin = true;
    }
    if ( !jsonread::member( json, "max" ).isNull() )
    {
        if ( !jsonread::readDouble( json, "max", out.maxValue, error ) )
            return false;
        out.hasMax = true;
    }
    if ( !jsonread::readString( json, "file_format", out.fileFormat, error ) )
        return false;
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
    if ( !jsonread::readBool( json, "inherit_environment", out.inheritEnvironment, error ) )
        return false;
    if ( !jsonread::readString( json, "working_directory", out.workingDirectoryParam, error ) )
        return false;
    if ( !jsonread::readInt( json, "timeout_seconds", out.timeoutSeconds, error ) )
        return false;
    if ( !jsonread::readInt( json, "stdout_limit_bytes", out.stdoutLimitBytes, error ) )
        return false;
    if ( !jsonread::readInt( json, "stderr_limit_bytes", out.stderrLimitBytes, error ) )
        return false;
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
    if ( !jsonread::readString( json, "group", out.group, error ) )
        return false;
    if ( !jsonread::readString( json, "description", out.description, error ) )
        return false;
    if ( !jsonread::readString( json, "memory_policy", out.memoryPolicy, error ) )
        return false;
    if ( !jsonread::readString( json, "determinism_grade", out.determinismGrade, error ) )
        return false;
    if ( !jsonread::readBool( json, "supports_cancel", out.supportsCancel, error ) )
        return false;
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
    if ( !jsonread::readString( json, "description", out.description, error ) )
        return false;
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
    if ( !jsonread::readString( json, "description", out.description, error ) )
        return false;
    if ( !jsonread::readBool( json, "gpu", out.gpu, error ) )
        return false;
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
    if ( !jsonread::readString( json, "category", out.category, error ) )
        return false;
    if ( !jsonread::readString( json, "description", out.description, error ) )
        return false;
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
    if ( !jsonread::readBool( json, "dock", out.dock, error ) )
        return false;
    if ( !jsonread::readBool( json, "menu_actions", out.menuActions, error ) )
        return false;
    if ( !jsonread::readBool( json, "settings_page", out.settingsPage, error ) )
        return false;
    if ( !jsonread::readString( json, "dock_title", out.dockTitle, error ) )
        return false;
    if ( !jsonread::readString( json, "settings_page_title", out.settingsPageTitle, error ) )
        return false;
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
    if ( !jsonread::readBool( json, "layout_items", out.layoutItems, error ) )
        return false;
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
    if ( !jsonread::readString( json, "package", out.package, error ) )
        return false;
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

std::string pluginRuntimeKindName( PluginRuntimeKind kind )
{
    switch ( kind )
    {
    case PluginRuntimeKind::InProcess:
        return "in-process";
    case PluginRuntimeKind::HostProcess:
        return "host-process";
    }
    return "in-process";
}

bool pluginRuntimeKindFromName( const std::string &name, PluginRuntimeKind &out )
{
    if ( name == "in-process" )
        out = PluginRuntimeKind::InProcess;
    else if ( name == "host-process" )
        out = PluginRuntimeKind::HostProcess;
    else
        return false;
    return true;
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
    // Structured declarations round-trip too: the discovery index, the
    // record snapshot and the host-process worker's load params all travel
    // through toJson(), and losing them silently disabled the capability
    // surfaces they carry (access/quotas, isolation 5.0+).
    if ( !access.isNull() )
        json["access"] = access;
    if ( !quotas.isNull() )
        json["quotas"] = quotas;
    if ( !conformance.isNull() )
        json["conformance"] = conformance;
    if ( !package.isNull() )
        json["package"] = package;
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
    // Round-trip the hosting strategy: the discovery index and the
    // host-process worker's load params serialize through this method; a
    // dropped "runtime" silently demoted host-process plugins to
    // in-process on every cache hit (the fixture crash-in-CLI bug class).
    json["runtime"] = pluginRuntimeKindName( runtime );
    // Project the python section whenever it carries content — the parser
    // reads it regardless of entrypoint kind, so a native plugin with an
    // auxiliary python payload must survive a round trip (found by the 9.0
    // completeness walk; the kind-only guard silently dropped it).
    if ( entrypointKind == PluginEntrypointKind::Python || !python.module.empty()
         || !python.package.empty() )
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
        for ( const ManifestModelRuntime &modelRuntime : modelRuntimes )
            runtimes.append( modelRuntime.toJson() );
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
    std::string fieldError;
    const auto failField = [&]( const char *field ) {
        error.code = PluginDiagnosticCode::ManifestInvalidField;
        error.field = field;
        error.message = fieldError;
        return false;
    };
    if ( !jsonread::readInt( json, "manifest_version", out.manifestVersion, fieldError ) )
        return failField( "manifest_version" );
    if ( !jsonread::readString( json, "id", out.id, fieldError ) )
        return failField( "id" );
    if ( !jsonread::readString( json, "name", out.name, fieldError ) )
        return failField( "name" );
    if ( !jsonread::readString( json, "version", out.version, fieldError ) )
        return failField( "version" );
    if ( !jsonread::readString( json, "api_version", out.apiVersion, fieldError ) )
        return failField( "api_version" );
    if ( !jsonread::readInt( json, "abi_version", out.abiVersion, fieldError ) )
        return failField( "abi_version" );
    if ( !jsonread::readString( json, "description", out.description, fieldError ) )
        return failField( "description" );
    if ( !jsonread::readString( json, "vendor", out.vendor, fieldError ) )
        return failField( "vendor" );
    if ( !jsonread::readString( json, "license", out.license, fieldError ) )
        return failField( "license" );
    for ( const Json::Value &platform : json["platforms"] )
    {
        if ( platform.isString() )
            out.platforms.push_back( platform.asString() );
    }
    if ( !jsonread::readString( json, "entrypoint", out.entrypoint, fieldError ) )
        return failField( "entrypoint" );
    std::string kindError;
    std::string kindName = "native";
    if ( !jsonread::readString( json, "entrypoint_kind", kindName, kindError ) )
    {
        error.code = PluginDiagnosticCode::ManifestInvalidField;
        error.field = "entrypoint_kind";
        error.message = kindError;
        return false;
    }
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
    std::string runtimeNameError;
    std::string runtimeName = "in-process";
    if ( !jsonread::readString( json, "runtime", runtimeName, runtimeNameError ) )
    {
        error.code = PluginDiagnosticCode::ManifestInvalidField;
        error.field = "runtime";
        error.message = runtimeNameError;
        return false;
    }
    if ( !pluginRuntimeKindFromName( runtimeName, out.runtime ) )
    {
        // Lenient here (forward compatibility); the validator refuses unknown
        // runtime values strictly so a load never guesses.
        out.runtime = PluginRuntimeKind::InProcess;
        out.runtimeUnknown = true;
        out.warnings.push_back( "unknown runtime '" + runtimeName + "'; loaded in-process" );
    }
    if ( !objectSection( json, "access", out.access, fieldError ) )
        return failField( "access" );
    if ( !objectSection( json, "quotas", out.quotas, fieldError ) )
        return failField( "quotas" );
    if ( !objectSection( json, "conformance", out.conformance, fieldError ) )
        return failField( "conformance" );
    if ( !objectSection( json, "package", out.package, fieldError ) )
        return failField( "package" );
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
    if ( root.isMember( "manifest_version" ) )
    {
        const Json::Value &version = root["manifest_version"];
        if ( !version.isInt() )
        {
            error.code = PluginDiagnosticCode::ManifestInvalidJson;
            error.field = "manifest_version";
            error.message = "manifest_version must be an integer";
            return false;
        }
        if ( version.asInt() != 1 )
        {
            error.code = PluginDiagnosticCode::ManifestUnknownVersion;
            error.field = "manifest_version";
            error.message = "unsupported manifest_version " + std::to_string( version.asInt() )
                            + " (this host understands 1)";
            return false;
        }
    }
    if ( !PluginManifest::fromJson( root, out, error ) )
        return false;
    error.pluginId = out.id;
    return true;
}

} // namespace exprs
