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
#include <QRegularExpression>
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

// ---------------------------------------------------------------------------
// T-p2-4 (H2): CSV formula-injection guard sees through leading whitespace
// ---------------------------------------------------------------------------

#include "experiment_studio/studio_export.h"

TEST_CASE( "r18e CSV: leading-whitespace formula bait is neutralized",
           "[studio][export][csv][r18_e]" )
{
    // R1 review P2-4: spreadsheet engines trim leading blanks before
    // formula evaluation, so " =cmd|..." executed while a position-0 scan
    // saw a space. The apostrophe guard must land before the padding.
    using namespace sicnu::experiment_studio;
    QJsonObject report;
    report.insert( QStringLiteral( "study_id" ), QStringLiteral( "s1" ) );
    const QStringList baits = { QStringLiteral( " =cmd|'/c calc'!A0" ),
                                QStringLiteral( "\t+HYPERLINK(\"x\")" ),
                                QStringLiteral( "-123" ),
                                QStringLiteral( " -123" ),
                                QStringLiteral( "\t-42.5" ),
                                QStringLiteral( "normal text" ) };
    QJsonArray rows;
    for ( const QString &bait : baits )
    {
        QJsonObject row;
        row.insert( QStringLiteral( "point_id" ), bait );
        row.insert( QStringLiteral( "status" ), QStringLiteral( "failed" ) );
        row.insert( QStringLiteral( "error_summary" ), QStringLiteral( "ok" ) );
        rows.append( row );
    }
    report.insert( QStringLiteral( "run_table" ), rows );

    const QString csv = studyRunTableToCsv( report );
    // Every bait cell is apostrophe-prefixed (before the whitespace).
    CHECK( csv.contains( QStringLiteral( "' =cmd" ) ) );
    CHECK( csv.contains( QStringLiteral( "'\t+HYPERLINK" ) ) );
    // Plain and PADDED negative numbers survive verbatim (numeric typing
    // preserved — R2 review P3-1), as does ordinary text.
    CHECK( csv.contains( QStringLiteral( "-123" ) ) );
    CHECK( csv.contains( QStringLiteral( " -123" ) ) );
    CHECK( csv.contains( QStringLiteral( "\t-42.5" ) ) );
    CHECK_FALSE( csv.contains( QStringLiteral( "' -123" ) ) );
    CHECK( csv.contains( QStringLiteral( "normal text" ) ) );
    // No CELL starts (after optional padding) with formula bait: the
    // escaped text still contains the bait substring after the apostrophe
    // — the guard is about the cell's first effective character.
    const QStringList lines = csv.split( QLatin1Char( '\n' ) );
    for ( const QString &line : lines )
    {
        const int effective = line.indexOf( QRegularExpression( QStringLiteral( "\\S" ) ) );
        if ( effective < 0 )
            continue;
        const QChar c = line.at( effective );
        INFO( "line: " << line.toStdString() );
        CHECK( c != QLatin1Char( '=' ) );
        CHECK( c != QLatin1Char( '+' ) );
        CHECK( c != QLatin1Char( '@' ) );
        // A negative number cell legitimately starts with '-'.
        if ( c == QLatin1Char( '-' ) )
            CHECK( line.at( effective + 1 ).isDigit() );
    }
}
