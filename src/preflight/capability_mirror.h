// capability_mirror.h — read-only projection of the capability knowledge
// mirror for the Scientific Preflight Engine (RS14-02).
//
// The mirror documents (data/agent/capabilities/*.json, top-level arrays of
// entries) stay the single source of truth. This projection only READS them:
//   * extends chains merge parent-first, then the family default, then the
//     entry itself (child wins; top-level key replacement);
//   * a variant applies when every when.param matches the given params
//     (first matching variant in declaration order);
//   * the keys "extends" and "when" never leak into a merged entry;
//   * cycles and depth are bounded; malformed documents are counted in
//     problems() and fail the projection closed (healthy() == false, queries
//     answer Unavailable) — never silently skipped.
//
// The Qt-side runtime authority (sicnu::agent::harness::CapabilityKnowledge)
// keeps its own merge for consumers that link Qt; a future agent-tool track
// should delegate to it. Until then this projection is the Qt-free read path
// and is pinned against the real mirror documents by tests.

#pragma once

#include "preflight/provider.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::preflight {

/// Variant selector helper: {"key": "value"} — the params shape consumed by
/// entryForOperator.
Json::Value makeVariantParams( const std::string &key, const std::string &value );

class CapabilityMirrorProjection : public ICapabilityProvider
{
  public:
    /// Adds one parsed document (must be a top-level array of entries).
    /// @p origin names the document in problems(); a non-array or duplicate
    /// entry id fails closed.
    void addDocument( const Json::Value &documentArray, const std::string &origin );

    /// Reads every *.json file in @p directory (std::filesystem, bounded).
    /// Returns the number of documents loaded. Marks the projection
    /// configured; a missing/unreadable directory records a problem.
    int loadDirectory( const std::string &directory );

    bool configured() const;
    bool healthy() const;
    const std::vector<std::string> &problems() const;

    /// ICapabilityProvider: merged entry for @p operatorId with the documented
    /// merge semantics, or typed Unknown ("not declared" / "not configured")
    /// / Unavailable ("configured but failed to load").
    CapabilityEntryResult entryForOperator( const std::string &operatorId,
                                            const Json::Value &variantParams ) const override;

    /// All entry ids across documents, sorted.
    std::vector<std::string> entryIds() const;

  private:
    struct Entry
    {
        Json::Value value;
        std::string origin;
    };
    const Entry *findEntry( const std::string &id ) const;
    Json::Value mergeEntry( const Entry &entry ) const;

    std::vector<Entry> entries_;
    std::vector<std::string> problems_;
    bool configured_ = false;
    bool loadFailed_ = false;
};

} // namespace sicnu::preflight
