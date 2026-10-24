// tests/test_r21_e_studio_verifier_hook.cpp — R21-E W2 (#1613): the
// PRODUCTION verifier hook end-to-end through the studio composition layer.
//
// WHAT THIS PROVES (and what it does not — R2 P1-1): the hook installed by
// runFaultScenarioTeaching really materializes the faulted artifact and runs
// the referenced lab rules through the OutputVerifier teaching seam, and the
// runner folds the OBSERVED deductions into the scenario verdict. The
// control section (must_fail that does not fire -> scenario fails) is what
// makes the fold non-vacuous: the same hook with a different declaration
// produces the opposite verdict.
//
// WHAT IT DOES NOT PROVE: fault-specific discrimination. No shipped rules
// kind is band-ROLE sensitive (the grader's index assertions take POSITIONAL
// band indices, and band_role_swap swaps role labels only), so no rules file
// can distinguish a role swap from the clean fixture by value. Fault
// observability itself is the faultlab expectation lane's job
// (tests/test_r21_e_faultlab_verifier.cpp); this lane proves the
// artifact-level integration reads real deductions.

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "experiment_studio/live/studio_live.h"
#include "geospatial/crs/crs_policy.h"

#include <string>

using sicnu::experiment_studio::live::runFaultScenarioTeaching;

namespace
{
/// Capability probe (R2 P1-2): the hook materializes a CRS through
/// Crs::fromAuthid, which needs a locatable PROJ database. On a host without
/// one the lane is not testable — say so instead of failing red.
bool projDataAvailable()
{
    return sicnu::geo::Crs::fromAuthid( "EPSG:4326" ).isValid();
}


QJsonObject verifierScenario( const QString &rulesRef, const QJsonArray &mustFail )
{
    QJsonObject fault;
    fault.insert( QStringLiteral( "family" ), QStringLiteral( "band_role_swap" ) );
    fault.insert( QStringLiteral( "params" ),
                  QJsonObject{ { QStringLiteral( "role_a" ), QStringLiteral( "red" ) },
                               { QStringLiteral( "role_b" ), QStringLiteral( "nir" ) } } );
    fault.insert( QStringLiteral( "seed" ), 42 );

    QJsonObject fixture;
    fixture.insert( QStringLiteral( "fixture_id" ), QStringLiteral( "two_band_index_pair" ) );
    fixture.insert( QStringLiteral( "params" ), QJsonObject{} );
    fixture.insert( QStringLiteral( "seed" ), 42 );

    QJsonObject sandbox;
    sandbox.insert( QStringLiteral( "max_bytes" ), 1048576 );

    QJsonArray observables;
    observables.append( QJsonObject{ { QStringLiteral( "id" ), QStringLiteral( "band_roles" ) },
                                      { QStringLiteral( "relation" ),
                                        QStringLiteral( "changed" ) } } );
    observables.append( QJsonObject{ { QStringLiteral( "id" ), QStringLiteral( "index_mean" ) },
                                      { QStringLiteral( "relation" ),
                                        QStringLiteral( "delta_le" ) },
                                      { QStringLiteral( "value" ), -0.5 } } );

    QJsonObject verifier;
    verifier.insert( QStringLiteral( "rules_ref" ), rulesRef );
    verifier.insert( QStringLiteral( "must_fail_assertions" ), mustFail );

    QJsonObject expected;
    expected.insert( QStringLiteral( "observables" ), observables );
    expected.insert( QStringLiteral( "diagnosis" ),
                     QJsonObject{ { QStringLiteral( "signature" ),
                                    QStringLiteral( "all_negative_index" ) } } );
    expected.insert( QStringLiteral( "verifier" ), verifier );

    QJsonObject objective;
    objective.insert( QStringLiteral( "id" ), QStringLiteral( "LO-03" ) );
    objective.insert( QStringLiteral( "statement" ),
                      QStringLiteral( "Band metadata defines an index." ) );

    QJsonObject doc;
    doc.insert( QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab.faults/1" ) );
    doc.insert( QStringLiteral( "scenario_id" ), QStringLiteral( "fs_band_role_swap_ndvi" ) );
    doc.insert( QStringLiteral( "title" ), QStringLiteral( "Band role swap verifier e2e" ) );
    doc.insert( QStringLiteral( "fault" ), fault );
    doc.insert( QStringLiteral( "base_fixture" ), fixture );
    doc.insert( QStringLiteral( "sandbox" ), sandbox );
    doc.insert( QStringLiteral( "expected" ), expected );
    doc.insert( QStringLiteral( "learning_objective" ), objective );
    doc.insert( QStringLiteral( "cleanup" ),
                QJsonObject{ { QStringLiteral( "required" ), true },
                             { QStringLiteral( "verify_no_residue" ), true } } );
    return doc;
}
} // namespace

TEST_CASE( "studio verifier hook: rules really run over the faulted artifact",
           "[r21e][studio_verifier_hook]" )
{
    if ( !projDataAvailable() )
    {
        WARN( "PROJ database not locatable on this host — the hook cannot "
              "materialize a CRS; lane not testable here (see R2 P1-2)" );
        return;
    }

    SECTION( "the referenced rules run and the declared must_fail is observed" )
    {
        // ndvi_bandpair pins exactly this fixture's geometry (16x16, 2 bands,
        // EPSG:4326), so the run reaches the value assertions; the must_fail
        // id is a real deduction of the faulted artifact's grade.
        const QJsonArray mustFail{ QStringLiteral( "stats.index_mean_sigma" ) };
        const auto run =
            runFaultScenarioTeaching( verifierScenario( QStringLiteral( "ndvi_bandpair" ), mustFail ),
                                      QStringLiteral( "all_negative_index" ),
                                      QString() /* default sandbox root */ );
        REQUIRE( run.has_value() );
        const QJsonObject evidence = run->evidence;
        REQUIRE( evidence.contains( QStringLiteral( "verifier" ) ) );
        const QJsonObject verifier = evidence.value( QStringLiteral( "verifier" ) ).toObject();
        CHECK( verifier.value( QStringLiteral( "checked" ) ).toBool() );
        const QJsonArray observed = verifier.value( QStringLiteral( "observed_failed" ) ).toArray();
        bool mustFailFired = false;
        for ( const auto &id : observed )
            if ( id.toString() == QStringLiteral( "stats.index_mean_sigma" ) )
                mustFailFired = true;
        INFO( "observed failing assertions: " << observed );
        CHECK( mustFailFired );
    }

    SECTION( "control: a must_fail the grader does not observe fails the scenario" )
    {
        // Same hook, same fixture, but the declaration names an assertion
        // that does NOT fire on the faulted artifact: the fold must read the
        // real deductions and refuse — this is what makes the positive
        // section above non-vacuous (R2 P1-1).
        const QJsonArray mustFail{ QStringLiteral( "layout.stack" ) }; // not an
        // ndvi_bandpair assertion at all
        const auto run =
            runFaultScenarioTeaching( verifierScenario( QStringLiteral( "ndvi_bandpair" ), mustFail ),
                                      QStringLiteral( "all_negative_index" ),
                                      QString() );
        REQUIRE( run.has_value() );
        CHECK( run->issues.contains(
          QStringLiteral( "experiment_studio.fault_scenario_failed:"
                          " the scenario did not pass (see evidence)" ) ) );
        const QJsonObject evidence = run->evidence;
        REQUIRE( evidence.contains( QStringLiteral( "verifier" ) ) );
        const QJsonObject verifier = evidence.value( QStringLiteral( "verifier" ) ).toObject();
        CHECK( verifier.value( QStringLiteral( "checked" ) ).toBool() );
        CHECK_FALSE( verifier.value( QStringLiteral( "must_fail_observed" ) ).toBool() );
    }

    SECTION( "unresolvable rules_ref fails closed" )
    {
        const QJsonArray mustFail{ QStringLiteral( "stats.index_mean_sigma" ) };
        const auto run = runFaultScenarioTeaching(
          verifierScenario( QStringLiteral( "no_such_lab_r21e" ), mustFail ),
          QStringLiteral( "all_negative_index" ), QString() );
        REQUIRE( run.has_value() );
        // The hook could not grade (usage-class unknown lab): the scenario
        // must NOT pass — unchecked, not guessed.
        CHECK( run->issues.contains(
          QStringLiteral( "experiment_studio.fault_scenario_failed:"
                          " the scenario did not pass (see evidence)" ) ) );
        const QJsonObject evidence = run->evidence;
        REQUIRE( evidence.contains( QStringLiteral( "verifier" ) ) );
        const QJsonObject verifier = evidence.value( QStringLiteral( "verifier" ) ).toObject();
        CHECK_FALSE( verifier.value( QStringLiteral( "checked" ) ).toBool() );
        CHECK_FALSE( verifier.value( QStringLiteral( "must_fail_observed" ) ).toBool() );
    }
}
