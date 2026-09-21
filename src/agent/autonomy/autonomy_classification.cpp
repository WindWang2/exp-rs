// src/agent/autonomy/autonomy_classification.cpp
#include "agent/autonomy/autonomy_classification.h"

#include <array>

namespace sicnu::agent::autonomy {

const std::string *autonomyRiskClassVocabulary( int &count )
{
    static const std::array<std::string, 7> kVocabulary = {
        autonomy_risk_classes::kReadOnly,
        autonomy_risk_classes::kModifiesDisplay,
        autonomy_risk_classes::kCreatesArtifact,
        autonomy_risk_classes::kModifiesProject,
        autonomy_risk_classes::kDestructive,
        autonomy_risk_classes::kExternalProcess,
        autonomy_risk_classes::kNetwork,
    };
    count = static_cast<int>( kVocabulary.size() );
    return kVocabulary.data();
}

const std::string *labIntentVocabulary( int &count )
{
    // Mirror of the closed lab list in intent_vocabulary.h.
    static const std::array<std::string, 5> kVocabulary = {
        "lab_troubleshoot",
        "lab_hint",
        "lab_concept",
        "lab_grade_request",
        "lab_execute",
    };
    count = static_cast<int>( kVocabulary.size() );
    return kVocabulary.data();
}

AssistanceClassification classifyAssistanceIntent( const std::string &intent )
{
    AssistanceClassification classification;
    classification.intent = intent;
    if ( intent == "lab_concept" )
        classification.capability = assistance_capabilities::kConceptHint;
    else if ( intent == "lab_troubleshoot" )
        classification.capability = assistance_capabilities::kErrorLocalization;
    else if ( intent == "lab_hint" )
        classification.capability = assistance_capabilities::kNextStepRecommendation;
    else if ( intent == "lab_grade_request" || intent == "lab_execute" )
        classification.capability = assistance_capabilities::kAutonomousExecution;
    else
        return classification; // known=false, empty capability

    classification.known = true;
    return classification;
}

ActionClassification classifyActionRisk( const std::string &riskClass )
{
    ActionClassification classification;
    classification.riskClass = riskClass;
    if ( riskClass == autonomy_risk_classes::kReadOnly ||
         riskClass == autonomy_risk_classes::kModifiesDisplay )
        classification.capability = assistance_capabilities::kReadOnlyQuery;
    else if ( riskClass == autonomy_risk_classes::kCreatesArtifact ||
              riskClass == autonomy_risk_classes::kModifiesProject ||
              riskClass == autonomy_risk_classes::kDestructive ||
              riskClass == autonomy_risk_classes::kExternalProcess ||
              riskClass == autonomy_risk_classes::kNetwork )
        classification.capability = assistance_capabilities::kAutonomousExecution;
    else
        return classification; // known=false, empty capability

    classification.known = true;
    return classification;
}

} // namespace sicnu::agent::autonomy
