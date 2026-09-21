// src/agent/autonomy/autonomy_holder.cpp
#include "agent/autonomy/autonomy_holder.h"

namespace sicnu::agent::autonomy {

AutonomyPolicyHolder &AutonomyPolicyHolder::instance()
{
    static AutonomyPolicyHolder holder;
    return holder;
}

AutonomyPolicy AutonomyPolicyHolder::researchDefaultPolicy()
{
    AutonomyPolicy policy;
    policy.hasLevel = true;
    policy.level = AutonomyLevel::L5;
    policy.mode = autonomy_modes::kAgent;
    policy.source = policy_sources::kCourse;
    return policy;
}

void AutonomyPolicyHolder::installCoursePolicy( const AutonomyPolicy &policy )
{
    const std::lock_guard<std::mutex> guard( mMutex );
    mCoursePolicy = policy;
    mProblems.clear();
}

bool AutonomyPolicyHolder::installCoursePolicyJson( const std::string &text )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson( text );
    if ( !parsed.ok )
    {
        const std::lock_guard<std::mutex> guard( mMutex );
        mProblems = parsed.errors; // previous policy is kept
        return false;
    }
    installCoursePolicy( parsed.policy );
    return true;
}

AutonomyPolicy AutonomyPolicyHolder::coursePolicy() const
{
    const std::lock_guard<std::mutex> guard( mMutex );
    return mCoursePolicy;
}

AutonomyPolicy AutonomyPolicyHolder::effectivePolicy(
    const std::vector<AutonomyPolicyLayer> &extraLayers ) const
{
    std::vector<AutonomyPolicyLayer> layers;
    {
        const std::lock_guard<std::mutex> guard( mMutex );
        layers.emplace_back( AutonomyPolicyLayer{ policy_sources::kCourse, mCoursePolicy } );
    }
    layers.insert( layers.end(), extraLayers.begin(), extraLayers.end() );
    return resolveEffectivePolicy( layers );
}

std::vector<std::string> AutonomyPolicyHolder::problems() const
{
    const std::lock_guard<std::mutex> guard( mMutex );
    return mProblems;
}

} // namespace sicnu::agent::autonomy
