/***************************************************************************
 * exprs/plugin_capabilities.cpp
 ***************************************************************************/
#include "exprs/plugin_capabilities.h"

#include "exprs/path_policy.h"

#include <algorithm>
#include <filesystem>

namespace exprs {

namespace {

bool asBool( const Json::Value &value, bool &out )
{
    if ( value.isBool() )
    {
        out = value.asBool();
        return true;
    }
    return false;
}

std::vector<std::string> parsePathList( const Json::Value &value, bool &ok )
{
    std::vector<std::string> paths;
    ok = value.isArray();
    if ( !ok )
        return paths;
    for ( const Json::Value &item : value )
    {
        if ( !item.isString() || item.asString().empty() )
        {
            ok = false;
            return paths;
        }
        paths.push_back( item.asString() );
    }
    return paths;
}

} // namespace

Json::Value PluginCapabilities::toJson() const
{
    Json::Value json( Json::objectValue );
    Json::Value read( Json::arrayValue );
    for ( const std::string &root : fsReadRoots )
        read.append( root );
    json["filesystemRead"] = read;
    Json::Value write( Json::arrayValue );
    for ( const std::string &root : fsWriteRoots )
        write.append( root );
    json["filesystemWrite"] = write;
    json["network"] = network;
    json["externalProcess"] = externalProcess;
    if ( !gpuHint.empty() )
        json["gpuHint"] = gpuHint;
    json["workspaceMutation"] = workspaceMutation;
    json["projectMutation"] = projectMutation;
    Json::Value frameworks( Json::arrayValue );
    for ( const std::string &framework : modelFrameworks )
        frameworks.append( framework );
    json["modelFrameworks"] = frameworks;
    json["uiContribution"] = uiContribution;
    json["destructive"] = destructive;
    return json;
}

bool expandCapabilityPath( const std::string &declared, const std::string &pluginDir,
                           const std::string &workspaceRoot, const std::string &tempDir,
                           std::string &resolved, std::string &error )
{
    std::string expanded = declared;
    auto replaceAll = []( std::string &text, const std::string &from, const std::string &to ) {
        if ( from.empty() )
            return;
        size_t pos = 0;
        while ( ( pos = text.find( from, pos ) ) != std::string::npos )
        {
            text.replace( pos, from.size(), to );
            pos += to.size();
        }
    };
    if ( declared.find( "${workspace}" ) != std::string::npos && workspaceRoot.empty() )
    {
        error = "capability path uses ${workspace} but no workspace is configured: " + declared;
        return false;
    }
    replaceAll( expanded, "${plugin}", pluginDir );
    replaceAll( expanded, "${workspace}", workspaceRoot );
    replaceAll( expanded, "${temp}", tempDir );

    if ( expanded.find( "${" ) != std::string::npos )
    {
        error = "unknown placeholder in capability path: " + declared;
        return false;
    }
    if ( expanded.empty() )
    {
        error = "capability path expands to an empty string: " + declared;
        return false;
    }
    if ( !PathPolicy::isAbsolute( expanded ) )
    {
        // Declared paths must be anchored to a known root: relative fragments
        // would silently resolve against the host's cwd.
        error = "capability path is not rooted at ${plugin}/${workspace}/${temp}: " + declared;
        return false;
    }

    // Canonicalize what exists; for not-yet-existing tails (planned output
    // directories), canonicalize the deepest existing ancestor and re-attach
    // the remainder lexically so validation stays deterministic.
    resolved = PathPolicy::canonical( expanded );
    if ( resolved.empty() )
    {
        const std::filesystem::path original( expanded );
        std::filesystem::path parent = original.parent_path();
        while ( !parent.empty() )
        {
            const std::string canonicalParent =
                PathPolicy::canonical( parent.generic_string() );
            if ( !canonicalParent.empty() )
            {
                const std::filesystem::path rel = original.lexically_relative( parent );
                if ( !rel.empty() && rel.generic_string() != "." )
                {
                    resolved = ( std::filesystem::path( canonicalParent ) / rel )
                                   .lexically_normal()
                                   .generic_string();
                }
                else
                {
                    resolved = canonicalParent;
                }
                break;
            }
            const std::filesystem::path next = parent.parent_path();
            if ( next == parent )
                break;
            parent = next;
        }
    }
    if ( resolved.empty() )
    {
        error = "capability path cannot be canonicalized: " + declared;
        return false;
    }
    return true;
}

bool pathIsWithinRoot( const std::string &candidate, const std::string &root,
                       std::string &resolvedCandidate )
{
    if ( candidate.empty() || root.empty() )
        return false;
    // Canonicalize what exists; anchor not-yet-existing tails lexically at
    // the deepest existing ancestor (mirrors expandCapabilityPath).
    std::string resolved = PathPolicy::canonical( candidate );
    if ( resolved.empty() )
    {
        std::string anchorError;
        if ( !expandCapabilityPath( candidate, candidate, "", "", resolved, anchorError ) )
            return false;
        if ( resolved.empty() )
            return false;
    }
    resolvedCandidate = resolved;
    const std::string canonicalRoot = PathPolicy::canonical( root );
    if ( canonicalRoot.empty() )
        return false;
    if ( resolved == canonicalRoot )
        return true;
    std::string prefix = canonicalRoot;
    if ( prefix.back() != '/' )
        prefix += '/';
    return resolved.rfind( prefix, 0 ) == 0;
}

PluginCapabilityParseResult parsePluginAccess(
    const Json::Value &access, const std::string &pluginDir,
    const std::string &workspaceRoot, const std::string &tempDir )
{
    PluginCapabilityParseResult result;
    const Json::Value &json = access;
    if ( json.isNull() )
        return result; // no capabilities declared: defaults (deny-all) apply
    if ( !json.isObject() )
    {
        result.errors.push_back( "manifest access must be an object" );
        return result;
    }

    PluginCapabilities &caps = result.capabilities;

    const Json::Value &filesystem = json[ "filesystem" ];
    if ( !filesystem.isNull() )
    {
        if ( !filesystem.isObject() )
        {
            result.errors.push_back( "capabilities.filesystem must be an object" );
            return result;
        }
        for ( const auto &[ key, target ] : { std::pair<const char *, std::vector<std::string> *>{ "read", &caps.fsReadRoots },
                                              { "write", &caps.fsWriteRoots } } )
        {
            bool ok = false;
            if ( filesystem[ key ].isNull() )
                continue; // key not declared: no grant for that mode
            const std::vector<std::string> declared = parsePathList( filesystem[ key ], ok );
            if ( !ok )
            {
                result.errors.push_back(
                    std::string( "capabilities.filesystem." ) + key + " must be an array of paths" );
                return result;
            }
            for ( const std::string &path : declared )
            {
                std::string resolved;
                std::string error;
                if ( !expandCapabilityPath( path, pluginDir, workspaceRoot, tempDir, resolved, error ) )
                {
                    // Fail closed: an unresolvable root never becomes a grant.
                    result.errors.push_back( error );
                    continue;
                }
                target->push_back( resolved );
            }
        }
    }

    if ( !asBool( json.get( "network", Json::Value() ), caps.network )
         && !json[ "network" ].isNull() )
        result.errors.push_back( "capabilities.network must be a boolean" );

    if ( !asBool( json.get( "externalProcess", Json::Value() ), caps.externalProcess )
         && !json[ "externalProcess" ].isNull() )
        result.errors.push_back( "capabilities.externalProcess must be a boolean" );

    const Json::Value &gpu = json[ "gpu" ];
    if ( !gpu.isNull() )
    {
        if ( gpu.isString() )
        {
            caps.gpuHint = gpu.asString();
        }
        else if ( gpu.isObject() )
        {
            const Json::Value &hint = gpu[ "hint" ];
            if ( hint.isString() )
                caps.gpuHint = hint.asString();
            else
                result.errors.push_back( "capabilities.gpu.hint must be a string" );
        }
        else
        {
            result.errors.push_back( "capabilities.gpu must be a string or object" );
        }
    }

    const Json::Value &workspace = json[ "workspace" ];
    if ( !workspace.isNull() )
    {
        if ( workspace.isObject() )
        {
            if ( !asBool( workspace.get( "mutate", Json::Value() ), caps.workspaceMutation )
                 && !workspace[ "mutate" ].isNull() )
                result.errors.push_back( "capabilities.workspace.mutate must be a boolean" );
        }
        else
        {
            result.errors.push_back( "capabilities.workspace must be an object" );
        }
    }

    const Json::Value &project = json[ "project" ];
    if ( !project.isNull() )
    {
        if ( project.isObject() )
        {
            if ( !asBool( project.get( "mutate", Json::Value() ), caps.projectMutation )
                 && !project[ "mutate" ].isNull() )
                result.errors.push_back( "capabilities.project.mutate must be a boolean" );
        }
        else
        {
            result.errors.push_back( "capabilities.project must be an object" );
        }
    }

    const Json::Value &modelProvider = json[ "modelProvider" ];
    if ( !modelProvider.isNull() )
    {
        if ( modelProvider.isObject() )
        {
            const Json::Value &frameworks = modelProvider[ "frameworks" ];
            if ( !frameworks.isNull() )
            {
                bool ok = false;
                caps.modelFrameworks = parsePathList( frameworks, ok );
                if ( !ok )
                    result.errors.push_back(
                        "capabilities.modelProvider.frameworks must be an array of strings" );
            }
        }
        else
        {
            result.errors.push_back( "capabilities.modelProvider must be an object" );
        }
    }

    if ( !asBool( json.get( "ui", Json::Value() ), caps.uiContribution )
         && !json[ "ui" ].isNull() )
        result.errors.push_back( "capabilities.ui must be a boolean" );

    if ( !asBool( json.get( "destructive", Json::Value() ), caps.destructive )
         && !json[ "destructive" ].isNull() )
        result.errors.push_back( "capabilities.destructive must be a boolean" );

    return result;
}

bool manifestDeclaresAccess( const Json::Value &access )
{
    return access.isObject();
}

int accessBool( const Json::Value &access, const char *key )
{
    if ( !access.isObject() )
        return -1;
    const Json::Value &value = access[ key ];
    if ( !value.isBool() )
        return -1;
    return value.asBool() ? 1 : 0;
}

bool modelFrameworkAllowed( const Json::Value &access, const std::string &framework )
{
    if ( !manifestDeclaresAccess( access ) )
        return true;
    const Json::Value &modelProvider = access[ "modelProvider" ];
    if ( !modelProvider.isObject() )
        return true;
    const Json::Value &frameworks = modelProvider[ "frameworks" ];
    if ( !frameworks.isArray() )
        return true; // declared modelProvider without a frameworks list: unbounded
    for ( const Json::Value &entry : frameworks )
    {
        if ( entry.isString() && entry.asString() == framework )
            return true;
    }
    return false;
}

std::string capabilityEnforcementLevelName( CapabilityEnforcementLevel level )
{
    switch ( level )
    {
    case CapabilityEnforcementLevel::EnforcedHost:
        return "enforced-host";
    case CapabilityEnforcementLevel::EnforcedWorker:
        return "enforced-worker";
    case CapabilityEnforcementLevel::EnforcedOs:
        return "enforced-os";
    case CapabilityEnforcementLevel::Advisory:
        return "advisory";
    case CapabilityEnforcementLevel::AuditOnly:
        return "audit-only";
    case CapabilityEnforcementLevel::RefusedByContract:
        return "refused-by-contract";
    }
    return "unknown";
}

std::vector<CapabilityEnforcementEntry> pluginCapabilityEnforcementMatrix()
{
    using L = CapabilityEnforcementLevel;
    return {
        // -- filesystem ---------------------------------------------------
        { "filesystem.readRoots", "all", L::AuditOnly,
          "declared and validated at load; no gate intercepts plugin reads "
          "(native code cannot be confined from itself)" },
        { "filesystem.writeRoots", "host-process", L::EnforcedWorker,
          "operator workDir handed to plugin code is refused outside every "
          "declared write root and the plugin temp directory (E5005); opt-in: "
          "only plugins that DECLARE write roots are gated; NOT an OS sandbox" },
        { "filesystem.writeRoots", "in-process", L::AuditOnly,
          "declaration + audit + diagnostics only" },
        { "network", "all", L::RefusedByContract,
          "network access of plugin code is never claimed to be intercepted" },
        { "externalProcess", "host-process", L::EnforcedWorker,
          "the operator workDir seam is gated as above; quotas bound worker "
          "children where the OS allows" },
        { "externalProcess", "in-process", L::EnforcedHost,
          "manifest external-tool operators refuse to spawn when the manifest "
          "access object declares externalProcess:false (E5005); native code "
          "spawning processes directly cannot be intercepted (audit-only)" },
        { "gpu", "all", L::Advisory,
          "gpuHint is passed to the model runtime as a preference; never a "
          "hard assignment" },
        { "modelProvider.frameworks", "host-process", L::EnforcedHost,
          "model runtime load is refused for frameworks outside the declared "
          "list when the manifest declares an access object (E5005)" },
        { "modelProvider.frameworks", "in-process", L::EnforcedHost,
          "same host-side registration gate as the host-process runtime" },
        { "ui", "all", L::EnforcedHost,
          "declarative/in-process UI is refused (E5005) when the manifest "
          "access object explicitly declares ui:false" },
        { "workspace.mutate", "in-process", L::AuditOnly,
          "declaration + audit; mutation policy is owned by the workspace "
          "governance layer, not the plugin runtime" },
        { "project.mutate", "in-process", L::AuditOnly,
          "declaration + audit only" },
        { "destructive", "all", L::AuditOnly,
          "surfaced to users in inspect/doctor; no runtime gate" },
        // -- quotas (exprs/plugin_quotas.h) --------------------------------
        { "quotas.maxRequestConcurrency", "host-process", L::EnforcedHost,
          "FIFO concurrency gate; overflow refuses typed (E6007)" },
        { "quotas.requestDeadlineMs", "host-process", L::EnforcedHost,
          "per-request ceiling; timeout escalates through the kill ladder" },
        { "quotas.maxResponseBytes", "host-process", L::EnforcedWorker,
          "worker refuses to write larger frames (E6003); protocol 1.2 adds "
          "the host-side receive cap (defense in depth)" },
        { "quotas.maxRequestBytes", "host-process", L::EnforcedHost,
          "protocol 1.2 per-direction cap; the host refuses to send larger "
          "request frames (E6003)" },
        { "quotas.workerMemoryBytes", "host-process", L::EnforcedOs,
          "Windows: job object memory limit (exact); POSIX: pre-exec "
          "RLIMIT_AS (coarse, address space not RSS, documented)" },
        { "quotas.workerCpuRatePercent", "host-process", L::EnforcedOs,
          "Windows: job object CPU rate control; POSIX: advisory only "
          "(RLIMIT_NPROC-class control does not exist per-process)" },
        { "quotas.maxChildProcesses", "host-process", L::EnforcedOs,
          "Windows: job object ActiveProcessLimit (exact); POSIX: advisory "
          "(documented)" },
        // -- isolation -----------------------------------------------------
        { "isolation.processTreeCleanup", "host-process", L::EnforcedOs,
          "POSIX: setpgid + process-group SIGKILL (+ reaped after self-death); "
          "Windows: job object kill-on-close" },
        { "isolation.crashRestart", "host-process", L::EnforcedHost,
          "bounded restart policy (3 respawns / 60 s); exhaustion refuses "
          "typed (E6005)" },
    };
}

Json::Value pluginCapabilityEnforcementMatrixJson()
{
    Json::Value array( Json::arrayValue );
    for ( const CapabilityEnforcementEntry &entry : pluginCapabilityEnforcementMatrix() )
    {
        Json::Value row( Json::objectValue );
        row["capability"] = entry.capability;
        row["runtimeScope"] = entry.runtimeScope;
        row["level"] = capabilityEnforcementLevelName( entry.level );
        row["note"] = entry.note;
        array.append( row );
    }
    return array;
}

} // namespace exprs
