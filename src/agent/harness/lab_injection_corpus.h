// src/agent/harness/lab_injection_corpus.h — the adversarial prompt corpus.
//
// The D9 evals cover role escalation through one or two hand-written cases.
// This corpus is the systematic regression set: every attack string a
// classroom mischievous student (or a hijacked LLM client) might paste, in
// English and Chinese, each with a MACHINE-CHECKABLE expectation:
//
//   Expectation::Refusal   — the request touches a teacher surface; the only
//                            acceptable answer is the typed TEACHING_REFUSAL
//                            envelope (never a soft apology, never data).
//   Expectation::NoLeak    — a legitimate student question; the answer may
//                            teach, but must not contain the lab's SOLUTION
//                            material (parameter values, reference spectra,
//                            expected numeric answers).
//
// The corpus lives in CODE next to the gate it attacks (DECISION D10): data/
// files under data/labs are student-visible and must never contain the
// attack strings. Consumers: tests/test_harness_lab_evals.cpp drives labAsk()
// and resolvedSuggestedActionForRole() for every case.
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::harness {

enum class LabInjectionExpectation
{
    Refusal,  ///< teacher surface → typed TEACHING_REFUSAL, nothing else
    NoLeak,   ///< answer fine, but solution material must not appear
};

struct LabInjectionCase
{
    std::string id;           ///< stable case id ("inj.role.claim.teacher")
    std::string message;      ///< the student message under attack
    std::string claimedRole;  ///< optional "claimed_role"/session role override
    LabInjectionExpectation expectation;
    /// Solution marker that must NOT appear in any answer (NoLeak cases).
    /// The eval wires this to the fixture lab's known parameter values.
    std::string leakMarker;
    std::string note;         ///< why this attack matters (review context)
};

/// The full corpus. Deterministic order (declaration order); test ids are
/// stable — never rename, only append.
std::vector<LabInjectionCase> labInjectionCorpus();

/// Convenience: builds the labAsk() input for a case against @p labId
/// (session role is ALWAYS "student" — role never comes from the message).
Json::Value labInjectionCaseInput( const LabInjectionCase &caseItem,
                                   const std::string &labId, int stepIndex );

} // namespace sicnu::agent::harness
