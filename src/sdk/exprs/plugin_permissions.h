/***************************************************************************
 * exprs/plugin_permissions.h — plugin permission declarations & policy
 *
 * Plugins declare the high-risk capabilities they need in their manifest
 * ("permissions"). The host applies a policy (allow / audit / deny) on top.
 * This is a declaration + diagnostics model, NOT an OS sandbox: the goal is
 * to make every high-risk capability explicit and reviewable before a
 * plugin loads.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace exprs {

enum class PluginPermission
{
    FilesystemRead,
    FilesystemWrite,
    Network,
    ExternalProcess,
    Python,
    Gpu,
    ProjectMutation,
};

/// Stable lowercase names used in manifests and diagnostics.
std::string pluginPermissionName( PluginPermission permission );
/// Returns nullopt-equivalent (FilesystemRead, false) pair when unknown.
bool pluginPermissionFromName( const std::string &name, PluginPermission &out );

/// All permissions a given capability kind implicitly requires (a manifest
/// that declares the capability but omits the permission is completed with a
/// warning, not rejected — see docs/plugins/permissions.md).
std::vector<PluginPermission> requiredPermissionsForCapability( const std::string &capability );

/// Host-side enforcement mode.
enum class PluginPolicyMode
{
    /// Record permission usage in diagnostics only (default).
    Audit,
    /// Refuse to load plugins exercising a permission not declared+granted.
    Enforce,
};

struct PluginPolicy
{
    PluginPolicyMode mode = PluginPolicyMode::Audit;
    std::vector<std::string> blockedPluginIds;     ///< SICNU_PLUGIN_BLOCK
    std::vector<std::string> allowedPluginIds;     ///< when non-empty: only these load (SICNU_PLUGIN_ALLOW)
    bool allowThirdPartyNative = true;             ///< SICNU_PLUGIN_DISABLE_NATIVE_THIRD_PARTY flips this
    bool allowThirdPartyPython = true;

    /// Builds the policy from environment variables:
    ///   SICNU_PLUGIN_POLICY=audit|enforce
    ///   SICNU_PLUGIN_BLOCK=id1,id2     SICNU_PLUGIN_ALLOW=id1,id2
    ///   SICNU_PLUGIN_DISABLE_NATIVE_THIRD_PARTY=1
    static PluginPolicy fromEnvironment();

    Json::Value toJson() const;
};

/// Parses a manifest "permissions" array. Unknown names produce warnings in
/// @p warnings but do not fail validation (forward compatibility).
std::vector<PluginPermission> parsePermissions( const Json::Value &manifestPermissions,
                                                std::vector<std::string> &warnings );

} // namespace exprs
