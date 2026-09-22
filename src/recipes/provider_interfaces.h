// src/recipes/provider_interfaces.h
#pragma once

//
// RS14-20 provider seams. The recipe module is Qt-free and must not depend on
// the Processing Registry / CapabilityCatalog (heavy, app-coupled) or on any
// sibling RS14 track that has not merged. These minimal interfaces let tests
// run against fakes and let future wiring land in one adapter each:
//
//   IOperatorCatalog  ← AtomicAlgorithmRegistry (live descriptors) or the
//                       D8 capability sidecars (data/processing/algorithm_meta)
//   See docs/recipes-integration.md for the wiring map.
//

#include <map>
#include <string>
#include <vector>

namespace sicnu::recipes {

/// Minimal read-only view of "which operators exist and what params they
/// take". The compiler uses it ONLY for existence/param-name evidence —
/// semantics stay with the real registries.
class IOperatorCatalog
{
  public:
    virtual ~IOperatorCatalog() = default;

    /// True when `operatorId` (e.g. "rs:spectral_index") is a known operator.
    virtual bool hasOperator( const std::string &operatorId ) const = 0;

    /// Declared parameter names for the operator (empty when unknown or when
    /// the provider cannot enumerate them — callers must not treat empty as
    /// "no params").
    virtual std::vector<std::string> paramNames( const std::string &operatorId ) const = 0;
};

/// Directory-backed catalog over operator id sidecars. Two shapes are read:
///   * single-object docs carrying "id" (data/processing/algorithm_meta/**)
///   * array-of-objects docs (data/agent/capabilities/*.json bundles)
/// `family:*` entries (family defaults) are not operators and are skipped.
/// Offline, read-only, deterministic (sorted scan). This is the shipped
/// default for the compile tool — a live-registry adapter can replace it
/// later without touching the compiler.
class SidecarOperatorCatalog final : public IOperatorCatalog
{
  public:
    /// `directory` may be the algorithm_meta dir itself; the scan also picks
    /// up the `capability/` subdir. Missing dir → empty catalog (hasOperator
    /// always false — callers see `unknown_operator` diagnostics, not crashes).
    explicit SidecarOperatorCatalog( const std::string &directory );

    /// Multi-source variant: union of several directories (e.g.
    /// algorithm_meta + data/agent/capabilities).
    explicit SidecarOperatorCatalog( std::vector<std::string> directories );

    bool hasOperator( const std::string &operatorId ) const override;
    std::vector<std::string> paramNames( const std::string &operatorId ) const override;

    /// Ids loaded, sorted.
    const std::vector<std::string> &operatorIds() const { return mIds; }

  private:
    std::vector<std::string> mIds;
};

/// Test/embedding seam: an explicit id set (+ optional param names).
class FakeOperatorCatalog final : public IOperatorCatalog
{
  public:
    void add( const std::string &operatorId, std::vector<std::string> params = {} )
    {
      mIds.push_back( operatorId );
      mParams[operatorId] = std::move( params );
    }

    bool hasOperator( const std::string &operatorId ) const override;
    std::vector<std::string> paramNames( const std::string &operatorId ) const override;

  private:
    std::vector<std::string> mIds;
    std::map<std::string, std::vector<std::string>> mParams;
};

} // namespace sicnu::recipes
