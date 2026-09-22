/***************************************************************************
 * authored_guidance.h — authored teaching-guidance value objects
 *
 * Authored guidance is the human-written curriculum layer: narrative
 * purpose, prerequisites, per-parameter rationale ("why this value"),
 * skip consequences and literature references. It is loaded from
 * data/explain/guidance (exp.step_guidance.v1) and is always presented as
 * authored_guidance provenance — never as a runtime fact. The builder
 * cross-checks it against live operator schemas; contradictions are
 * surfaced, never silently merged.
 ***************************************************************************/
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace sicnu::explain
{

struct AuthoredParameterRationale
{
  std::string parameter;
  std::string rationale;
  std::string misconfigurationConsequence;
};

struct AuthoredStateNarrative
{
  std::string before; // teaching-expectation state token(s)
  std::string after;
};

struct AuthoredReference
{
  std::string title;
  std::string kind; // textbook | paper | standard | doc
  std::string locator;
  std::string note;
};

struct AuthoredSkipConsequence
{
  std::string summary;
  std::string detail;
  std::vector<std::string> downstreamRoles;
};

struct StepGuidance
{
  std::string operatorId;
  std::string role; // empty = generic entry; otherwise role-specific override

  std::string purpose;
  std::string whenToUse;
  std::vector<std::string> prerequisitesNote;
  std::vector<std::string> assumptions;
  std::vector<AuthoredParameterRationale> parameterRationale;
  std::optional<AuthoredStateNarrative> stateNarrative;
  std::optional<AuthoredSkipConsequence> skipConsequence;
  std::vector<AuthoredReference> references;
  std::string teachingNote;
};

// Stable authored-guidance lookup. Implementations: GuidanceStore (file
// backed, Slice C) and test fakes.
struct IAuthoredGuidance
{
  virtual ~IAuthoredGuidance() = default;
  // Role-specific entries win over the generic entry for the same operator.
  virtual std::optional<StepGuidance> guidanceFor( const std::string &operatorId,
                                                   const std::string &role ) const = 0;
};

} // namespace sicnu::explain
