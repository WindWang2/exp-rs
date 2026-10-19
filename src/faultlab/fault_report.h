// fault_report.h — the `sicnu.faultlab.report/1` value objects.
//
// The report is the framework's machine-readable surface: what was
// injected, what observables moved (with evidence), whether the expected
// diagnosis was produced, the sandbox facts (removed / source unchanged),
// and the deterministic replay digest. The canonical body carries no
// timestamps and no absolute paths, so two runs of the same scenario
// digest byte-identically on any machine — the property an agent (or a
// benchmark harness) can key on.
#pragma once

#include "fault_types.h"

#include <string>
#include <vector>

namespace sicnu::faultlab
{

struct FaultRunReport
{
    std::string scenarioId;
    std::string title;
    std::string faultFamily;
    std::uint32_t seed = 0;
    std::uint32_t fixtureSeed = 0;
    std::string fixtureId;

    bool faultApplied = false;
    std::uint32_t faultMutations = 0;

    std::vector<ExpectationResult> expectationResults;
    bool expectationsPassed = false;

    std::string expectedDiagnosisSignature;
    std::string actualDiagnosisSignature;
    bool diagnosisMatched = false;

    // Sandbox facts.
    bool sandboxRemoved = false;
    std::string sourceDigestBefore;
    std::string sourceDigestAfter;
    bool sourceUnchanged = false;

    // Deterministic replay.
    std::string replayDigest;
    bool replayDeterministic = false;

    std::vector<FaultDiagnostic> diagnostics;

    bool passed = false;

    Json::Value toJson() const;
};

/// Canonical JSON text of the report (`sicnu.faultlab.report/1`).
std::string faultReportToJson( const FaultRunReport &report );

/// Lowercase hex SHA-256 of the canonical report body (64 chars).
std::string faultReportDigest( const FaultRunReport &report );

} // namespace sicnu::faultlab
