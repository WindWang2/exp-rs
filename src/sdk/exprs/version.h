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

#include <stdexcept>
#include <string>
#include <system_error>

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

/**
 * Parses a "MAJOR.MINOR" API-version literal (used by the min/max host
 * range negotiation, plugin-platform 12.0). Returns false for anything
 * that is not exactly two non-empty DIGIT-ONLY parts (no sign, no spaces,
 * no extra components), so a malformed bound fails closed as a typed
 * validation error.
 */
inline bool parseApiVersion( const std::string &text, PluginApiVersion &out )
{
    const std::string separator = ".";
    const size_t position = text.find( separator );
    if ( position == std::string::npos || position == 0 || position + 1 >= text.size() )
        return false;
    const std::string majorText = text.substr( 0, position );
    const std::string minorText = text.substr( position + 1 );
    if ( minorText.find( separator ) != std::string::npos )
        return false;
    // Digit-only pre-check: std::stoi would happily accept "-1" / "+3" /
    // " 3" (and only fail on trailing junk), which would let a signed bound
    // through the compatibility gate.
    const auto digitsOnly = []( const std::string &part ) {
        for ( char c : part )
        {
            if ( !( c >= '0' && c <= '9' ) )
                return false;
        }
        return !part.empty();
    };
    if ( !digitsOnly( majorText ) || !digitsOnly( minorText ) )
        return false;
    try
    {
        size_t consumed = 0;
        const int major = std::stoi( majorText, &consumed );
        if ( consumed != majorText.size() )
            return false;
        const int minor = std::stoi( minorText, &consumed );
        if ( consumed != minorText.size() )
            return false;
        out = PluginApiVersion{ major, minor };
        return true;
    }
    catch ( const std::exception & )
    {
        return false;
    }
}

/**
 * WP1 (plugin-platform 12.0): the host-API range a plugin was validated
 * against. A manifest may OPTIONALLY declare "min_host_api"/"max_host_api"
 * ("MAJOR.MINOR"); an empty @p minHostApi / maxHostApi means the bound was
 * not declared and the plain isPluginApiCompatible rule applies. When
 * declared, the host version must sit inside [min, max] INCLUSIVE; an empty
 * string bound is skipped so a manifest can declare only one side. On refusal
 * @p offendingField names the bound that rejected the host ("min_host_api" or
 * "max_host_api"), so a diagnostic can attribute the failure to the exact
 * manifest field instead of guessing.
 */
inline bool isHostApiWithinRange( const PluginApiVersion &host, const std::string &minHostApi,
                                  const std::string &maxHostApi, std::string &error,
                                  std::string &offendingField )
{
    if ( !minHostApi.empty() )
    {
        PluginApiVersion minimum{};
        if ( !parseApiVersion( minHostApi, minimum ) )
        {
            error = "min_host_api '" + minHostApi + "' must be MAJOR.MINOR";
            offendingField = "min_host_api";
            return false;
        }
        if ( host.major != minimum.major ? host.major < minimum.major
                                         : host.minor < minimum.minor )
        {
            error = "host API " + std::to_string( host.major ) + "." + std::to_string( host.minor )
                    + " is below the plugin's declared minimum " + minHostApi;
            offendingField = "min_host_api";
            return false;
        }
    }
    if ( !maxHostApi.empty() )
    {
        PluginApiVersion maximum{};
        if ( !parseApiVersion( maxHostApi, maximum ) )
        {
            error = "max_host_api '" + maxHostApi + "' must be MAJOR.MINOR";
            offendingField = "max_host_api";
            return false;
        }
        if ( host.major != maximum.major ? host.major > maximum.major
                                         : host.minor > maximum.minor )
        {
            error = "host API " + std::to_string( host.major ) + "." + std::to_string( host.minor )
                    + " is above the plugin's declared maximum " + maxHostApi;
            offendingField = "max_host_api";
            return false;
        }
    }
    return true;
}

} // namespace exprs
