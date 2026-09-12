/***************************************************************************
 * exprs/plugin_index.h — local/offline plugin index (plugin-platform 9.0)
 *
 * Answers "what is available, compatible, and pinned?" by SCANNING plugin
 * directories (manifest-only: no dlopen, no code execution) and producing a
 * deterministic JSON index artifact. Purely local/offline by design — this
 * is an artifact generator and query, NOT a marketplace service and NOT a
 * network client.
 *
 * Index shape (sorted by id for reproducibility):
 * {
 *   "generatedAt": <caller-supplied ISO stamp | "">,
 *   "host": { "apiVersion", "abiVersion", "platform" },
 *   "sources": [dir...],
 *   "plugins": [ { "id", "version", "title", "path", "compatible",
 *                  "reason" } ]
 * }
 *
 * Pins: a caller-supplied {id: version} map annotates each entry with
 * "pin": "pinned" (matches) | "upgrade" | "downgrade" | "unpinned" — a pure
 * function, no state, no side effects.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace exprs {

struct PluginIndexEntry
{
    std::string id;
    std::string version;
    std::string title;
    std::string path;        ///< directory containing plugin.json
    bool compatible = false; ///< api/abi/platform match THIS host
    std::string reason;      ///< "" when compatible, else why not
};

class PluginIndex
{
public:
    /// Scans @p directories for plugin packages (each subdirectory carrying
    /// a parseable plugin.json is an entry; unreadable entries are skipped
    /// and counted in "skipped"). Entries are sorted by id.
    static std::vector<PluginIndexEntry> scan( const std::vector<std::string> &directories,
                                               int *skipped = nullptr );

    /// Builds the full index artifact (see header docs).
    static Json::Value build( const std::vector<std::string> &directories,
                              const std::string &generatedAt = {} );

    /// Annotates entries with pin status (pure function).
    static Json::Value applyPins( const Json::Value &index,
                                  const std::map<std::string, std::string> &pinnedVersions );

    /// True when @p manifest declares compatibility with THIS host
    /// (api_version/abi_version equal, platforms list empty or containing
    /// the running platform). @p reason explains an incompatible verdict.
    static bool isCompatible( const class PluginManifest &manifest, std::string &reason );
};

} // namespace exprs
