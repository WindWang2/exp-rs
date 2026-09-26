// test_experiment_bridge_r4.cpp — Track 11 (R4) regression for the lab
// report markdown fence contract (#1333 item 3): the report wraps JSON
// payloads in four-backtick fences; a payload whose string values carry
// longer backtick runs used to close the fence early and inject its tail
// as markdown prose. The rendered fence is now one longer than the longest
// backtick run in the payload (CommonMark), payload bytes unchanged.
#include <catch2/catch_test_macros.hpp>

#include "experiment/bridge/lab_report.h"
#include "experiment/bridge/lab_report_writers.h"
#include "experiment/debugger/evidence_source.h"
#include "experiment/debugger/debugger_types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace sicnu::experiment;

namespace
{

QJsonObject minimalValidDocument( const QJsonObject &stepParams )
{
    QJsonObject document;
    document.insert( QStringLiteral( "schema" ),
                     QLatin1String( sicnu::experiment::kLabReportSchemaId ) );
    document.insert( QStringLiteral( "header" ),
                     QJsonObject{ { QStringLiteral( "reportId" ),
                                    QStringLiteral( "r4-fence-report" ) },
                                  { QStringLiteral( "labId" ), QStringLiteral( "lab-1" ) },
                                  { QStringLiteral( "labName" ), QStringLiteral( "Fence lab" ) },
                                  { QStringLiteral( "objective" ), QStringLiteral( "objective" ) },
                                  { QStringLiteral( "student" ), QStringLiteral( "student" ) },
                                  { QStringLiteral( "session" ), QStringLiteral( "session-1" ) },
                                  { QStringLiteral( "generatedAtUtc" ),
                                    QStringLiteral( "2026-09-27T00:00:00Z" ) },
                                  { QStringLiteral( "softwareRevision" ),
                                    QStringLiteral( "rev" ) },
                                  { QStringLiteral( "gitSha" ), QStringLiteral( "sha" ) } } );
    document.insert( QStringLiteral( "runs" ), QJsonArray{} );
    document.insert(
        QStringLiteral( "steps" ),
        QJsonArray{ QJsonObject{ { QStringLiteral( "index" ), 1 },
                                 { QStringLiteral( "operator" ), QStringLiteral( "rs:noop" ) },
                                 { QStringLiteral( "success" ), true },
                                 { QStringLiteral( "startedAtUtc" ),
                                   QStringLiteral( "2026-09-27T00:00:01Z" ) },
                                 { QStringLiteral( "durationMs" ), 1.0 },
                                 { QStringLiteral( "params" ), stepParams } } } );
    document.insert( QStringLiteral( "statistics" ), QJsonArray{} );
    document.insert( QStringLiteral( "thumbnails" ), QJsonArray{} );
    document.insert( QStringLiteral( "grade" ),
                     QJsonObject{ { QStringLiteral( "status" ),
                                    QStringLiteral( "unavailable" ) } } );
    document.insert( QStringLiteral( "lineage" ), QJsonObject{} );
    document.insert( QStringLiteral( "environment" ), QJsonObject{} );
    document.insert( QStringLiteral( "replay" ), QJsonObject{} );
    return document;
}

int longestBacktickRun( const QString &text )
{
    int longest = 0;
    int run = 0;
    for ( const QChar &ch : text )
    {
        if ( ch == QLatin1Char( '`' ) )
        {
            ++run;
            longest = qMax( longest, run );
        }
        else
        {
            run = 0;
        }
    }
    return longest;
}

} // namespace

TEST_CASE( "markdown fences outgrow backtick runs inside step payloads (r4)",
           "[experiment][bridge][lab_report][r4]" )
{
    // A step parameter whose value carries a six-backtick run — exactly the
    // shape that used to close the report-standard four-backtick fence and
    // inject the JSON tail as markdown.
    const QString poison = QStringLiteral( "``````" );
    const auto document = minimalValidDocument(
        QJsonObject{ { QStringLiteral( "code" ), poison } } );
    REQUIRE( LabReportBuilder::validate( document ).has_value() );

    const QString md = labReportMarkdown( document ).value();
    REQUIRE( !md.isEmpty() );
    CHECK( md.contains( poison ) ); // payload bytes are never altered

    // Every fence inside the markdown outgrows the longest payload run: no
    // payload run can close a fence, so the fence count must equal the run
    // length + 1 for the poisoned block.
    const QString poisonedLine = QStringLiteral( "{\"code\":\"``````\"}" );
    REQUIRE( md.contains( poisonedLine ) );
    const int fenceAroundPayload = longestBacktickRun( poisonedLine ) + 1;
    CHECK( md.contains( QString( fenceAroundPayload, QLatin1Char( '`' ) ) +
                        QStringLiteral( "json\n" ) ) );
}

TEST_CASE( "clean payloads keep the report-standard four-backtick fence (r4)",
           "[experiment][bridge][lab_report][r4]" )
{
    // No backticks in the payload: rendering is byte-stable with the
    // historical four-backtick format (zero drift for existing consumers).
    const auto document =
        minimalValidDocument( QJsonObject{ { QStringLiteral( "threshold" ), 0.5 } } );
    REQUIRE( LabReportBuilder::validate( document ).has_value() );
    const QString md = labReportMarkdown( document ).value();
    CHECK( md.contains( QStringLiteral( "````json\n" ) ) );
    CHECK( !md.contains( QStringLiteral( "`````json\n" ) ) ); // five backticks: absent
}

// WP-D partition oracle (#1333 item 1 family / evidence_source structure):
// a bridge workflow document WITHOUT the steps partition is torn evidence —
// typed refusal, never silently "no steps" — while an EMPTY steps array is
// a legal empty partition, and a truncated step list still normalizes with
// its truncation warning riding along.
TEST_CASE( "bridge workflow steps partition: missing refused, empty legal (r4)",
           "[experiment][debugger][r4]" )
{
    using sicnu::experiment::debugger::stepEvidenceFromBridgeWorkflowMetrics;

    // Missing partition: refused, typed malformed — not "zero steps".
    {
        const auto missing = stepEvidenceFromBridgeWorkflowMetrics( QJsonObject{} );
        REQUIRE( !missing.has_value() );
        bool malformed = false;
        for ( const auto &diagnostic : missing.diagnostics() )
            if ( diagnostic.code == QLatin1String( kCodeMalformedEvidence ) )
                malformed = true;
        CHECK( malformed );
    }

    // Empty partition: legal, zero steps, StepsEvidence mode.
    {
        QJsonObject emptyWorkflow;
        emptyWorkflow.insert( QStringLiteral( "steps" ), QJsonArray{} );
        const auto empty = stepEvidenceFromBridgeWorkflowMetrics( emptyWorkflow );
        REQUIRE( empty.has_value() );
        CHECK( empty.value().mode ==
               sicnu::experiment::debugger::StepEvidenceMode::StepsEvidence );
        CHECK( empty.value().steps.isEmpty() );
    }

    // Truncated list: normalizes with a visible warning, never silently
    // presented as complete evidence.
    {
        QJsonObject truncatedWorkflow;
        truncatedWorkflow.insert( QStringLiteral( "steps_truncated" ), true );
        QJsonArray steps;
        steps.append( QJsonObject{ { QStringLiteral( "id" ), QStringLiteral( "s1" ) },
                                   { QStringLiteral( "operator" ), QStringLiteral( "rs:noop" ) } } );
        truncatedWorkflow.insert( QStringLiteral( "steps" ), steps );
        const auto truncated = stepEvidenceFromBridgeWorkflowMetrics( truncatedWorkflow );
        REQUIRE( truncated.has_value() );
        CHECK( truncated.value().steps.size() == 1 );
        CHECK( !truncated.diagnostics().isEmpty() );
    }
}
