// src/agent/autonomy/autonomy_decision.cpp
#include "agent/autonomy/autonomy_decision.h"

#include <algorithm>

namespace sicnu::agent::autonomy {

namespace {

constexpr const char *kLabDomain = "lab";
constexpr const char *kAgentMode = "agent";

AutonomyDecisionKind deny( AutonomyDecision &decision, const char *reasonCode )
{
    decision.kind = AutonomyDecisionKind::Deny;
    decision.reasonCode = reasonCode;
    decision.downgradeTo.clear();
    decision.verificationRequired = false;
    return decision.kind;
}

} // namespace

bool isKnownAutonomyReasonCode( const std::string &code )
{
    static const char *const kCodes[] = {
        autonomy_reason_codes::kAllowed,
        autonomy_reason_codes::kUnknownCapability,
        autonomy_reason_codes::kLevelTooLow,
        autonomy_reason_codes::kDowngraded,
        autonomy_reason_codes::kModeCeiling,
        autonomy_reason_codes::kCourseCap,
        autonomy_reason_codes::kOverrideDenied,
        autonomy_reason_codes::kLabStudentExecution,
        autonomy_reason_codes::kAgentModeRequired,
    };
    for ( const char *candidate : kCodes )
        if ( code == candidate )
            return true;
    return false;
}

bool isInstructorRole( const std::string &role )
{
    // Mirrors normalizeLabRole(): only the exact instructor spellings count;
    // everything else — including empty and near-misses like "teacher " —
    // is a student.
    return role == "teacher" || role == "admin";
}

std::string AutonomyDecision::kindString() const
{
    switch ( kind )
    {
        case AutonomyDecisionKind::Allow:
            return "allow";
        case AutonomyDecisionKind::Deny:
            return "deny";
        case AutonomyDecisionKind::Downgrade:
            return "downgrade";
    }
    return "deny";
}

Json::Value AutonomyDecision::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "decision" ] = kindString();
    doc[ "capability" ] = capability;
    doc[ "requested_level" ] = autonomyLevelToString( requestedLevel );
    doc[ "effective_level" ] = autonomyLevelToString( effectiveLevel );
    doc[ "reason_code" ] = reasonCode;
    if ( kind == AutonomyDecisionKind::Downgrade )
        doc[ "downgrade_to" ] = downgradeTo;
    if ( verificationRequired )
        doc[ "verification_required" ] = true;
    return doc;
}

AutonomyDecision decideAutonomy( const AutonomyPolicy &policy, const AutonomyRequest &request )
{
    AutonomyDecision decision;
    decision.capability = request.capability;
    decision.requestedLevel = isKnownAssistanceCapability( request.capability )
                                  ? minimumLevelForCapability( request.capability )
                                  : AutonomyLevel::L5;

    // Effective level: the declared level (undeclared ⇒ L0, fail-closed),
    // clamped by the mode ceiling and by the tightest course cap.
    const AutonomyLevel declaredLevel = policy.hasLevel ? policy.level : AutonomyLevel::L0;
    const AutonomyLevel ceiling = policy.mode.empty() ? AutonomyLevel::L5 : modeCeiling( policy.mode );
    const AutonomyLevel cap = policy.hasMaxLevel ? policy.maxLevel : AutonomyLevel::L5;
    decision.effectiveLevel = std::min( declaredLevel, std::min( ceiling, cap ) );

    const bool isExecution =
        request.capability == assistance_capabilities::kAutonomousExecution;
    const bool isLabDomain = request.domain == kLabDomain;

    // 1. Unknown capability: fail closed, always.
    if ( !isKnownAssistanceCapability( request.capability ) )
        return deny( decision, autonomy_reason_codes::kUnknownCapability ), decision;

    // 2. The teaching constraint, restated on the autonomy axis: a lab
    //    student never receives autonomous execution at any level, and no
    //    override can lift it — this is the structural rule from ADR 0155.
    if ( isLabDomain && isExecution && !isInstructorRole( request.role ) )
        return deny( decision, autonomy_reason_codes::kLabStudentExecution ), decision;

    // 3. The research domain's explicit opt-in: L5 execution there requires
    //    agent mode; scientific verification stays mandatory (set on allow).
    if ( !isLabDomain && isExecution && policy.mode != kAgentMode )
        return deny( decision, autonomy_reason_codes::kAgentModeRequired ), decision;

    // 4. Explicit per-capability override — the only rule that can bypass
    //    the level matrix (rules 1-3 are structural and already applied).
    for ( const auto &override : policy.overrides )
    {
        if ( override.first != request.capability )
            continue;
        if ( override.second.decision == "deny" )
        {
            decision.kind = AutonomyDecisionKind::Deny;
            decision.reasonCode = override.second.reasonCode.empty()
                                      ? autonomy_reason_codes::kOverrideDenied
                                      : override.second.reasonCode;
            return decision;
        }
        // "allow"
        decision.kind = AutonomyDecisionKind::Allow;
        decision.reasonCode = autonomy_reason_codes::kAllowed;
        decision.verificationRequired = isExecution;
        return decision;
    }

    // 5. Level comparison.
    if ( decision.effectiveLevel >= decision.requestedLevel )
    {
        decision.kind = AutonomyDecisionKind::Allow;
        decision.reasonCode = autonomy_reason_codes::kAllowed;
        decision.verificationRequired = isExecution;
        return decision;
    }

    // 6. Assistive capabilities downgrade to the highest unlocked capability.
    //    L0 is "no assistance": a downgrade target below concept_hint would
    //    still be help, so L0 denies instead of downgrading.
    if ( !isExecution )
    {
        const std::string target = highestCapabilityWithinLevel( decision.effectiveLevel );
        if ( target != assistance_capabilities::kReadOnlyQuery )
        {
            decision.kind = AutonomyDecisionKind::Downgrade;
            decision.reasonCode = autonomy_reason_codes::kDowngraded;
            decision.downgradeTo = target;
            return decision;
        }
    }

    // 7. Execution is never downgraded; report the binding constraint.
    if ( !policy.mode.empty() && ceiling < decision.requestedLevel )
        return deny( decision, autonomy_reason_codes::kModeCeiling ), decision;
    if ( policy.hasMaxLevel && cap < decision.requestedLevel )
        return deny( decision, autonomy_reason_codes::kCourseCap ), decision;
    return deny( decision, autonomy_reason_codes::kLevelTooLow ), decision;
}

} // namespace sicnu::agent::autonomy
