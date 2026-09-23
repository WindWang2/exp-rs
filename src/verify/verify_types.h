// src/verify/verify_types.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — value objects, closed
// vocabularies, versioned serde and structural validation.
//
// One verification truth for student experiments, agent execution and
// batch acceptance:
//
//   VerificationSpec  — WHAT to verify (versioned, composable via packs)
//   VerificationCheckResult — one check's verdict + typed code + evidence
//   VerificationReport — the whole-node/whole-task outcome with a
//                        deterministic digest (canonical JSON body, no
//                        wall clock — same discipline as the lab grade
//                        digest and DAG provenance serialization).
//
// Schemas: "sicnu.verification.spec/1", "sicnu.verification.report/1",
// "sicnu.verification.evidence/1" (embedded evidence carries no schema
// marker; the standalone sidecar projection adds it).
//
// Plain C++20 + jsoncpp, no Qt — same discipline as src/contracts.
//

#include <json/json.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "verify_status.h"

namespace sicnu::verify
{

inline constexpr const char *kSpecSchema = "sicnu.verification.spec/1";
inline constexpr const char *kReportSchema = "sicnu.verification.report/1";
inline constexpr const char *kEvidenceSchema = "sicnu.verification.evidence/1";
inline constexpr const char *kPackSchema = "sicnu.verification.pack/1";

/// Closed check-kind vocabulary. Wire-stable; extending is a conscious
/// contract update (Slice A locks the set in test_verifier_schema).
extern const std::vector<std::string> kCheckKinds;

/// Closed spec scopes: a node-scope spec judges one plan node's
/// postcondition; a task-scope spec judges the whole task outcome.
extern const std::vector<std::string> kSpecScopes;

/// Resource budgets (plan §8): specs and packs are caller-supplied, so the
/// validator bounds them mechanically instead of trusting callers.
inline constexpr std::size_t kMaxChecksPerSpec = 256;
inline constexpr std::size_t kMaxSpecsPerPack = 64;
inline constexpr std::size_t kMaxExpectationsPerCheck = 64;
inline constexpr std::size_t kMaxRelationsPerCheck = 64;
inline constexpr std::size_t kMaxRequiredKeysPerCheck = 128;
inline constexpr std::size_t kMaxOutputsPerCrossCheck = 32;
inline constexpr std::size_t kMaxSumOperands = 64;

// --------------------------------------------------------------------------
// VerificationSpec
// --------------------------------------------------------------------------

struct VerificationCheckSpec
{
    std::string checkId;      ///< unique within the spec, non-empty
    std::string kind;         ///< member of kCheckKinds
    std::string description;  ///< optional teaching-facing intent line
    Json::Value params{Json::objectValue}; ///< kind-specific, validated
};

struct VerificationSpec
{
    int schemaVersion = 1;
    std::string specId;    ///< stable identity, non-empty
    std::string scope;     ///< member of kSpecScopes ("node" | "task")
    std::vector<VerificationCheckSpec> checks; ///< declaration order preserved end-to-end
};

/// Structural validation, kind-specific params included. @returns an empty
/// vector when the spec is well-formed, else one human-readable reason per
/// violation in a DETERMINISTIC order (spec-level first, then declaration
/// order). Vacuous checks (e.g. a grid check with zero constraints) are
/// rejected here — a check that cannot fail must not exist (the #1179
/// oracle-potency discipline, applied at the schema level).
std::vector<std::string> validateSpec( const VerificationSpec &spec );

/// Kind-specific params validation for ONE check (same rules validateSpec
/// applies, including the vacuous-check refusal). Engine backstop for
/// checks that reach evaluation without a full-spec validation. @returns an
/// empty vector when the params are well-formed for @p check.kind.
std::vector<std::string> validateCheckParams( const VerificationCheckSpec &check );

/// Canonical spec document (schema marker + sorted keys + 12-significant-
/// digit doubles). The spec digest is sha256 over this canonical text.
Json::Value specToJson( const VerificationSpec &spec );

/// Inverse of specToJson with STRICT schema rejection: unknown top-level
/// and per-check fields, wrong schema marker, wrong types and duplicate
/// check ids are errors — a spec is caller-supplied content.
/// @returns false + @p error on any mismatch (out is left unmodified).
bool specFromJson( const Json::Value &json, VerificationSpec &out, std::string &error );

/// Bounded text parse (jsoncpp CharReader, explicit stackLimit — a spec is
/// untrusted content) + specFromJson + validateSpec. On schema rejection
/// @returns false with @p error; on structural rejection @returns false
/// with @p validationErrors populated (the caller chooses which surface to
/// show).
bool parseSpec( const std::string &text, VerificationSpec &out, std::string &error,
                std::vector<std::string> &validationErrors );

/// sha256 of the canonical spec document text.
std::string specDigest( const VerificationSpec &spec );

// --------------------------------------------------------------------------
// Results, evidence, report
// --------------------------------------------------------------------------

struct VerificationEvidence
{
    std::string source;  ///< what was observed (provider + path/key), e.g. "grid:<path>"
    Json::Value observed{Json::objectValue};  ///< observed facts (deterministic projection)
    Json::Value expected{Json::objectValue};  ///< the pinned expectation from the spec params
    bool hasSampling = false;
    std::size_t sampledPoints = 0; ///< bounded-sample size when sampling applied

    Json::Value toCanonicalJson() const;
    /// Inverse of toCanonicalJson; false + @p error on shape mismatch.
    bool fromCanonicalJson( const Json::Value &json, std::string &error );
};

struct VerificationCheckResult
{
    std::string checkId;
    std::string kind;
    VerificationStatus status = VerificationStatus::Indeterminate;
    std::string code;    ///< empty on Pass; else a verify:e_*/verify:i_* wire code
    std::string message; ///< one-line human explanation (teaching + logs)
    std::optional<VerificationEvidence> evidence;
};

struct VerificationReport
{
    int schemaVersion = 1;
    std::string specId;
    std::string scope;
    std::string specDigest; ///< digest of the evaluated spec (canonical text)
    std::vector<VerificationCheckResult> checks; ///< spec declaration order
    VerificationStatus overall = VerificationStatus::Indeterminate;

    std::size_t passCount() const;
    std::size_t failCount() const;
    std::size_t indeterminateCount() const;

    /// Canonical report document: schema marker, checks in declaration
    /// order, per-status counts. NO digest field inside (the digest is
    /// sha256 over this canonical text — a field carrying its own hash is
    /// unrepresentable).
    Json::Value toCanonicalJson() const;
    /// sha256 over the canonical report text. Empty string = refusal
    /// sentinel: the body carries a non-finite number and cannot be sealed
    /// (an unsealable report must not masquerade as sha256("")-sealed).
    std::string digest() const;

    /// Strict inverse of toCanonicalJson + digest re-verification: a
    /// persisted report whose embedded digest does not match its body is
    /// rejected (tamper-evidence for batch flows). @returns false + @p error.
    static bool fromCanonicalJson( const Json::Value &json, VerificationReport &out,
                                   std::string &error, const std::string &expectedDigest );
};

/// Aggregates check results into a report: overall = lattice of the check
/// statuses; empty input stays Indeterminate (kCodeEmptyInput documents the
/// cause on the report level via an indeterminate count of zero — callers
/// that need the code on a check surface should synthesize a check).
VerificationReport buildReport( const std::string &specId, const std::string &scope,
                                const std::string &specDigest,
                                std::vector<VerificationCheckResult> checks );

// --------------------------------------------------------------------------
// Canonical JSON primitives
// --------------------------------------------------------------------------

/// Deterministic JSON text: compact, no comments, sorted object keys,
/// doubles at 12 significant digits (0.1+0.2 == 0.3 in this text space —
/// digests are stable across evaluation orders). A body containing a
/// non-finite number returns the EMPTY string as a refusal sentinel: the
/// writer would otherwise seal NaN as `null` (digest-indistinguishable from
/// a real null) and infinities as `1e+9999` (parser-divergent).
///
/// Honest binding limit: the 12-significant-digit space DELIBERATELY merges
/// doubles that differ beyond 12 digits, so the seal detects structural
/// edits, not sub-precision perturbation of an observed metric value — the
/// digest is an order-stability and tamper-EVIDENCE seal, not a
/// sub-digit-precision tamper lock.
std::string canonicalJsonText( const Json::Value &value );

} // namespace sicnu::verify
