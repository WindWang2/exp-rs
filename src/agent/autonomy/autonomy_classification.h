// src/agent/autonomy/autonomy_classification.h
#pragma once

//
// RS14-12: classification of concrete inputs onto the capability ladder.
//
// Two axes, one closed mapping each:
//   * assistance — a lab intent (the teaching surface) maps to the
//     capability it would exercise;
//   * action — a tool/action risk class maps to the capability executing it
//     would exercise.
//
// Classification is total and fail-closed. An input that is not in the
// closed vocabulary is reported as unknown (known=false, empty capability);
// the decision engine denies unknown capabilities with a typed reason. No
// unknown input ever maps to a more capable class — that is the property
// that keeps a typo or a new, unclassified tool from silently unlocking
// execution.
//
// The risk-class strings mirror tool_manifest.h (the authority). The heavy
// gate suite cross-checks the mirror against the live table so the two
// vocabularies cannot drift apart.

#include <string>

#include "agent/autonomy/autonomy_capability.h"

namespace sicnu::agent::autonomy {

/// Mirrors sicnu::agent::harness::risk_classes (tool_manifest.h). Never
/// rename; the drift floor in the gate suite pins both sides.
namespace autonomy_risk_classes
{
inline constexpr const char *kReadOnly = "read_only";
inline constexpr const char *kModifiesDisplay = "modifies_display";
inline constexpr const char *kCreatesArtifact = "creates_artifact";
inline constexpr const char *kModifiesProject = "modifies_project";
inline constexpr const char *kDestructive = "destructive";
inline constexpr const char *kExternalProcess = "external_process";
inline constexpr const char *kNetwork = "network";
} // namespace autonomy_risk_classes

/// The closed risk vocabulary (mirror of tool_manifest).
const std::string *autonomyRiskClassVocabulary( int &count );

/// The closed lab intent vocabulary (mirror of intent_vocabulary.h).
const std::string *labIntentVocabulary( int &count );

struct AssistanceClassification
{
    std::string intent;
    std::string capability; ///< empty when unknown
    bool known = false;
};

struct ActionClassification
{
    std::string riskClass;
    std::string capability; ///< empty when unknown
    bool known = false;
};

/// Lab intent → capability. Unknown intents are not classified.
AssistanceClassification classifyAssistanceIntent( const std::string &intent );

/// Risk class → capability. read_only / modifies_display are queries;
/// everything that changes state executes work on the requester's behalf.
/// Unknown risk classes are not classified.
ActionClassification classifyActionRisk( const std::string &riskClass );

} // namespace sicnu::agent::autonomy
