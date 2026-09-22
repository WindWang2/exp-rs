// src/agent/autonomy/autonomy_capability.cpp
#include "agent/autonomy/autonomy_capability.h"

#include <array>

namespace sicnu::agent::autonomy {

const std::string *autonomyCapabilityVocabulary( int &count )
{
    static const std::array<std::string, 6> kVocabulary = {
        assistance_capabilities::kReadOnlyQuery,
        assistance_capabilities::kConceptHint,
        assistance_capabilities::kErrorLocalization,
        assistance_capabilities::kNextStepRecommendation,
        assistance_capabilities::kPlanGeneration,
        assistance_capabilities::kAutonomousExecution,
    };
    count = static_cast<int>( kVocabulary.size() );
    return kVocabulary.data();
}

bool isKnownAssistanceCapability( const std::string &capability )
{
    int count = 0;
    const std::string *vocabulary = autonomyCapabilityVocabulary( count );
    for ( int index = 0; index < count; ++index )
        if ( capability == vocabulary[ index ] )
            return true;
    return false;
}

AutonomyLevel minimumLevelForCapability( const std::string &capability )
{
    if ( capability == assistance_capabilities::kReadOnlyQuery )
        return AutonomyLevel::L0;
    if ( capability == assistance_capabilities::kConceptHint )
        return AutonomyLevel::L1;
    if ( capability == assistance_capabilities::kErrorLocalization )
        return AutonomyLevel::L2;
    if ( capability == assistance_capabilities::kNextStepRecommendation )
        return AutonomyLevel::L3;
    if ( capability == assistance_capabilities::kPlanGeneration )
        return AutonomyLevel::L4;
    if ( capability == assistance_capabilities::kAutonomousExecution )
        return AutonomyLevel::L5;
    // Unknown capabilities have no unlock level; the decision engine treats
    // them as deny before this value is ever consulted.
    return AutonomyLevel::L5;
}

std::string highestCapabilityWithinLevel( AutonomyLevel level )
{
    static const std::array<std::pair<AutonomyLevel, const char *>, 6> kByLevel = { {
        { AutonomyLevel::L0, assistance_capabilities::kReadOnlyQuery },
        { AutonomyLevel::L1, assistance_capabilities::kConceptHint },
        { AutonomyLevel::L2, assistance_capabilities::kErrorLocalization },
        { AutonomyLevel::L3, assistance_capabilities::kNextStepRecommendation },
        { AutonomyLevel::L4, assistance_capabilities::kPlanGeneration },
        { AutonomyLevel::L5, assistance_capabilities::kAutonomousExecution },
    } };
    const char *best = assistance_capabilities::kReadOnlyQuery;
    for ( const auto &entry : kByLevel )
    {
        if ( entry.first <= level )
            best = entry.second;
    }
    return std::string( best );
}

} // namespace sicnu::agent::autonomy
