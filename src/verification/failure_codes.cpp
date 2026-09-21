// failure_codes.cpp — the closed table every agent-facing surface derives from.
//
// Adding a code means adding ONE row here; nothing else keeps a copy. The
// `allFailureCodes()` ordering guarantee (sorted, unique) is asserted by
// tests/test_verifier_core_14.cpp, which also asserts each row carries a
// category, a replan class and a hint.

#include "verification/failure_codes.h"

#include <algorithm>

namespace sicnu::verification
{

namespace
{

struct FailureCodeSpec
{
    const char *code;
    const char *category;
    ReplanClass replan;
    const char *hint;
    const char *harnessCounterpart; // "" when none exists
};

// Grouped by concern for readability; iteration order does not matter because
// allFailureCodes() sorts.
const FailureCodeSpec kFailureCodeTable[] = {
    // ---- caller-side (spec/program) defects -----------------------------
    { failure_codes::kSpecInvalid, "validation", ReplanClass::Abort,
      "The verification specification itself is invalid or unreadable; nothing "
      "was judged, so no result can be trusted on the strength of this run.",
      "VALIDATION_ERROR" },
    { failure_codes::kUnsupportedCheckKind, "validation", ReplanClass::Abort,
      "A check kind this verifier does not implement was requested. It was NOT "
      "skipped: the check is indeterminate rather than absent.",
      "VALIDATION_ERROR" },
    { failure_codes::kNoChecks, "validation", ReplanClass::Replan,
      "The specification carried no checks, so there is no evidence for or "
      "against the result.",
      "VALIDATION_ERROR" },

    // ---- evidence supply -------------------------------------------------
    { failure_codes::kEvidenceUnavailable, "io", ReplanClass::Retry,
      "A fact needed to judge this check could not be obtained. This is not a "
      "failure of the result — it is absence of evidence.",
      "TRANSIENT_FAILURE" },
    { failure_codes::kEvidenceRefused, "io", ReplanClass::Retry,
      "A fact provider actively refused to answer; the refusal reason is in the "
      "details. Absence of evidence again, not a failed result.",
      "TRANSIENT_FAILURE" },

    // ---- state invariants -------------------------------------------------
    { failure_codes::kStateTokenMissing, "validation", ReplanClass::Replan,
      "The declared scientific state (numeric domain / radiometric state) of "
      "the input is unknown, so no statement about the output's validity can be "
      "made. Note that an UNDECLARED state is not enough to declare the result "
      "wrong either - it means this check cannot conclude.",
      "" },
    { failure_codes::kStateInvariantViolation, "validation", ReplanClass::Replan,
      "The result violates a stated scientific invariant of the operator (for "
      "example a linear-power operator applied to dB values). Treating it as "
      "correct would silently bias the science.",
      "INVALID_RADIOMETRY" },

    // ---- artifacts ---------------------------------------------------------
    { failure_codes::kArtifactMissing, "io", ReplanClass::Replan,
      "An expected output artifact does not exist, so the step produced nothing "
      "to judge.",
      "OUTPUT_INVALID" },
    { failure_codes::kArtifactKindMismatch, "validation", ReplanClass::Replan,
      "The artifact is of a different kind than declared (for example a vector "
      "where a raster was expected).",
      "OUTPUT_INVALID" },
    { failure_codes::kArtifactGridMismatch, "validation", ReplanClass::Replan,
      "Raster dimensions differ from the declared grid, so per-pixel comparison "
      "against any reference would be meaningless.",
      "GRID_MISMATCH" },
    { failure_codes::kArtifactSchemaMismatch, "validation", ReplanClass::Replan,
      "Required artifact facts are missing or have the wrong type.",
      "OUTPUT_INVALID" },

    // ---- numbers -----------------------------------------------------------
    { failure_codes::kNumericOutOfRange, "validation", ReplanClass::Replan,
      "A measured metric lies outside the physically meaningful range that was "
      "declared for it.",
      "INVALID_RADIOMETRY" },
    { failure_codes::kNumericNotFinite, "validation", ReplanClass::Replan,
      "A metric is NaN or infinite. Comparisons against NaN always evaluate to "
      "false, so this can never be treated as in range.",
      "INVALID_RADIOMETRY" },

    // ---- relations, provenance, reproducibility ----------------------------
    { failure_codes::kRelationInconsistent, "validation", ReplanClass::Replan,
      "Two facts that are required to agree do not agree.",
      "FACT_CONFLICT" },
    { failure_codes::kProvenanceIncomplete, "validation", ReplanClass::Replan,
      "Required provenance dimensions are absent, so the result cannot be cited "
      "or reproduced even if it is numerically plausible.",
      "OUTPUT_INVALID" },
    { failure_codes::kReproducibilityDigestMismatch, "validation", ReplanClass::Replan,
      "The reproducibility digest does not match the recorded one: this run is "
      "not a reproduction of the run it claims to reproduce.",
      "IDENTITY_MISMATCH" },
    { failure_codes::kCrossOutputInconsistent, "validation", ReplanClass::Replan,
      "Two outputs that must agree disagree beyond the declared tolerance.",
      "FACT_CONFLICT" },

    // ---- resource ----------------------------------------------------------
    { failure_codes::kBudgetExceeded, "resource", ReplanClass::Retry,
      "The verification ran past its declared budget; the checks it did not "
      "reach are indeterminate, not passed.",
      "RESOURCE_OVER_BUDGET" },
};

const FailureCodeSpec *findSpec( const std::string &code )
{
    for ( const FailureCodeSpec &spec : kFailureCodeTable )
    {
        if ( code == spec.code )
        {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace

std::vector<std::string> allFailureCodes()
{
    std::vector<std::string> codes;
    codes.reserve( sizeof( kFailureCodeTable ) / sizeof( kFailureCodeTable[0] ) );
    for ( const FailureCodeSpec &spec : kFailureCodeTable )
    {
        codes.emplace_back( spec.code );
    }
    std::sort( codes.begin(), codes.end() );
    codes.erase( std::unique( codes.begin(), codes.end() ), codes.end() );
    return codes;
}

bool isKnownFailureCode( const std::string &code )
{
    return findSpec( code ) != nullptr;
}

std::string failureCategoryForCode( const std::string &code )
{
    const FailureCodeSpec *spec = findSpec( code );
    return spec != nullptr ? std::string( spec->category ) : std::string( "runtime" );
}

ReplanClass replanClassForCode( const std::string &code )
{
    const FailureCodeSpec *spec = findSpec( code );
    return spec != nullptr ? spec->replan : ReplanClass::Replan;
}

const char *replanClassToWire( ReplanClass replanClass )
{
    switch ( replanClass )
    {
        case ReplanClass::None:
            return "none";
        case ReplanClass::Retry:
            return "retry";
        case ReplanClass::Replan:
            return "replan";
        case ReplanClass::Abort:
            return "abort";
    }
    return "none";
}

std::string failureHintForCode( const std::string &code )
{
    const FailureCodeSpec *spec = findSpec( code );
    return spec != nullptr ? std::string( spec->hint )
                           : std::string( "Unrecognised verification failure; treat the result "
                                          "as unverified rather than as passed." );
}

std::string toHarnessCode( const std::string &code )
{
    const FailureCodeSpec *spec = findSpec( code );
    return spec != nullptr ? std::string( spec->harnessCounterpart ) : std::string();
}

} // namespace sicnu::verification
