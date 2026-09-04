/***************************************************************************
 * exprs/version.h — ExpRS Developer Platform public SDK versioning
 *
 * This header is part of the ExpRS public SDK surface (installed under
 * include/exprs/). It is safe to include from third-party plugin builds.
 *
 * Versioning model (three independent axes — see docs/sdk/versioning.md):
 *
 *  1. SDK API version  (EXP_RS_PLUGIN_API_VERSION, "MAJOR.MINOR")
 *     The declared surface of the plugin interfaces and manifests.
 *     - MAJOR bumps break the plugin contract.
 *     - MINOR bumps are strictly additive (new optional methods, new
 *       manifest fields, new capability kinds). Plugins compiled against an
 *       older MINOR keep loading: a host accepts plugins whose MINOR is
 *       <= its own.
 *
 *  2. Plugin ABI version (EXP_RS_PLUGIN_ABI_VERSION, integer)
 *     C++ binary compatibility of the interface headers themselves
 *     (vtable layout, base-class layout, pinned third-party types).
 *     It is bumped whenever the ABI breaks without an API-visible change.
 *     The loader refuses plugins whose abi_version differs from the host's.
 *     The C++ ABI is NOT assumed stable: plugins must be rebuilt against
 *     each SDK release that bumps this number, and the conformance kit
 *     checks the gate before dlopen.
 *
 *  3. Manifest version (plugin.json "manifest_version", integer)
 *     Schema of the plugin manifest document. The v1 validator rejects
 *     unknown major versions and ignores unknown optional fields inside a
 *     known major version.
 *
 * Pinned cross-ABI types: the plugin interfaces intentionally use
 * Json::Value (jsoncpp) + std::string + std::vector, mirroring the
 * existing RSOperator contract. Qt types appear only in the separately
 * versioned UI contribution interface (exprs/plugin_ui.h), which is
 * build-locked to the host application and never treated as long-term ABI.
 ***************************************************************************/
#pragma once

#define EXP_RS_PLUGIN_API_VERSION_MAJOR 3
#define EXP_RS_PLUGIN_API_VERSION_MINOR 0
#define EXP_RS_PLUGIN_ABI_VERSION 1
#define EXP_RS_MANIFEST_VERSION 1

#define EXP_RS_STRINGIFY_(x) #x
#define EXP_RS_STRINGIFY(x) EXP_RS_STRINGIFY_(x)

/// Plugin API version as "MAJOR.MINOR" (e.g. "3.0").
#define EXP_RS_PLUGIN_API_VERSION \
    EXP_RS_STRINGIFY(EXP_RS_PLUGIN_API_VERSION_MAJOR) "." \
    EXP_RS_STRINGIFY(EXP_RS_PLUGIN_API_VERSION_MINOR)

/// SDK library version (semver of the SDK package itself).
#define EXP_RS_SDK_VERSION_MAJOR 3
#define EXP_RS_SDK_VERSION_MINOR 0
#define EXP_RS_SDK_VERSION_PATCH 0
#define EXP_RS_SDK_VERSION "3.0.0"

namespace exprs {

/// Parsed plugin API version used by the compatibility gate.
struct PluginApiVersion
{
    int major = 0;
    int minor = 0;
};

/// Returns the plugin API version this SDK was built with.
inline PluginApiVersion pluginApiVersion()
{
    return { EXP_RS_PLUGIN_API_VERSION_MAJOR, EXP_RS_PLUGIN_API_VERSION_MINOR };
}

/// Returns the plugin ABI version this SDK was built with.
inline int pluginAbiVersion() { return EXP_RS_PLUGIN_ABI_VERSION; }

/// Returns the manifest schema version this SDK understands (v1).
inline int supportedManifestVersion() { return EXP_RS_MANIFEST_VERSION; }

/**
 * Decides whether a plugin declaring @p declared can load into a host
 * running @p host. Rule: same major, declared.minor <= host.minor.
 */
inline bool isPluginApiCompatible( const PluginApiVersion &host, const PluginApiVersion &declared )
{
    return declared.major == host.major && declared.minor <= host.minor;
}

} // namespace exprs
