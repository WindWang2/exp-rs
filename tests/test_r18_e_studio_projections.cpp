/***************************************************************************
  tests/test_r18_e_studio_projections.cpp — R18-E E05 studio projection lane.

  RED-first pins:

    T501 (H6): projectSensitivity associated spatialSummaries.first() with
      whatever point was selected — the teaching UI could read another
      point's run-vs-baseline delta as the selected point's. The delta
      must belong to the selection (via the located row's output asset) or
      stay empty; mis-association is never acceptable.

    T502 (H7): uncertainty_missing_replicate fired whenever the spec echo
      lacked budget.seed_replicates — even when envelopes WITH bands
      existed, proving replicates ran. The flag belongs to absent
      replicate EVIDENCE, and the issue text must not tell a student to
      re-declare what they already declared.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "experiment_studio/sensitivity_projection.h"
#include "study/study_analysis.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace sicnu::experiment_studio;
using namespace sicnu::study;

namespace
{

SpatialDifferenceSummary delta( const QString &runPath, double changedPercent )
{
    SpatialDifferenceSummary s;
    s.baselinePath = QStringLiteral( "/base.tif" );
    s.runPath = runPath;
    s.totalPixels = 100;
    s.validPixels = 100;
    s.changedPixels = static_cast<qint64>( changedPercent );
    s.changedPercent = changedPercent;
    return s;
}

StudyRunRow row( const QString &pointId, const QString &outputPath )
{
    StudyRunRow r;
    r.pointId = pointId;
    r.runId = QStringLiteral( "run-" ) + pointId;
    r.status = QStringLiteral( "recorded" );
    r.outputAssetPath = outputPath;
    return r;
}

UncertaintyEnvelope envelope( const QString &metric )
{
    UncertaintyEnvelope e;
    e.metricName = metric;
    UncertaintyBand band;
    band.runCount = 4;
    band.mean = 0.5;
    band.populationStdDev = 0.1;
    e.bands.append( band );
    return e;
}

} // namespace

TEST_CASE( "r18e T501: the spatial delta summary belongs to the selected point",
           "[studio][sensitivity][r18_e]" )
{
    StudyReport report;
    report.studyId = QStringLiteral( "study-1" );
    report.runTable = { row( QStringLiteral( "p1" ), QStringLiteral( "/out/p1.tif" ) ),
                        row( QStringLiteral( "p2" ), QStringLiteral( "/out/p2.tif" ) ) };
    report.spatialSummaries = { delta( QStringLiteral( "/out/p2.tif" ), 50.0 ),
                                delta( QStringLiteral( "/out/p1.tif" ), 5.0 ) };

    SECTION( "selection p1 shows p1's delta, not the first summary's" )
    {
        const SensitivityViewModel vm = projectSensitivity( report, QStringLiteral( "p1" ) );
        REQUIRE( vm.selectedPointId == QStringLiteral( "p1" ) );
        CHECK( vm.spatialDeltaSummary.value( QStringLiteral( "run_path" ) ).toString()
               == QStringLiteral( "/out/p1.tif" ) );
        CHECK( vm.spatialDeltaSummary.value( QStringLiteral( "changed_percent" ) ).toDouble()
               == 5.0 );
    }

    SECTION( "a selection with no delta of its own shows none — never a sibling's" )
    {
        report.spatialSummaries = { delta( QStringLiteral( "/out/p2.tif" ), 50.0 ) };
        const SensitivityViewModel vm = projectSensitivity( report, QStringLiteral( "p1" ) );
        CHECK( vm.spatialDeltaSummary.isEmpty() );
    }

    SECTION( "no selection keeps the legacy first-summary behavior" )
    {
        const SensitivityViewModel vm = projectSensitivity( report, QString() );
        CHECK( vm.spatialDeltaSummary.value( QStringLiteral( "run_path" ) ).toString()
               == QStringLiteral( "/out/p2.tif" ) );
    }
}

TEST_CASE( "r18e T502: replicate-missing flags evidence, not spec-echo spelling",
           "[studio][sensitivity][r18_e]" )
{
    StudyReport report;
    report.studyId = QStringLiteral( "study-2" );
    report.runTable = { row( QStringLiteral( "p1" ), QStringLiteral( "/out/p1.tif" ) ) };

    SECTION( "bands exist — replicates ran; no stale-echo flag" )
    {
        report.envelopes = { envelope( QStringLiteral( "kappa" ) ) };
        // spec echo WITHOUT budget.seed_replicates (the stale/legacy echo)
        const SensitivityViewModel vm = projectSensitivity( report, QString() );
        CHECK_FALSE( vm.uncertaintyMissingReplicate );
    }

    SECTION( "no bands and a replicate-declaring spec — flagged, wording does not blame the spec" )
    {
        QJsonObject budget;
        budget.insert( QStringLiteral( "seed_replicates" ), 4 );
        report.specJson.insert( QStringLiteral( "budget" ), budget );
        const SensitivityViewModel vm = projectSensitivity( report, QString() );
        CHECK( vm.uncertaintyMissingReplicate );
        bool blames = false;
        for ( const QString &issue : vm.issues )
            if ( issue.contains( QStringLiteral( "set budget.seed_replicates" ) ) )
                blames = true;
        CHECK_FALSE( blames );
    }

    SECTION( "no bands and replicates undeclared — flagged with the fix hint" )
    {
        const SensitivityViewModel vm = projectSensitivity( report, QString() );
        CHECK( vm.uncertaintyMissingReplicate );
        bool hints = false;
        for ( const QString &issue : vm.issues )
            if ( issue.contains( QStringLiteral( "seed_replicates" ) ) )
                hints = true;
        CHECK( hints );
    }
}
