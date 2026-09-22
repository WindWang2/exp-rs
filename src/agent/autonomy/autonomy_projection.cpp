// src/agent/autonomy/autonomy_projection.cpp
#include "agent/autonomy/autonomy_projection.h"

#include <algorithm>

#include "agent/autonomy/autonomy_decision.h"

namespace sicnu::agent::autonomy {

AutonomyLevel effectiveAutonomyLevel( const AutonomyPolicy &policy )
{
    const AutonomyLevel declared = policy.hasLevel ? policy.level : AutonomyLevel::L0;
    const AutonomyLevel ceiling = policy.mode.empty() ? AutonomyLevel::L5 : modeCeiling( policy.mode );
    const AutonomyLevel cap = policy.hasMaxLevel ? policy.maxLevel : AutonomyLevel::L5;
    return std::min( declared, std::min( ceiling, cap ) );
}

std::string autonomyReasonZh( const std::string &reasonCode )
{
    if ( reasonCode == autonomy_reason_codes::kAllowed )
        return "当前自治等级允许该操作。";
    if ( reasonCode == autonomy_reason_codes::kUnknownCapability )
        return "未知能力：策略无法确认其安全性，已拒绝。";
    if ( reasonCode == autonomy_reason_codes::kLevelTooLow )
        return "当前自治等级不足，无法提供该级别的帮助。";
    if ( reasonCode == autonomy_reason_codes::kDowngraded )
        return "已按当前自治等级降级提供帮助。";
    if ( reasonCode == autonomy_reason_codes::kModeCeiling )
        return "当前模式（考试/练习/教师/智能体）不允许该等级。";
    if ( reasonCode == autonomy_reason_codes::kCourseCap )
        return "课程策略设置了上限，无法超过该上限。";
    if ( reasonCode == autonomy_reason_codes::kOverrideDenied )
        return "课程或教师显式禁止了该能力。";
    if ( reasonCode == autonomy_reason_codes::kLabStudentExecution )
        return "教学约束：实验过程中学生不可由 AI 代做（宁可少帮，不可代做）。";
    if ( reasonCode == autonomy_reason_codes::kAgentModeRequired )
        return "自动执行需要显式切换到智能体（agent）模式，并保留科学验证。";
    return "";
}

Json::Value autonomyDecisionDoc( const AutonomyDecision &decision )
{
    Json::Value doc( Json::objectValue );
    doc[ "decision" ] = decision.kindString();
    doc[ "capability" ] = decision.capability;
    doc[ "effective_level" ] = autonomyLevelToString( decision.effectiveLevel );
    doc[ "reason_code" ] = decision.reasonCode;
    doc[ "reason_zh" ] = autonomyReasonZh( decision.reasonCode );
    if ( decision.kind == AutonomyDecisionKind::Downgrade )
        doc[ "downgrade_to" ] = decision.downgradeTo;
    if ( decision.verificationRequired )
        doc[ "verification_required" ] = true;
    return doc;
}

Json::Value autonomyStatusProjection( const AutonomyPolicy &policy, const std::string &role,
                                      const std::string &domain )
{
    Json::Value status( Json::objectValue );
    status[ "schema" ] = kAutonomyStatusSchema;
    status[ "level" ] = autonomyLevelToString( effectiveAutonomyLevel( policy ) );
    if ( !policy.mode.empty() )
        status[ "mode" ] = policy.mode;
    status[ "role" ] = role;
    status[ "domain" ] = domain;

    Json::Value allowed( Json::arrayValue );
    Json::Value limited( Json::arrayValue );
    Json::Value forbidden( Json::arrayValue );

    int capabilityCount = 0;
    const std::string *vocabulary = autonomyCapabilityVocabulary( capabilityCount );
    bool verificationRequired = false;
    for ( int index = 0; index < capabilityCount; ++index )
    {
        AutonomyRequest request;
        request.domain = domain;
        request.role = role;
        request.capability = vocabulary[ index ];
        const AutonomyDecision decision = decideAutonomy( policy, request );
        switch ( decision.kind )
        {
            case AutonomyDecisionKind::Allow:
                allowed.append( vocabulary[ index ] );
                if ( decision.verificationRequired )
                    verificationRequired = true;
                break;
            case AutonomyDecisionKind::Downgrade:
            {
                Json::Value entry( Json::objectValue );
                entry[ "capability" ] = vocabulary[ index ];
                entry[ "downgrade_to" ] = decision.downgradeTo;
                entry[ "reason_code" ] = decision.reasonCode;
                entry[ "reason_zh" ] = autonomyReasonZh( decision.reasonCode );
                limited.append( entry );
                break;
            }
            case AutonomyDecisionKind::Deny:
            {
                Json::Value entry( Json::objectValue );
                entry[ "capability" ] = vocabulary[ index ];
                entry[ "reason_code" ] = decision.reasonCode;
                entry[ "reason_zh" ] = autonomyReasonZh( decision.reasonCode );
                forbidden.append( entry );
                break;
            }
        }
    }

    status[ "allowed" ] = allowed;
    status[ "limited" ] = limited;
    status[ "forbidden" ] = forbidden;
    if ( verificationRequired )
        status[ "verification_required" ] = true;
    return status;
}

} // namespace sicnu::agent::autonomy
