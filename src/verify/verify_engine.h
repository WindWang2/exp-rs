// src/verify/verify_engine.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — the evaluation engine.
//
// Dispatches each spec check to its kind evaluator against the provider
// seams in verify_context.h. Fail-closed by construction:
//
//   - a check whose provider is not attached is Indeterminate
//     (verify:i_provider_missing) — never skipped, never guessed;
//   - an observation the engine cannot judge (non-finite number, unreadable
//     artifact) is Indeterminate — never Pass;
//   - a detectable contract violation is Fail with a verify:e_* code;
//   - an unknown check kind resolves to Indeterminate, so a vocabulary
//     extension without an engine implementation can never read as Pass
//     (the vocabulary-engine interlock is locked by hasEvaluator + tests).
//
// Resource discipline: evaluation is O(checks); every evaluator touches
// only the paths/keys its params name. The engine never walks pixels and
// never scans beyond what the spec pins — bounded sampling is the probe
// implementation's contract (verify_context.h).
//

#include <string>

#include "verify_context.h"
#include "verify_types.h"

namespace sicnu::verify
{

/// @returns true when the dispatch table implements @p kind. Locked against
/// kCheckKinds in the adversarial tests: adding a kind without an evaluator
/// turns the suite red.
bool hasEvaluator( const std::string &kind );

/// Judge ONE check against @p context. Never throws for any spec content:
/// hostile params come back as a typed Fail (verify:e_invalid_spec), not an
/// escaping exception. The returned result carries evidence whenever the
/// probes produced observable facts.
VerificationCheckResult evaluateCheck( const VerificationCheckSpec &check,
                                       const VerificationContext &context );

/// Evaluate a whole spec in declaration order. The spec is sealed and
/// validated first: a structurally invalid — or unsealable (non-finite
/// number in params) — spec yields a single synthetic "spec.valid" Fail
/// result (verify:e_invalid_spec) instead of an evaluation, because a
/// malformed contract must not silently judge nothing.
VerificationReport evaluate( const VerificationSpec &spec, const VerificationContext &context );

} // namespace sicnu::verify
