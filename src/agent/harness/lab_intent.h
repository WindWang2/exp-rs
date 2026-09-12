// src/agent/harness/lab_intent.h
#pragma once

//
// D9: deterministic classifier for the lab (teaching) intent domain.
//
// Maps a help-seeking message onto the closed kLabIntentVocabulary — no
// model, no network, no randomness: the same message always yields the same
// intent, which is what the eval suite pins.
//
// Refusal default: anything unclassifiable (empty, gibberish, unknown) is
// lab_hint. The classifier NEVER falls back to lab_execute or
// lab_grade_request — those are reachable only through explicit signals and
// are gated to the teacher role downstream.
//
// Role is NOT classified: it is session state (harness_actions
// normalizeLabRole). Impersonation inside the message text is inert by
// construction.
//

#include <string>
#include <vector>

#include "intent_vocabulary.h"

namespace sicnu::agent::harness {

struct LabIntentClassification
{
    std::string intent;                   ///< one of kLabIntentVocabulary
    double score = 0.0;                   ///< matched-signal count (informational)
    std::vector<std::string> matchedSignals; ///< which signals fired (audit)
};

/// Classify one message. Deterministic; Chinese- and English-keyword based.
LabIntentClassification classifyLabIntent( const std::string &message );

} // namespace sicnu::agent::harness
