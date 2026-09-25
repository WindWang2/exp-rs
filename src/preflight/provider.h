// provider.h — the authority seams of the Scientific Preflight Engine.
//
// Facts enter the engine ONLY through these two read-only interfaces: the
// scientific-state passport seam (asset facts) and the capability-knowledge
// seam (merged operator entries). Providers never guess, never fabricate and
// never register anything; an authority that cannot answer reports that
// through the typed FactStatus, so rules can degrade to explicit unknowns.
//
// The Memory* implementations are the in-library fake used by tests and by
// consumers that have not wired a real authority yet.

#pragma once

#include "preflight/facts.h"

#include <json/json.h>

#include <functional>
#include <map>
#include <string>

namespace sicnu::preflight {

struct SlotFactsResult
{
  FactStatus status = FactStatus::Unavailable;
  SlotFacts facts;
  std::string detail;  ///< Set for Unknown / Unavailable; says what is missing.
};

struct CapabilityEntryResult
{
  FactStatus status = FactStatus::Unavailable;
  Json::Value entry{ Json::objectValue };  ///< Merged capability entry (Available only).
  std::string detail;
  /// True when the operator declares variant-parameterized policies but no
  /// variant matched the given params: policy keys absent from the merged
  /// entry are declared-but-unconsultable, not undeclared (rules must gate).
  bool variantPoliciesDropped = false;
};

/// Asset-facts authority (scientific_state passport projection).
class IAssetFactsProvider
{
  public:
    virtual ~IAssetFactsProvider() = default;
    virtual SlotFactsResult slotFacts( const std::string &assetRef ) const = 0;
};

/// Capability-knowledge authority. Implementations hand out the MERGED entry
/// for an operator id (extends chain + family default + first matching
/// variant) — the merge itself stays with the authority, preflight never
/// re-implements an authority.
class ICapabilityProvider
{
  public:
    virtual ~ICapabilityProvider() = default;
    virtual CapabilityEntryResult entryForOperator( const std::string &operatorId,
                                                    const Json::Value &variantParams ) const = 0;
};

/// Map-backed fake: unregistered references are typed unknowns, never
/// fabricated facts.
class MemoryFactsProvider : public IAssetFactsProvider
{
  public:
    void set( const std::string &assetRef, const SlotFacts &facts );
    void setUnknown( const std::string &assetRef, const std::string &detail );
    void setUnavailable( const std::string &assetRef, const std::string &detail );

    SlotFactsResult slotFacts( const std::string &assetRef ) const override;

    /// Mutable access for tests composing scenarios incrementally.
    SlotFacts &at( const std::string &assetRef );

  private:
    std::map<std::string, SlotFactsResult> entries_;
};

/// Map-backed capability fake with the same fail-closed defaults.
class MemoryCapabilityProvider : public ICapabilityProvider
{
  public:
    void setEntry( const std::string &operatorId, Json::Value entry );
    void markUnavailable( const std::string &detail );

    CapabilityEntryResult entryForOperator( const std::string &operatorId,
                                            const Json::Value &variantParams ) const override;

  private:
    std::map<std::string, Json::Value> entries_;
    bool unavailable_ = false;
    std::string detail_;
};

} // namespace sicnu::preflight
