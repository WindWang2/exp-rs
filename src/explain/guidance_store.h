/***************************************************************************
 * guidance_store.h — file-backed authored-guidance store (exp.step_guidance.v1)
 *
 * Loads data/explain/guidance/*.json. Following the platform's authored
 * content discipline (ADR 0146): the JSON files are the single source of
 * truth, loading is fail-closed (invalid files are skipped and recorded,
 * never partially applied), there is no fallback content, and parsing
 * uses the jsoncpp safe-reader pattern (stackLimit) so hostile documents
 * cannot recurse the process.
 ***************************************************************************/
#pragma once

#include "explain/authored_guidance.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sicnu::explain
{

struct GuidanceLoadProblem
{
  std::string file;
  std::string code; // typed, machine-readable
  std::string message;
};

class GuidanceStore final : public IAuthoredGuidance
{
public:
  // Never throws: unreadable/invalid files are recorded in @p problems and
  // skipped. The returned store is always non-null (possibly empty).
  static std::unique_ptr<GuidanceStore> loadFromDirectory(
    const std::string &directory, std::vector<GuidanceLoadProblem> &problems );

  // Role-specific entries win over the generic entry for the same operator;
  // an unknown role falls back to the generic entry.
  std::optional<StepGuidance> guidanceFor( const std::string &operatorId,
                                           const std::string &role ) const override;

  size_t entryCount() const { return entries_.size(); }
  const std::vector<GuidanceLoadProblem> &loadProblems() const { return problems_; }

private:
  struct Key
  {
    std::string operatorId;
    std::string role;
    bool operator<( const Key &other ) const
    {
      if ( operatorId != other.operatorId )
        return operatorId < other.operatorId;
      return role < other.role;
    }
  };

  std::map<Key, StepGuidance> entries_;
  std::vector<GuidanceLoadProblem> problems_;
};

} // namespace sicnu::explain
