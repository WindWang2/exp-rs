// fault_runner.cpp — the scenario runner (see header for the pipeline).
#include "fault_runner.h"

#include "fault_diagnosis.h"
#include "fault_expectations.h"
#include "fault_fixtures.h"
#include "fault_observables.h"
#include "fault_sandbox.h"
#include "fault_transforms.h"
#include "util/canonical_json.h"

#include <algorithm>

namespace sicnu::faultlab
{

namespace
{

/// Cleanup + re-digest, shared by every exit path so sandbox and source
/// facts are always reported honestly. A null source means the fixture
/// never materialized: nothing to verify, so source immutability is not
/// claimed. A source that cannot be digested (canonical refusal) is the
/// same honesty rule: immutability is provable only against a digest.
void finishRun( FaultSandbox &sandbox, const FaultGrid *source, FaultRunReport &report )
{
    sandbox.cleanup();
    report.sandboxRemoved = sandbox.verifyNoResidue();
    if ( source != nullptr )
    {
        std::string digestError;
        if ( !canonical::trySha256HexOf( source->toJson(), report.sourceDigestAfter, digestError ) )
        {
            report.sourceUnchanged = false;
            report.diagnostics.push_back(
                FaultDiagnostic{ "faultlab.digest_failed",
                                 "cannot digest the source fixture after the run: " + digestError,
                                 FaultSeverity::Error } );
            return;
        }
        report.sourceUnchanged =
            !report.sourceDigestBefore.empty() && report.sourceDigestBefore == report.sourceDigestAfter;
        if ( !report.sourceUnchanged )
        {
            report.diagnostics.push_back(
                FaultDiagnostic{ "faultlab.source_mutated",
                                 "source fixture digest drifted during the run",
                                 FaultSeverity::Error } );
        }
    }
}

} // namespace

FaultResult<FaultRunReport> runFaultScenario( const FaultScenario &scenario,
                                              const FaultRunOptions &options )
{
    FaultRunReport report;
    report.scenarioId = scenario.scenarioId;
    report.title = scenario.title;
    report.faultFamily = scenario.fault.familyId;
    report.seed = scenario.fault.seed;
    report.fixtureSeed = scenario.fixtureSeed;
    report.fixtureId = scenario.fixtureId;
    report.expectedDiagnosisSignature = scenario.expectedDiagnosisSignature;

    auto sandbox = FaultSandbox::create( options.sandboxRoot );
    if ( !sandbox.ok )
    {
        report.diagnostics = sandbox.diagnostics;
        return makeOk( std::move( report ) );
    }

    // --- materialize the base fixture (never written to: it lives in
    // memory and is re-digested after the run) ---
    const auto fixture = makeFixture( scenario.fixtureId, scenario.fixtureParams,
                                      scenario.fixtureSeed );
    if ( !fixture.ok )
    {
        report.diagnostics = fixture.diagnostics;
        finishRun( sandbox.value, nullptr, report );
        report.passed = false;
        return makeOk( std::move( report ) );
    }
    const FaultGrid source = fixture.value;
    {
        std::string digestError;
        if ( !canonical::trySha256HexOf( source.toJson(), report.sourceDigestBefore, digestError ) )
        {
            report.diagnostics.push_back(
                FaultDiagnostic{ "faultlab.digest_failed",
                                 "cannot digest the source fixture: " + digestError,
                                 FaultSeverity::Error } );
            finishRun( sandbox.value, nullptr, report ); // nothing verifiable
            report.passed = false;
            return makeOk( std::move( report ) );
        }
    }

    // --- copy within the byte budget, then inject into the copy only ---
    const auto copy = FaultSandbox::copyWithinBudget( source, scenario.maxBytes );
    if ( !copy.ok )
    {
        report.diagnostics = copy.diagnostics;
        finishRun( sandbox.value, &source, report );
        report.passed = false;
        return makeOk( std::move( report ) );
    }

    FaultGrid faulted = copy.value;
    const FaultOutcome injected = applyFault( faulted, scenario.fault );
    report.faultApplied = injected.ok;
    report.faultMutations = injected.mutations;
    if ( !injected.ok )
    {
        report.diagnostics = injected.diagnostics;
    }

    // --- measure, check expectations, diagnose ---
    const ObservableSet cleanSet = measureObservables( source );
    const ObservableSet faultedSet = measureObservables( faulted );
    if ( injected.ok )
    {
        report.expectationResults =
            checkExpectations( cleanSet, faultedSet, scenario.expectations );
        report.expectationsPassed = std::all_of(
            report.expectationResults.begin(), report.expectationResults.end(),
            []( const ExpectationResult &result ) { return result.passed; } );
        report.actualDiagnosisSignature = diagnoseTransition( cleanSet, faultedSet );
        report.diagnosisMatched =
            report.actualDiagnosisSignature == scenario.expectedDiagnosisSignature;
    }

    // --- deterministic replay: inject a second time, compare digests ---
    FaultGrid replay = FaultSandbox::copyOf( source );
    const FaultOutcome replayed = applyFault( replay, scenario.fault );
    std::string faultedDigest;
    std::string digestError;
    if ( !canonical::trySha256HexOf( faulted.toJson(), faultedDigest, digestError ) ||
         !canonical::trySha256HexOf( replay.toJson(), report.replayDigest, digestError ) )
    {
        // An unhashable faulted state cannot prove replay determinism: two
        // empty digests would compare equal, so the claim is refused.
        report.diagnostics.push_back(
            FaultDiagnostic{ "faultlab.digest_failed",
                             "cannot digest the faulted/replayed fixture: " + digestError,
                             FaultSeverity::Error } );
        report.replayDeterministic = false;
    }
    else
    {
        report.replayDeterministic =
            replayed.ok == injected.ok && report.replayDigest == faultedDigest;
    }

    // --- cleanup + source immutability ---
    finishRun( sandbox.value, &source, report );

    report.passed = report.faultApplied && report.expectationsPassed && report.diagnosisMatched &&
                    report.sandboxRemoved && report.sourceUnchanged && report.replayDeterministic;
    return makeOk( std::move( report ) );
}

} // namespace sicnu::faultlab
