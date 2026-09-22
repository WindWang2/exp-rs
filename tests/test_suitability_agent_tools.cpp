// test_suitability_agent_tools.cpp — RS14 Slice H3: the suitability agent
// adapter (the machine-readable assess/profiles entry points that the
// data-platform tool surface dispatches to), exercised directly.
//
// The adapter lives inside sicnu_suitability precisely so this suite needs no
// qgis/agent link. The agent-side shell (defs table row, prefix check, one-line
// dispatch in handleDataPlatformTool) is covered by inspection plus the
// existing test_surface_parity suite; capability-drift files are deliberately
// untouched (#1151 avoidance zone).
//
// Two failure layers are locked here:
//   - a structurally broken REQUEST (missing goal, unparsable JSON document,
//     unknown profile key, dataset_version_id without dataset_db) throws
//     std::runtime_error;
//   - a well-formed request with invalid CONTENT (or an assessment that
//     cannot run) is a completed call with valid=false + diagnostics.
#include <catch2/catch_test_macros.hpp>

#include "suitability/suitability_agent_adapter.h"
#include "suitability/suitability_level.h"
#include "suitability/suitability_report.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

using sicnu::suitability::agent_adapter::suitabilityAssess;
using sicnu::suitability::agent_adapter::suitabilityProfiles;
using sicnu::suitability::SuitabilityReport;

namespace
{

QString goalText( const QJsonObject &goal )
{
    return QString::fromUtf8( QJsonDocument( goal ).toJson( QJsonDocument::Compact ) );
}

QString scenesText( const QJsonArray &scenes )
{
    return QString::fromUtf8( QJsonDocument( scenes ).toJson( QJsonDocument::Compact ) );
}

QJsonObject sceneJson( const QString &id )
{
    QJsonObject extent;
    extent.insert( QStringLiteral( "min_x" ), 0.0 );
    extent.insert( QStringLiteral( "min_y" ), 0.0 );
    extent.insert( QStringLiteral( "max_x" ), 1.0 );
    extent.insert( QStringLiteral( "max_y" ), 1.0 );
    extent.insert( QStringLiteral( "valid" ), true );

    QJsonObject scene;
    scene.insert( QStringLiteral( "schema_version" ), 1 );
    scene.insert( QStringLiteral( "id" ), id );
    scene.insert( QStringLiteral( "state" ), QStringLiteral( "Ready" ) );
    scene.insert( QStringLiteral( "extent" ), extent );
    scene.insert( QStringLiteral( "gsd_m" ), 10.0 );
    scene.insert( QStringLiteral( "acquisition_time_utc" ),
                  QStringLiteral( "2026-05-10T10:00:00Z" ) );
    scene.insert( QStringLiteral( "cloud_cover_percent" ), 5.0 );
    QJsonArray roles;
    roles.append( QStringLiteral( "nir" ) );
    scene.insert( QStringLiteral( "band_roles" ), roles );
    return scene;
}

QJsonArray twoScenes()
{
    QJsonArray scenes;
    scenes.append( sceneJson( QStringLiteral( "scene-a" ) ) );
    scenes.append( sceneJson( QStringLiteral( "scene-b" ) ) );
    return scenes;
}

} // namespace

TEST_CASE( "suitability:profiles lists the builtin table", "[suitability][agenttools]" )
{
    SECTION( "all profiles" )
    {
        const auto result = suitabilityProfiles( {} );
        const auto profiles = result.value( QStringLiteral( "profiles" ) ).toList();
        REQUIRE( result.value( QStringLiteral( "count" ) ).toInt() == 8 );
        REQUIRE( profiles.size() == 8 );
        bool sawPhenology = false;
        for ( const auto &rowValue : profiles )
        {
            const auto row = rowValue.toMap();
            if ( row.value( QStringLiteral( "key" ) ).toString() == QLatin1String( "phenology" ) )
                sawPhenology = true;
        }
        CHECK( sawPhenology );
    }

    SECTION( "single profile by key" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "profile" ), QStringLiteral( "phenology" ) );
        const auto result = suitabilityProfiles( args );
        const auto profiles = result.value( QStringLiteral( "profiles" ) ).toList();
        REQUIRE( profiles.size() == 1 );
        const auto row = profiles.first().toMap();
        CHECK( row.value( QStringLiteral( "key" ) ).toString() == QLatin1String( "phenology" ) );
        CHECK( row.value( QStringLiteral( "min_scenes_in_window" ) ).toInt() == 6 );
        const auto seasons = row.value( QStringLiteral( "required_seasons" ) ).toList();
        REQUIRE( seasons.size() == 4 );
    }

    SECTION( "unknown profile key is a tool error" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "profile" ), QStringLiteral( "nope" ) );
        REQUIRE_THROWS_AS( suitabilityProfiles( args ), std::runtime_error );
    }
}

TEST_CASE( "suitability:assess happy path returns a verifiable report",
           "[suitability][agenttools]" )
{
    QVariantMap args;
    args.insert( QStringLiteral( "goal" ), goalText( QJsonObject{ { "schema_version", 1 } } ) );
    args.insert( QStringLiteral( "scenes" ), scenesText( twoScenes() ) );

    const auto result = suitabilityAssess( args );

    REQUIRE( result.value( QStringLiteral( "valid" ) ).toBool() );
    const QString level = result.value( QStringLiteral( "overall_level" ) ).toString();
    const auto parsedLevel = sicnu::suitability::suitabilityLevelFromString( level );
    REQUIRE( parsedLevel.has_value() );

    // "Callable and verifiable": the report JSON round-trips and its digest
    // matches the digest the adapter reported alongside it.
    const auto reportJson = result.value( QStringLiteral( "report" ) ).toJsonObject();
    REQUIRE( !reportJson.isEmpty() );
    const auto reparsed = SuitabilityReport::fromJson( reportJson );
    REQUIRE( reparsed.has_value() );
    CHECK( reparsed.value().contentDigest() ==
           result.value( QStringLiteral( "report_digest" ) ).toString() );
    CHECK( reparsed.value().overallLevel() == *parsedLevel );

    CHECK( result.contains( QStringLiteral( "gaps" ) ) );
    const auto teaching = result.value( QStringLiteral( "teaching" ) ).toList();
    CHECK( !teaching.isEmpty() );
}

TEST_CASE( "suitability:assess goal content failure is soft", "[suitability][agenttools]" )
{
    // Well-formed JSON, invalid CONTENT (minGsdM > maxGsdM).
    QJsonObject goal;
    goal.insert( QStringLiteral( "schema_version" ), 1 );
    goal.insert( QStringLiteral( "min_gsd_m" ), 30.0 );
    goal.insert( QStringLiteral( "max_gsd_m" ), 10.0 );

    QVariantMap args;
    args.insert( QStringLiteral( "goal" ), goalText( goal ) );
    args.insert( QStringLiteral( "scenes" ), scenesText( twoScenes() ) );

    const auto result = suitabilityAssess( args );
    CHECK( !result.value( QStringLiteral( "valid" ) ).toBool() );
    CHECK( !result.value( QStringLiteral( "diagnostics" ) ).toList().isEmpty() );
}

TEST_CASE( "suitability:assess malformed request is a tool error",
           "[suitability][agenttools]" )
{
    SECTION( "missing goal" )
    {
        REQUIRE_THROWS_AS( suitabilityAssess( {} ), std::runtime_error );
    }
    SECTION( "syntactically broken goal document" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "goal" ), QStringLiteral( "{oops" ) );
        REQUIRE_THROWS_AS( suitabilityAssess( args ), std::runtime_error );
    }
    SECTION( "scenes is not an array document" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "goal" ), goalText( QJsonObject{ { "schema_version", 1 } } ) );
        args.insert( QStringLiteral( "scenes" ), QStringLiteral( "{\"id\":1}" ) );
        REQUIRE_THROWS_AS( suitabilityAssess( args ), std::runtime_error );
    }
    SECTION( "invalid scene candidate content is a tool error" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "goal" ), goalText( QJsonObject{ { "schema_version", 1 } } ) );
        args.insert( QStringLiteral( "scenes" ),
                     scenesText( QJsonArray{ QJsonObject{ { "schema_version", 1 } } } ) );
        REQUIRE_THROWS_AS( suitabilityAssess( args ), std::runtime_error );
    }
    SECTION( "a named dataset without a store is refused" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "goal" ), goalText( QJsonObject{ { "schema_version", 1 } } ) );
        args.insert( QStringLiteral( "dataset_version_id" ), QStringLiteral( "v-123" ) );
        REQUIRE_THROWS_AS( suitabilityAssess( args ), std::runtime_error );
    }
}

TEST_CASE( "suitability:assess reports an unrunnable assessment softly",
           "[suitability][agenttools]" )
{
    // A well-formed goal and well-formed but empty scene array: the subject
    // names nothing assessable. The typed empty-subject failure is the
    // completed answer, not an exception.
    QVariantMap args;
    args.insert( QStringLiteral( "goal" ), goalText( QJsonObject{ { "schema_version", 1 } } ) );
    args.insert( QStringLiteral( "scenes" ), scenesText( {} ) );

    const auto result = suitabilityAssess( args );
    CHECK( !result.value( QStringLiteral( "valid" ) ).toBool() );
    const auto diagnostics = result.value( QStringLiteral( "diagnostics" ) ).toList();
    REQUIRE( !diagnostics.isEmpty() );
    bool sawEmptySubject = false;
    for ( const auto &diagnosticValue : diagnostics )
    {
        if ( diagnosticValue.toMap().value( QStringLiteral( "code" ) ).toString() ==
             QLatin1String( "suitability.empty_subject" ) )
            sawEmptySubject = true;
    }
    CHECK( sawEmptySubject );
}
