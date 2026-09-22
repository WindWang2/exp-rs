// tests/test_autonomy_classification.cpp
//
// RS14-12 teaching autonomy ladder — Slice B: action classification.
//
// Classification is the projection from concrete inputs (a lab intent, or a
// tool/action risk class) onto the closed capability taxonomy. It is total
// and fail-closed: every input either maps to a known capability or is
// reported as unknown — nothing ever defaults to a MORE capable class, so an
// unrecognized input can never unlock execution.
//
// Pure value-object suite: no Qt, no QGIS, no network.

#include <catch2/catch_test_macros.hpp>

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_classification.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

#include <string>
#include <vector>

using namespace sicnu::agent::autonomy;

TEST_CASE( "lab intents map onto the capability ladder", "[autonomy][classification]" )
{
    struct Case { const char *intent; const char *capability; };
    const Case cases[] = {
        { "lab_concept", assistance_capabilities::kConceptHint },
        { "lab_troubleshoot", assistance_capabilities::kErrorLocalization },
        { "lab_hint", assistance_capabilities::kNextStepRecommendation },
        { "lab_grade_request", assistance_capabilities::kAutonomousExecution },
        { "lab_execute", assistance_capabilities::kAutonomousExecution },
    };
    for ( const Case &c : cases )
    {
        const AssistanceClassification classification = classifyAssistanceIntent( c.intent );
        INFO( c.intent );
        REQUIRE( classification.known );
        REQUIRE( classification.capability == c.capability );
    }
}

TEST_CASE( "unknown assistance intents classify as unknown (fail-closed)", "[autonomy][classification]" )
{
    for ( const std::string bad : { "", "lab", "execute", "lab_Execute", "grade" } )
    {
        const AssistanceClassification classification = classifyAssistanceIntent( bad );
        INFO( bad );
        REQUIRE_FALSE( classification.known );
        REQUIRE( classification.capability.empty() );
    }
}

TEST_CASE( "risk classes map onto the capability ladder", "[autonomy][classification]" )
{
    struct Case { const char *riskClass; const char *capability; };
    const Case cases[] = {
        { autonomy_risk_classes::kReadOnly, assistance_capabilities::kReadOnlyQuery },
        { autonomy_risk_classes::kModifiesDisplay, assistance_capabilities::kReadOnlyQuery },
        { autonomy_risk_classes::kCreatesArtifact, assistance_capabilities::kAutonomousExecution },
        { autonomy_risk_classes::kModifiesProject, assistance_capabilities::kAutonomousExecution },
        { autonomy_risk_classes::kDestructive, assistance_capabilities::kAutonomousExecution },
        { autonomy_risk_classes::kExternalProcess, assistance_capabilities::kAutonomousExecution },
        { autonomy_risk_classes::kNetwork, assistance_capabilities::kAutonomousExecution },
    };
    for ( const Case &c : cases )
    {
        const ActionClassification classification = classifyActionRisk( c.riskClass );
        INFO( c.riskClass );
        REQUIRE( classification.known );
        REQUIRE( classification.capability == c.capability );
        REQUIRE( classification.riskClass == c.riskClass );
    }
}

TEST_CASE( "unknown risk classes classify as unknown (fail-closed)", "[autonomy][classification]" )
{
    for ( const std::string bad : { "", "readonly", "safe", "READ_ONLY", "unknown" } )
    {
        const ActionClassification classification = classifyActionRisk( bad );
        INFO( bad );
        REQUIRE_FALSE( classification.known );
        REQUIRE( classification.capability.empty() );
    }
}

TEST_CASE( "classification is total over both closed vocabularies", "[autonomy][classification]" )
{
    // Every lab intent in the closed list classifies.
    int intentCount = 0;
    const std::string *intents = labIntentVocabulary( intentCount );
    REQUIRE( intentCount == 5 );
    for ( int index = 0; index < intentCount; ++index )
        REQUIRE( classifyAssistanceIntent( intents[ index ] ).known );

    // Every risk class in the closed list classifies.
    int riskCount = 0;
    const std::string *riskClasses = autonomyRiskClassVocabulary( riskCount );
    REQUIRE( riskCount == 7 );
    for ( int index = 0; index < riskCount; ++index )
        REQUIRE( classifyActionRisk( riskClasses[ index ] ).known );
}

TEST_CASE( "classified capabilities always have a minimum level", "[autonomy][classification]" )
{
    const std::string intents[] = { "lab_concept",   "lab_troubleshoot", "lab_hint",
                                    "lab_grade_request", "lab_execute" };
    for ( const std::string &intent : intents )
    {
        const AssistanceClassification classification = classifyAssistanceIntent( intent );
        REQUIRE( isKnownAssistanceCapability( classification.capability ) );
        REQUIRE( minimumLevelForCapability( classification.capability ) >= AutonomyLevel::L1 );
    }
    const ActionClassification readOnly = classifyActionRisk( autonomy_risk_classes::kReadOnly );
    REQUIRE( minimumLevelForCapability( readOnly.capability ) == AutonomyLevel::L0 );
    const ActionClassification artifact =
        classifyActionRisk( autonomy_risk_classes::kCreatesArtifact );
    REQUIRE( minimumLevelForCapability( artifact.capability ) == AutonomyLevel::L5 );
}

TEST_CASE( "downgrade targets descend the ladder monotonically", "[autonomy][classification]" )
{
    REQUIRE( highestCapabilityWithinLevel( AutonomyLevel::L0 ) == assistance_capabilities::kReadOnlyQuery );
    REQUIRE( highestCapabilityWithinLevel( AutonomyLevel::L1 ) == assistance_capabilities::kConceptHint );
    REQUIRE( highestCapabilityWithinLevel( AutonomyLevel::L2 ) == assistance_capabilities::kErrorLocalization );
    REQUIRE( highestCapabilityWithinLevel( AutonomyLevel::L3 ) ==
             assistance_capabilities::kNextStepRecommendation );
    REQUIRE( highestCapabilityWithinLevel( AutonomyLevel::L4 ) == assistance_capabilities::kPlanGeneration );
    REQUIRE( highestCapabilityWithinLevel( AutonomyLevel::L5 ) == assistance_capabilities::kAutonomousExecution );
}
