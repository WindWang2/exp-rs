/***************************************************************************
  render_teaching.h — the human surface of a verification report (RS14-14)

  The student's question is not "what is the roll-up" but "can I hand this in".
  That question has exactly three honest answers:

    trustworthy      every declared expectation was met;
    not_trustworthy  at least one declared expectation was violated;
    unverified       the evidence needed to decide was never obtained.

  The third one is where renderers fail, and they fail in a specific direction:
  "we could not determine" gets worded as a softer version of "correct", because
  most of the sentence is shared ("nothing was found wrong with..."). A student
  who reads that submits an unverified result. So this surface obeys one hard
  rule: when the verdict is `unverified`, NO field of the view may use the
  vocabulary of the `trustworthy` verdict. tests/test_verifier_render_14.cpp
  pins that rule with a keyword guard over the whole serialized view.

  Two further rules, both about acting rather than judging:

    - every check renders all four of expected / observed / whyItMatters /
      howToFix. A blank cell is not a neutral cell — a student cannot act on
      an empty "how do I fix this", and a renderer that leaves it blank has
      quietly shifted the work back onto her.
    - every number reaching this surface goes through `roundSignificant`. A raw
      double in a view is a reproducibility defect: 0.1 + 0.2 and 0.3 must not
      render as different text.

  The three `derived*` functions below are the SHARED derivation that both this
  view and render_agent.cpp read, so the two surfaces cannot drift apart. They
  read the report's structure (nodes, results) rather than its summary fields,
  because a summary that claims Pass over Indeterminate nodes is precisely the
  fail-open this slice exists to catch.
 ***************************************************************************/
#pragma once

#include "verification/report.h"
#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::verification
{

inline constexpr const char *kTeachingSchema = "exp.verification.teaching.v1";

/// The closed verdict vocabulary. Three values; "sort of trustworthy" is not one.
inline constexpr const char *kTeachingVerdictTrustworthy = "trustworthy";
inline constexpr const char *kTeachingVerdictNotTrustworthy = "not_trustworthy";
inline constexpr const char *kTeachingVerdictUnverified = "unverified";

/// One check, as a student reads it: what was wanted, what was seen, why it
/// matters, and what to do about it. All four text fields are always populated.
struct TeachingCheckView
{
    std::string checkId;
    std::string title;
    std::string expected;
    std::string observed;
    std::string whyItMatters;
    std::string howToFix;
    CheckStatus status = CheckStatus::Indeterminate;
    std::string failureCode;

    Json::Value toJson() const;
};

struct TeachingView
{
    std::string schema = kTeachingSchema;

    /// "trustworthy" | "not_trustworthy" | "unverified"
    std::string verdict;
    /// One sentence the student can act on, in the same three-way vocabulary.
    std::string headline;
    /// Why the verdict is what it is. Never empty: a verdict without a reason
    /// is an order, not an explanation.
    std::vector<std::string> reasons;
    std::vector<TeachingCheckView> checks;

    Json::Value toJson() const;
};

TeachingView renderTeaching( const VerificationReport &report );

/// --- shared derivation (also used by render_agent.cpp) -------------------

/// The task-level status this report's STRUCTURE supports:
///   - the level-2 roll-up over its nodes, when it has any;
///   - the flat roll-up over its results, when it has none;
///   - Indeterminate when it has neither. Zero evidence is never a pass.
/// The summary fields (`status`, `outcome`) are deliberately NOT consulted: a
/// renderer that trusts them renders whatever the roll-up claimed, which is
/// exactly what a mangled roll-up gets wrong.
CheckStatus derivedStatus( const VerificationReport &report );

/// Failure codes carried by the report's non-Pass results, deduplicated and
/// sorted. Nothing is invented and nothing is dropped: the set is exactly the
/// codes the results carry.
std::vector<std::string> derivedFailureCodes( const VerificationReport &report );

/// Ids of the nodes that are not Pass, in node order. Empty when there is
/// nothing to point at — an empty report has no blocker, and inventing one
/// would be the same dishonesty as inventing a pass.
std::vector<std::string> derivedBlockingNodes( const VerificationReport &report );

} // namespace sicnu::verification
