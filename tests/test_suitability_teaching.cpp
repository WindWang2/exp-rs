// test_suitability_teaching.cpp — Slice H2: the teaching narrative.
//
// The narrative is DERIVED from the report and must never contradict it:
//   - every criterion paragraph opens with the criterion's human name and
//     carries the exact level string the JSON carries;
//   - an Unsuitable/Marginal paragraph quotes every gap (description AND
//     machine id); an Unknown paragraph states why ("cannot judge" + notes);
//     a not-applicable paragraph says it was skipped and why;
//   - the overview line states the overall level word, the gap total and
//     the most pressing problems (bounded at three);
//   - the same report always teaches byte-identical lines;
//   - degenerate reports (empty, all not-applicable) do not crash.

#include <catch2/catch_test_macros.hpp>

#include "suitability/suitability_assessor.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_level.h"
#include "suitability/suitability_report.h"
#include "suitability/suitability_teaching.h"
#include "suitability/suitability_types.h"

#include "data/data_asset.h"

#include <QDateTime>

#include <algorithm>

using sicnu::data::AssetState;
using sicnu::dataset::BenchmarkTaskFamily;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::explainCriterion;
using sicnu::suitability::suitabilityLevelToString;
using sicnu::suitability::teachingExplanation;

namespace
{

SceneCandidate sceneWith( const QString &id, const QStringList &bandRoles )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    scene.crsWkt = QStringLiteral( "scene-crs" );
    scene.extent = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    scene.acquisitionTimeUtc =
        QDateTime::fromString( QStringLiteral( "2024-07-15T00:00:00Z" ), Qt::ISODate );
    scene.bandRoles = bandRoles;
    return scene;
}

/// A classification-shaped assessment with a real gap: scenes carry only
/// red/green/blue while the goal demands NIR too.
SuitabilityReport classificationReport()
{
    SuitabilityGoal goal;
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    goal.aoiCrsWkt = QStringLiteral( "scene-crs" );
    goal.hasTimeWindow = true;
    goal.windowStartUtc =
        QDateTime::fromString( QStringLiteral( "2024-01-01T00:00:00Z" ), Qt::ISODate );
    goal.windowEndUtc =
        QDateTime::fromString( QStringLiteral( "2025-01-01T00:00:00Z" ), Qt::ISODate );
    goal.requiredBandRoles = { QStringLiteral( "nir" ), QStringLiteral( "red" ) };

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.scenes = { sceneWith( QStringLiteral( "rgb-1" ),
                                 { QStringLiteral( "red" ), QStringLiteral( "green" ),
                                   QStringLiteral( "blue" ) } ) };
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    return result.value();
}

/// A phenology-shaped assessment: full-year requirement, no winter anywhere.
SuitabilityReport phenologyReport()
{
    SuitabilityGoal goal;
    goal.taskFamily = BenchmarkTaskFamily::TemporalPrediction;
    goal.profileKey = QStringLiteral( "phenology" );
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    goal.aoiCrsWkt = QStringLiteral( "scene-crs" );

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    for ( const char *time : { "2024-04-01T00:00:00Z", "2024-07-01T00:00:00Z",
                               "2024-10-01T00:00:00Z" } )
    {
        SceneCandidate scene = sceneWith( QString::fromLatin1( time ), {} );
        scene.acquisitionTimeUtc = QDateTime::fromString( QString::fromLatin1( time ), Qt::ISODate );
        inputs.scenes.append( scene );
    }
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    return result.value();
}

const SuitabilityCriterion *findCriterion( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityCriterion &criterion : report.criteria() )
        if ( criterion.id == id )
            return &criterion;
    return nullptr;
}

} // namespace

TEST_CASE( "every criterion paragraph carries the level string and all gap ids",
           "[suitability][teaching]" )
{
    const SuitabilityReport report = classificationReport();
    const QStringList lines = teachingExplanation( report );

    // One overview line + one line per criterion, in canonical order.
    REQUIRE( lines.size() == report.criteria().size() + 1 );

    for ( int i = 0; i < report.criteria().size(); ++i )
    {
        const SuitabilityCriterion &criterion = report.criteria().at( i );
        const QString line = lines.at( i + 1 );
        // The paragraph teaches the same criterion, in report order, with the
        // exact level string the JSON carries.
        INFO( line.toStdString() );
        REQUIRE( line.contains( criterion.id ) );
        REQUIRE( line.contains( suitabilityLevelToString( criterion.level ) ) );
        // Unsuitable/Marginal paragraphs quote every gap id; Suitable
        // paragraphs have none to quote.
        if ( criterion.level == SuitabilityLevel::Unsuitable
             || criterion.level == SuitabilityLevel::Marginal )
        {
            for ( const auto &gap : criterion.gaps )
                REQUIRE( line.contains( gap.id ) );
        }
        if ( !criterion.applicable )
            REQUIRE( line.contains( QLatin1String( "not applicable" ) ) );
    }

    // The paragraph for spectral.bands explains WHY (missing NIR) with the
    // machine id — the "why is this unfit" answer a student can act on.
    const SuitabilityCriterion *spectral =
        findCriterion( report, QStringLiteral( "spectral.bands" ) );
    REQUIRE( spectral != nullptr );
    REQUIRE( spectral->level == SuitabilityLevel::Unsuitable );
    const int spectralIndex =
        static_cast< int >( std::find_if( report.criteria().cbegin(), report.criteria().cend(),
                                          []( const SuitabilityCriterion &c )
                                          { return c.id == QLatin1String( "spectral.bands" ); } )
                            - report.criteria().cbegin() );
    const QString spectralLine = lines.at( spectralIndex + 1 );
    REQUIRE( spectralLine.contains( QLatin1String( "band.missing.nir" ) ) );
    REQUIRE( !spectralLine.contains( QLatin1String( "band.missing.red" ) ) );

    // Per-criterion reuse: the standalone explainCriterion output IS the
    // paragraph teachingExplanation used.
    REQUIRE( explainCriterion( *spectral ) == spectralLine );
}

TEST_CASE( "the overview states the overall level word and the unsuitable narrative names every gap",
           "[suitability][teaching]" )
{
    const SuitabilityReport report = classificationReport();
    const QStringList lines = teachingExplanation( report );
    const QString overview = lines.first();

    REQUIRE( overview.startsWith( QLatin1String( "Overall: " ) ) );
    REQUIRE( overview.startsWith( QStringLiteral( "Overall: %1." )
                                      .arg( suitabilityLevelToString( report.overallLevel() ) ) ) );
    // The gap total is stated.
    REQUIRE( overview.contains( QStringLiteral( "%1 gap" ).arg( report.allGaps().size() ) ) );

    // An unsuitable report's narrative names every gap id somewhere.
    QString narrative;
    for ( const QString &line : lines )
        narrative += line + QLatin1Char( '\n' );
    for ( const auto &gap : report.allGaps() )
        REQUIRE( narrative.contains( gap.id ) );
}

TEST_CASE( "an unknown paragraph explains what could not be judged and why",
           "[suitability][teaching]" )
{
    // A report whose criteria are Unknown (no AOI, no window, no band
    // evidence): the paragraphs must say "cannot judge" and quote the notes.
    SuitabilityAssessor::Inputs inputs;
    inputs.goal.taskFamily = BenchmarkTaskFamily::SpectralMatching; // minimal requirements
    inputs.scenes.append( sceneWith( QStringLiteral( "bare" ), {} ) ); // no band roles at all

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityReport report = result.value();

    bool sawUnknownParagraph = false;
    const QStringList lines = teachingExplanation( report );
    for ( int i = 0; i < report.criteria().size(); ++i )
    {
        const SuitabilityCriterion &criterion = report.criteria().at( i );
        const QString line = lines.at( i + 1 );
        REQUIRE( line.contains( suitabilityLevelToString( criterion.level ) ) );
        if ( criterion.applicable && criterion.level == SuitabilityLevel::Unknown )
        {
            sawUnknownParagraph = true;
            REQUIRE( line.contains( QLatin1String( "cannot judge" ), Qt::CaseInsensitive ) );
            // The why lives in the notes (or the summary when no note exists).
            for ( const QString &note : criterion.notes )
                REQUIRE( line.contains( note ) );
        }
    }
    REQUIRE( sawUnknownParagraph );

    // No note, still unknown: the summary alone must carry the reason.
    SuitabilityCriterion bare;
    bare.id = QStringLiteral( "spatial.resolution" );
    bare.level = SuitabilityLevel::Unknown;
    bare.summary = QStringLiteral( "No scene carries a meter GSD; resolution is unmeasured." );
    const QString explained = explainCriterion( bare );
    REQUIRE( explained.contains( QLatin1String( "cannot judge" ), Qt::CaseInsensitive ) );
    REQUIRE( explained.contains( QLatin1String( "unmeasured" ) ) );
}

TEST_CASE( "an empty report and an all-not-applicable report teach without crashing",
           "[suitability][teaching]" )
{
    // Empty report: overall is Unknown (nothing applicable asserts nothing);
    // the narrative says so instead of inventing a verdict.
    const SuitabilityReport empty;
    const QStringList emptyLines = teachingExplanation( empty );
    REQUIRE( !emptyLines.isEmpty() );
    REQUIRE( emptyLines.first().startsWith( QLatin1String( "Overall: unknown." ) ) );

    // All not-applicable: every paragraph is a skip sentence; the overall
    // stays Unknown (an empty applicable set claims nothing).
    SuitabilityReport allNotApplicable;
    for ( const char *id : { "spatial.coverage", "spectral.bands", "model.compatibility" } )
    {
        SuitabilityCriterion criterion;
        criterion.id = QString::fromLatin1( id );
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "Not requested by this goal." );
        allNotApplicable.addCriterion( criterion );
    }
    const QStringList naLines = teachingExplanation( allNotApplicable );
    REQUIRE( naLines.size() == 4 );
    REQUIRE( naLines.first().startsWith( QLatin1String( "Overall: unknown." ) ) );
    for ( int i = 1; i < naLines.size(); ++i )
    {
        INFO( naLines.at( i ).toStdString() );
        REQUIRE( naLines.at( i ).contains( QLatin1String( "not applicable" ) ) );
        REQUIRE( naLines.at( i ).contains( QLatin1String( "unknown" ) ) );
    }
}

TEST_CASE( "the phenology narrative names the missing seasons and stays consistent",
           "[suitability][teaching]" )
{
    const SuitabilityReport report = phenologyReport();
    const SuitabilityCriterion *seasonality =
        findCriterion( report, QStringLiteral( "temporal.seasonality" ) );
    REQUIRE( seasonality != nullptr );
    REQUIRE( seasonality->level == SuitabilityLevel::Unsuitable );

    const QStringList lines = teachingExplanation( report );
    QString narrative;
    for ( const QString &line : lines )
        narrative += line + QLatin1Char( '\n' );
    REQUIRE( narrative.contains( QLatin1String( "season.missing.winter" ) ) );

    // Determinism: the same report teaches byte-identical lines.
    REQUIRE( teachingExplanation( report ) == lines );
    // And the overview agrees with the report's own aggregate.
    REQUIRE( lines.first()
                 .startsWith( QStringLiteral( "Overall: %1." )
                                  .arg( suitabilityLevelToString( report.overallLevel() ) ) ) );
}
