// test_suitability_core.cpp — Slice A: suitability status lattice, criterion /
// gap value objects, report aggregation and versioned serialization.
//
// RED-first contract for the schema slice:
//   - four-state lattice with a severity rank where Unknown ranks ABOVE
//     Suitable (partial evidence never claims "suitable");
//   - strict versioned JSON round-trips with typed failures;
//   - deterministic canonical JSON (sorted criteria / gaps) and a stable
//     content digest for downstream provenance.

#include <catch2/catch_test_macros.hpp>

#include "suitability/suitability_level.h"
#include "suitability/suitability_report.h"
#include "suitability/suitability_types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGap;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::aggregateSuitabilityLevels;
using sicnu::suitability::suitabilityLevelFromString;
using sicnu::suitability::suitabilityLevelToString;
using sicnu::suitability::suitabilitySeverityRank;

namespace
{

SuitabilityCriterion makeCriterion( const QString &id, SuitabilityLevel level,
                                    bool applicable = true )
{
    SuitabilityCriterion criterion;
    criterion.id = id;
    criterion.applicable = applicable;
    criterion.level = level;
    criterion.summary = QStringLiteral( "summary for %1" ).arg( id );
    criterion.notes.append( QStringLiteral( "note for %1" ).arg( id ) );
    criterion.evidence = QJsonObject{ { QStringLiteral( "measured" ), 42 } };
    return criterion;
}

SuitabilityGap makeGap( const QString &id, const QString &criterionId )
{
    SuitabilityGap gap;
    gap.id = id;
    gap.criterionId = criterionId;
    gap.description = QStringLiteral( "gap %1" ).arg( id );
    gap.evidence = QJsonObject{ { QStringLiteral( "required" ), QStringLiteral( "nir" ) } };
    return gap;
}

} // namespace

TEST_CASE( "suitability level string round-trip", "[suitability][level]" )
{
    REQUIRE( suitabilityLevelToString( SuitabilityLevel::Suitable ) == QStringLiteral( "suitable" ) );
    REQUIRE( suitabilityLevelToString( SuitabilityLevel::Marginal ) == QStringLiteral( "marginal" ) );
    REQUIRE( suitabilityLevelToString( SuitabilityLevel::Unsuitable ) == QStringLiteral( "unsuitable" ) );
    REQUIRE( suitabilityLevelToString( SuitabilityLevel::Unknown ) == QStringLiteral( "unknown" ) );

    for ( const SuitabilityLevel level :
          { SuitabilityLevel::Suitable, SuitabilityLevel::Marginal,
            SuitabilityLevel::Unsuitable, SuitabilityLevel::Unknown } )
    {
        const auto parsed = suitabilityLevelFromString( suitabilityLevelToString( level ) );
        REQUIRE( parsed.has_value() );
        REQUIRE( *parsed == level );
    }

    REQUIRE( !suitabilityLevelFromString( QStringLiteral( "ok" ) ).has_value() );
    REQUIRE( !suitabilityLevelFromString( QString() ).has_value() );
}

TEST_CASE( "suitability severity rank orders Unknown above Suitable", "[suitability][level]" )
{
    // Unknown blocks a "suitable" claim; Marginal reports measured weakness;
    // Unsuitable dominates everything.
    REQUIRE( suitabilitySeverityRank( SuitabilityLevel::Suitable )
             < suitabilitySeverityRank( SuitabilityLevel::Unknown ) );
    REQUIRE( suitabilitySeverityRank( SuitabilityLevel::Unknown )
             < suitabilitySeverityRank( SuitabilityLevel::Marginal ) );
    REQUIRE( suitabilitySeverityRank( SuitabilityLevel::Marginal )
             < suitabilitySeverityRank( SuitabilityLevel::Unsuitable ) );
}

TEST_CASE( "aggregate suitability levels never upgrades partial evidence", "[suitability][level]" )
{
    // No evidence at all claims nothing: empty -> Unknown.
    REQUIRE( aggregateSuitabilityLevels( {} ) == SuitabilityLevel::Unknown );
    REQUIRE( aggregateSuitabilityLevels( { SuitabilityLevel::Suitable } )
             == SuitabilityLevel::Suitable );
    REQUIRE( aggregateSuitabilityLevels( { SuitabilityLevel::Suitable,
                                           SuitabilityLevel::Unknown } )
             == SuitabilityLevel::Unknown );
    REQUIRE( aggregateSuitabilityLevels( { SuitabilityLevel::Unknown,
                                           SuitabilityLevel::Marginal } )
             == SuitabilityLevel::Marginal );
    REQUIRE( aggregateSuitabilityLevels( { SuitabilityLevel::Suitable,
                                           SuitabilityLevel::Marginal,
                                           SuitabilityLevel::Unsuitable } )
             == SuitabilityLevel::Unsuitable );
}

TEST_CASE( "criterion and gap JSON round-trip", "[suitability][types]" )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "spatial.coverage" ),
                                                    SuitabilityLevel::Marginal );
    criterion.gaps.append( makeGap( QStringLiteral( "coverage.below_minimum" ),
                                    criterion.id ) );
    criterion.diagnostics.append(
        { QStringLiteral( "suitability.example" ), QStringLiteral( "example diagnostic" ),
          sicnu::data::DiagnosticSeverity::Warning } );

    const QJsonObject json = criterion.toJson();
    const auto parsed = SuitabilityCriterion::fromJson( json );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->id == criterion.id );
    REQUIRE( parsed->applicable == criterion.applicable );
    REQUIRE( parsed->level == criterion.level );
    REQUIRE( parsed->summary == criterion.summary );
    REQUIRE( parsed->evidence == criterion.evidence );
    REQUIRE( parsed->notes == criterion.notes );
    REQUIRE( parsed->gaps.size() == criterion.gaps.size() );
    REQUIRE( parsed->gaps.first().id == criterion.gaps.first().id );
    REQUIRE( parsed->gaps.first().evidence == criterion.gaps.first().evidence );
    REQUIRE( parsed->diagnostics.size() == criterion.diagnostics.size() );
    REQUIRE( parsed->diagnostics.first().code == criterion.diagnostics.first().code );
    REQUIRE( parsed->diagnostics.first().severity == criterion.diagnostics.first().severity );

    const auto gapParsed = SuitabilityGap::fromJson( criterion.gaps.first().toJson() );
    REQUIRE( gapParsed.has_value() );
    REQUIRE( gapParsed->id == criterion.gaps.first().id );
    REQUIRE( gapParsed->criterionId == criterion.gaps.first().criterionId );
    REQUIRE( gapParsed->description == criterion.gaps.first().description );
}

TEST_CASE( "criterion fromJson fails typed on foreign schema", "[suitability][types]" )
{
    QJsonObject json = makeCriterion( QStringLiteral( "spatial.coverage" ),
                                      SuitabilityLevel::Unknown ).toJson();
    json.insert( QStringLiteral( "schema_version" ), 999 );

    const auto parsed = SuitabilityCriterion::fromJson( json );
    REQUIRE( !parsed.has_value() );
    REQUIRE( parsed.diagnostics().first().code == QStringLiteral( "suitability.criterion_schema" ) );

    // Missing id is invalid content, not a schema mismatch.
    QJsonObject noId = makeCriterion( QStringLiteral( "spatial.coverage" ),
                                      SuitabilityLevel::Unknown ).toJson();
    noId.remove( QStringLiteral( "id" ) );
    const auto parsedNoId = SuitabilityCriterion::fromJson( noId );
    REQUIRE( !parsedNoId.has_value() );
    REQUIRE( parsedNoId.diagnostics().first().code == QStringLiteral( "suitability.criterion_invalid" ) );
}

TEST_CASE( "report keeps criteria in canonical id order", "[suitability][report]" )
{
    SuitabilityReport report;
    report.addCriterion( makeCriterion( QStringLiteral( "temporal.density" ),
                                        SuitabilityLevel::Suitable ) );
    report.addCriterion( makeCriterion( QStringLiteral( "spatial.coverage" ),
                                        SuitabilityLevel::Marginal ) );
    report.addCriterion( makeCriterion( QStringLiteral( "spectral.bands" ),
                                        SuitabilityLevel::Unknown ) );

    REQUIRE( report.criteria().size() == 3 );
    REQUIRE( report.criteria().at( 0 ).id == QStringLiteral( "spatial.coverage" ) );
    REQUIRE( report.criteria().at( 1 ).id == QStringLiteral( "spectral.bands" ) );
    REQUIRE( report.criteria().at( 2 ).id == QStringLiteral( "temporal.density" ) );
}

TEST_CASE( "report overall level skips not-applicable criteria", "[suitability][report]" )
{
    SuitabilityReport report;
    report.addCriterion( makeCriterion( QStringLiteral( "grid.compatibility" ),
                                        SuitabilityLevel::Unsuitable, false ) );
    report.addCriterion( makeCriterion( QStringLiteral( "spatial.coverage" ),
                                        SuitabilityLevel::Suitable ) );
    REQUIRE( report.overallLevel() == SuitabilityLevel::Suitable );

    // Nothing applicable claims nothing.
    SuitabilityReport emptyApplicable;
    emptyApplicable.addCriterion( makeCriterion( QStringLiteral( "grid.compatibility" ),
                                                 SuitabilityLevel::Unsuitable, false ) );
    REQUIRE( emptyApplicable.overallLevel() == SuitabilityLevel::Unknown );

    SuitabilityReport nothing;
    REQUIRE( nothing.overallLevel() == SuitabilityLevel::Unknown );
}

TEST_CASE( "report collects and sorts gaps", "[suitability][report]" )
{
    SuitabilityReport report;
    SuitabilityCriterion bands = makeCriterion( QStringLiteral( "spectral.bands" ),
                                                SuitabilityLevel::Unsuitable );
    bands.gaps.append( makeGap( QStringLiteral( "band.missing.nir" ), bands.id ) );
    bands.gaps.append( makeGap( QStringLiteral( "band.missing.red_edge" ), bands.id ) );
    report.addCriterion( bands );

    SuitabilityCriterion labels = makeCriterion( QStringLiteral( "labels.availability" ),
                                                 SuitabilityLevel::Marginal );
    labels.gaps.append( makeGap( QStringLiteral( "samples.below_minimum" ), labels.id ) );
    report.addCriterion( labels );

    const QVector<SuitabilityGap> gaps = report.allGaps();
    REQUIRE( gaps.size() == 3 );
    REQUIRE( gaps.at( 0 ).id == QStringLiteral( "band.missing.nir" ) );
    REQUIRE( gaps.at( 1 ).id == QStringLiteral( "band.missing.red_edge" ) );
    REQUIRE( gaps.at( 2 ).id == QStringLiteral( "samples.below_minimum" ) );
}

TEST_CASE( "report JSON round-trip preserves subject and criteria", "[suitability][report]" )
{
    SuitabilityReport report;
    report.setDatasetVersionId( QStringLiteral( "dv-123" ) );
    report.setSceneIds( { QStringLiteral( "scene-a" ), QStringLiteral( "scene-b" ) } );
    report.setGoalDigest( QStringLiteral( "deadbeef" ) );
    report.addCriterion( makeCriterion( QStringLiteral( "spatial.coverage" ),
                                        SuitabilityLevel::Unsuitable ) );
    report.addCriterion( makeCriterion( QStringLiteral( "quality.cloud" ),
                                        SuitabilityLevel::Unknown ) );

    const auto parsed = SuitabilityReport::fromJson( report.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->datasetVersionId() == QStringLiteral( "dv-123" ) );
    REQUIRE( parsed->sceneIds() == report.sceneIds() );
    REQUIRE( parsed->goalDigest() == QStringLiteral( "deadbeef" ) );
    REQUIRE( parsed->criteria().size() == 2 );
    REQUIRE( parsed->overallLevel() == report.overallLevel() );
    REQUIRE( parsed->toJson() == report.toJson() );
}

TEST_CASE( "report fromJson fails typed on foreign schema or bad criteria", "[suitability][report]" )
{
    QJsonObject json = SuitabilityReport().toJson();
    json.insert( QStringLiteral( "schema_version" ), 2 );
    const auto foreign = SuitabilityReport::fromJson( json );
    REQUIRE( !foreign.has_value() );
    REQUIRE( foreign.diagnostics().first().code == QStringLiteral( "suitability.report_schema" ) );

    QJsonObject badCriterion = SuitabilityReport().toJson();
    QJsonObject criteriaArray = badCriterion.value( QStringLiteral( "criteria" ) ).toObject();
    criteriaArray.insert( QStringLiteral( "x" ), 1 );
    badCriterion.insert( QStringLiteral( "criteria" ), criteriaArray );
    const auto invalid = SuitabilityReport::fromJson( badCriterion );
    REQUIRE( !invalid.has_value() );
    REQUIRE( invalid.diagnostics().first().code == QStringLiteral( "suitability.report_invalid" ) );
}

TEST_CASE( "report content digest is deterministic for identical evidence", "[suitability][report]" )
{
    SuitabilityReport first;
    first.setDatasetVersionId( QStringLiteral( "dv-123" ) );
    first.addCriterion( makeCriterion( QStringLiteral( "spatial.coverage" ),
                                       SuitabilityLevel::Marginal ) );
    first.addCriterion( makeCriterion( QStringLiteral( "temporal.density" ),
                                       SuitabilityLevel::Suitable ) );

    SuitabilityReport second;
    second.setDatasetVersionId( QStringLiteral( "dv-123" ) );
    second.addCriterion( makeCriterion( QStringLiteral( "temporal.density" ),
                                        SuitabilityLevel::Suitable ) );
    second.addCriterion( makeCriterion( QStringLiteral( "spatial.coverage" ),
                                        SuitabilityLevel::Marginal ) );

    // Same content, different insertion order -> same canonical JSON, same digest.
    REQUIRE( first.contentDigest() == second.contentDigest() );
    REQUIRE( !first.contentDigest().isEmpty() );

    first.criteria()[0].evidence = QJsonObject{ { QStringLiteral( "measured" ), 43 } };
    REQUIRE( first.contentDigest() != second.contentDigest() );
}
