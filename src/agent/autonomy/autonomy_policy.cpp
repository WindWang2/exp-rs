// src/agent/autonomy/autonomy_policy.cpp
#include "agent/autonomy/autonomy_policy.h"

#include "agent/autonomy/autonomy_decision.h"

#include <algorithm>
#include <set>

namespace sicnu::agent::autonomy {

namespace {

const std::set<std::string> &knownTopLevelFields()
{
    static const std::set<std::string> kFields = {
        "schema", "level", "mode", "max_level", "capability_overrides", "source",
    };
    return kFields;
}

} // namespace

bool isKnownAutonomyMode( const std::string &mode )
{
    return mode == autonomy_modes::kExam || mode == autonomy_modes::kPractice ||
           mode == autonomy_modes::kInstructor || mode == autonomy_modes::kAgent;
}

bool isKnownPolicySource( const std::string &source )
{
    return source == policy_sources::kCourse || source == policy_sources::kLabspec ||
           source == policy_sources::kTeacher || source == policy_sources::kSession;
}

AutonomyLevel modeCeiling( const std::string &mode )
{
    // Agent mode is the explicit L5 opt-in for the research domain: it has no
    // ceiling of its own (the level field and course max_level still apply).
    // Every other mode caps the ladder; an unknown mode caps at L0.
    if ( mode == autonomy_modes::kAgent )
        return AutonomyLevel::L5;
    if ( mode == autonomy_modes::kPractice )
        return AutonomyLevel::L4;
    if ( mode == autonomy_modes::kInstructor )
        return AutonomyLevel::L5;
    if ( mode == autonomy_modes::kExam )
        return AutonomyLevel::L2;
    return AutonomyLevel::L0;
}

AutonomyPolicyParseResult parseAutonomyPolicy( const Json::Value &doc )
{
    AutonomyPolicyParseResult result;
    if ( !doc.isObject() )
    {
        result.errors.emplace_back( "policy.not_object" );
        return result;
    }

    const Json::Value::Members members = doc.getMemberNames();
    for ( const std::string &member : members )
        if ( knownTopLevelFields().find( member ) == knownTopLevelFields().end() )
            result.errors.emplace_back( "policy.field.unknown:" + member );

    const Json::Value &schema = doc[ "schema" ];
    if ( !schema.isString() )
        result.errors.emplace_back( "policy.schema.missing" );
    else if ( schema.asString() != kAutonomyPolicySchema )
        result.errors.emplace_back( "policy.schema.unsupported:" + schema.asString() );

    AutonomyPolicy &policy = result.policy;
    policy.schema = kAutonomyPolicySchema;

    const Json::Value &level = doc[ "level" ];
    if ( !level.isNull() )
    {
        AutonomyLevel parsed = AutonomyLevel::L0;
        if ( level.isString() && autonomyLevelFromString( level.asString(), parsed ) )
        {
            policy.hasLevel = true;
            policy.level = parsed;
        }
        else
        {
            result.errors.emplace_back( "policy.level.invalid" );
        }
    }

    const Json::Value &mode = doc[ "mode" ];
    if ( !mode.isNull() )
    {
        if ( mode.isString() && isKnownAutonomyMode( mode.asString() ) )
            policy.mode = mode.asString();
        else
            result.errors.emplace_back( "policy.mode.unknown" );
    }

    const Json::Value &maxLevel = doc[ "max_level" ];
    if ( !maxLevel.isNull() )
    {
        AutonomyLevel parsed = AutonomyLevel::L5;
        if ( maxLevel.isString() && autonomyLevelFromString( maxLevel.asString(), parsed ) )
        {
            policy.hasMaxLevel = true;
            policy.maxLevel = parsed;
        }
        else
        {
            result.errors.emplace_back( "policy.max_level.invalid" );
        }
    }

    const Json::Value &overrides = doc[ "capability_overrides" ];
    if ( !overrides.isNull() )
    {
        if ( !overrides.isObject() )
        {
            result.errors.emplace_back( "policy.overrides.not_object" );
        }
        else
        {
            for ( const std::string &capability : overrides.getMemberNames() )
            {
                if ( !isKnownAssistanceCapability( capability ) )
                {
                    result.errors.emplace_back( "policy.override.capability.unknown:" + capability );
                    continue;
                }
                const Json::Value &entry = overrides[ capability ];
                if ( !entry.isObject() )
                {
                    result.errors.emplace_back( "policy.override.not_object:" + capability );
                    continue;
                }
                AutonomyCapabilityOverride parsed;
                const Json::Value &decision = entry[ "decision" ];
                if ( decision.isString() &&
                     ( decision.asString() == "allow" || decision.asString() == "deny" ) )
                {
                    parsed.decision = decision.asString();
                }
                else
                {
                    result.errors.emplace_back( "policy.override.decision.unknown:" + capability );
                    continue;
                }
                const Json::Value &reasonCode = entry[ "reason_code" ];
                if ( !reasonCode.isNull() )
                {
                    // The deny reason rides into decisions and the audit
                    // log verbatim: only the closed reason-code vocabulary
                    // may be injected, never a caller-chosen string — and
                    // a deny never carries "allowed" as its reason.
                    const bool knownCode = reasonCode.isString() &&
                        isKnownAutonomyReasonCode( reasonCode.asString() );
                    const bool allowedOnDeny = parsed.decision == "deny" &&
                        reasonCode.isString() &&
                        reasonCode.asString() == autonomy_reason_codes::kAllowed;
                    if ( knownCode && !allowedOnDeny )
                        parsed.reasonCode = reasonCode.asString();
                    else
                        result.errors.emplace_back( "policy.override.reason_code.unknown:" + capability );
                }
                policy.overrides.emplace_back( capability, parsed );
            }
        }
    }

    const Json::Value &source = doc[ "source" ];
    if ( !source.isNull() )
    {
        if ( source.isString() && isKnownPolicySource( source.asString() ) )
            policy.source = source.asString();
        else
            result.errors.emplace_back( "policy.source.unknown" );
    }

    // Deterministic order regardless of document member order.
    std::sort( policy.overrides.begin(), policy.overrides.end(),
               []( const std::pair<std::string, AutonomyCapabilityOverride> &lhs,
                   const std::pair<std::string, AutonomyCapabilityOverride> &rhs ) {
                   return lhs.first < rhs.first;
               } );

    result.ok = result.errors.empty();
    if ( !result.ok )
        result.policy = AutonomyPolicy{};
    return result;
}

AutonomyPolicyParseResult parseAutonomyPolicyJson( const std::string &text )
{
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( text.data(), text.data() + text.size(), &doc, &errors ) )
    {
        AutonomyPolicyParseResult result;
        result.errors.emplace_back( "policy.json.parse_failed" );
        return result;
    }
    return parseAutonomyPolicy( doc );
}

bool validateAutonomyPolicy( const AutonomyPolicy &policy, std::vector<std::string> &errors )
{
    if ( policy.schema != kAutonomyPolicySchema )
        errors.emplace_back( "policy.schema.unsupported:" + policy.schema );
    if ( !policy.mode.empty() && !isKnownAutonomyMode( policy.mode ) )
        errors.emplace_back( "policy.mode.unknown" );
    if ( !policy.source.empty() && !isKnownPolicySource( policy.source ) )
        errors.emplace_back( "policy.source.unknown" );
    for ( const auto &override : policy.overrides )
    {
        if ( !isKnownAssistanceCapability( override.first ) )
            errors.emplace_back( "policy.override.capability.unknown:" + override.first );
        if ( override.second.decision != "allow" && override.second.decision != "deny" )
            errors.emplace_back( "policy.override.decision.unknown:" + override.first );
    }
    return errors.empty();
}

Json::Value AutonomyPolicy::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "schema" ] = schema;
    if ( hasLevel )
        doc[ "level" ] = autonomyLevelToString( level );
    if ( !mode.empty() )
        doc[ "mode" ] = mode;
    if ( hasMaxLevel )
        doc[ "max_level" ] = autonomyLevelToString( maxLevel );
    if ( !overrides.empty() )
    {
        Json::Value overridesDoc( Json::objectValue );
        for ( const auto &override : overrides )
        {
            Json::Value entry( Json::objectValue );
            entry[ "decision" ] = override.second.decision;
            if ( !override.second.reasonCode.empty() )
                entry[ "reason_code" ] = override.second.reasonCode;
            overridesDoc[ override.first ] = entry;
        }
        doc[ "capability_overrides" ] = overridesDoc;
    }
    if ( !source.empty() )
        doc[ "source" ] = source;
    return doc;
}

AutonomyPolicy resolveEffectivePolicy( const std::vector<AutonomyPolicyLayer> &layers )
{
    // Precedence: course < labspec < teacher < session. Resolution is
    // per-field: the highest-precedence source that DECLARES a value wins
    // (level, mode, per-capability override); max_level ceilings take the
    // tightest declared value so no source can loosen another's cap. Unknown
    // sources are ignored so they can never grant anything.
    static const auto sourceRank = []( const std::string &source ) {
        if ( source == policy_sources::kSession )
            return 4;
        if ( source == policy_sources::kTeacher )
            return 3;
        if ( source == policy_sources::kLabspec )
            return 2;
        if ( source == policy_sources::kCourse )
            return 1;
        return 0;
    };

    AutonomyPolicy effective;
    int levelRank = 0;
    int modeRank = 0;
    for ( const AutonomyPolicyLayer &layer : layers )
    {
        const int rank = sourceRank( layer.source );
        if ( rank == 0 )
            continue;
        const AutonomyPolicy &policy = layer.policy;
        if ( policy.hasLevel && rank >= levelRank )
        {
            effective.hasLevel = true;
            effective.level = policy.level;
            levelRank = rank;
        }
        if ( !policy.mode.empty() && rank >= modeRank )
        {
            effective.mode = policy.mode;
            modeRank = rank;
        }
        if ( policy.hasMaxLevel && ( !effective.hasMaxLevel || policy.maxLevel < effective.maxLevel ) )
        {
            effective.hasMaxLevel = true;
            effective.maxLevel = policy.maxLevel;
        }
        for ( const auto &override : policy.overrides )
        {
            const auto existing = std::find_if(
                effective.overrides.begin(), effective.overrides.end(),
                [ &override ]( const std::pair<std::string, AutonomyCapabilityOverride> &candidate ) {
                    return candidate.first == override.first;
                } );
            if ( existing == effective.overrides.end() )
            {
                effective.overrides.emplace_back( override.first,
                                                  AutonomyCapabilityOverride{ override.second.decision,
                                                                              override.second.reasonCode,
                                                                              rank } );
            }
            else if ( rank >= existing->second.rank )
            {
                existing->second.decision = override.second.decision;
                existing->second.reasonCode = override.second.reasonCode;
                existing->second.rank = rank;
            }
        }
    }
    std::sort( effective.overrides.begin(), effective.overrides.end(),
               []( const std::pair<std::string, AutonomyCapabilityOverride> &lhs,
                   const std::pair<std::string, AutonomyCapabilityOverride> &rhs ) {
                   return lhs.first < rhs.first;
               } );
    return effective;
}

} // namespace sicnu::agent::autonomy
