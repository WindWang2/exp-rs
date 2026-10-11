/***************************************************************************
  report.h — the two-level roll-up and the report that carries it

  Two levels, because two different questions get asked at two different times:

    Level 1 — did THIS NODE satisfy its postcondition?
              Asked immediately after the node runs, by whoever decides whether
              the next node may start.
    Level 2 — is THIS TASK's outcome trustworthy?
              Asked once, at the end, to decide whether to ship or replan.

  Collapsing the two into one list is what produces the failures this track was
  created to fix: with a flat list you cannot tell "node 3 is fine but node 7
  is unknown" from "everything is fine".

  The roll-up obeys exactly the lattice in status_lattice.h, applied twice:

    - Fail anywhere dominates.
    - A single Indeterminate node keeps the task Indeterminate. It is NOT
      diluted by a majority of Pass nodes — the whole point of the third value
      is that unknown + many goods is still unknown, not good.
    - Nothing at all => Indeterminate. Zero nodes is zero evidence.

  `blockingNodes` names the nodes that stopped the roll-up, so an Agent reading
  the report can act on a specific node instead of re-running everything. The
  empty task is Indeterminate but has NO blockers — there is nothing to point
  at, and inventing one would be as dishonest as inventing a pass.
 ***************************************************************************/
#pragma once

#include "verification/failure_codes.h"
#include "verification/spec.h"
#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::verification
{

inline constexpr int kVerificationReportSchemaVersion = 1;
inline constexpr const char *kVerificationReportSchema = "exp.verification.report.v1";

/// Level 1: one node's postcondition verdict.
struct NodeOutcome
{
    std::string nodeId;
    CheckStatus status = CheckStatus::Indeterminate;
    std::vector<std::string> checkIds;    ///< in the spec's declaration order
    std::vector<std::string> failureCodes; ///< deduplicated, sorted
    std::vector<std::string> promotedCheckIds;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, NodeOutcome &out, std::string &error );
};

/// Level 2: the whole task's verdict.
struct TaskOutcome
{
    CheckStatus status = CheckStatus::Indeterminate;

    /// Nodes that are not Pass, in roll-up order — i.e. what is stopping us.
    std::vector<std::string> blockingNodes;
    std::string firstBlockingNode;

    std::vector<std::string> failureCodes;   ///< deduplicated across nodes, sorted
    std::vector<ReplanClass> replanClasses;  ///< deduplicated, derived from the codes

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, TaskOutcome &out, std::string &error );
};

/// What a run actually consumed. Exceeding a cap must be VISIBLE in the report:
/// an invisible budget overrun is indistinguishable from having evaluated
/// everything.
struct BudgetUsage
{
    std::size_t checksEvaluated = 0;
    std::size_t checksUnevaluated = 0;
    std::size_t nodes = 0;
    std::size_t maxEvidenceBytes = 0;
    int maxDepthSeen = 0;
    bool exceeded = false;
    std::vector<std::string> reasons;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, BudgetUsage &out, std::string &error );
};

struct VerificationReport
{
    int schemaVersion = kVerificationReportSchemaVersion;
    std::string specId;
    std::string specDigest;            ///< content address of the spec that produced this
    CheckStatus status = CheckStatus::Indeterminate;
    std::vector<CheckResult> results;  ///< in spec declaration order
    std::vector<NodeOutcome> nodes;
    TaskOutcome outcome;
    BudgetUsage budgetUsage;
    Json::Value counters{ Json::objectValue };

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, VerificationReport &out, std::string &error );
};

/// @p results are already evaluated and in spec declaration order; node mapping
/// is nodeId -> ids of the checks belonging to it.
using NodeCheckMap = std::map<std::string, std::vector<std::string>>;

NodeOutcome rollUpNode( const std::string &nodeId, const std::vector<std::string> &checkIds,
                        const std::vector<CheckResult> &results,
                        IndeterminatePolicy policy = IndeterminatePolicy::Keep );

/// Level-2 roll-up. Empty input stays Indeterminate with NO blocking node — see
/// the header note on why inventing a blocker would be as bad as inventing a
/// pass.
TaskOutcome rollUpTask( const std::vector<NodeOutcome> &nodes );

/// Assembles a complete report, including the per-status/per-kind counters that
/// make a roll-up auditable rather than merely assertable.
VerificationReport buildReport( const VerificationSpec &spec,
                                const std::vector<CheckResult> &results,
                                const NodeCheckMap &nodeChecks,
                                IndeterminatePolicy policy,
                                const BudgetUsage &usage );

/// Content address of a report. Explicitly EXCLUDES nothing — and that is the
/// constraint: because the report carries no clock and no environment, two runs
/// over identical inputs must hash identically. Adding any time-dependent field
/// later would silently destroy this property, so tests pin it.
std::string reportDigest( const VerificationReport &report );

} // namespace sicnu::verification
