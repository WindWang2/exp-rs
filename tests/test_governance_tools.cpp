// test_governance_tools.cpp — Workspace Governance 3.0 agent surfaces:
// bounded, structured tool execution over a seeded WorkspaceService.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "agent/spatial_tools/governance_tools.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/workspace_state.h"
#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "data/governance/workspace_service.h"

using namespace sicnu::workspace;

namespace
{

Json::Value parseInput( const QString &json )
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream( json.toStdString() );
    Json::parseFromStream( builder, stream, &parsed, &errors );
    return parsed;
}

} // namespace

TEST_CASE( "Governance tools return bounded structured results", "[agent][governance_tools]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    WorkspaceService service;
    REQUIRE( service.openStore( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    // Seed: 60 assets across sensors, one result, one run, one lineage chain.
    QVector<GovernedAsset> assets;
    for ( int i = 0; i < 60; ++i )
    {
        GovernedAsset a;
        a.assetId = QStringLiteral( "g-%1" ).arg( i );
        a.canonicalSource = QStringLiteral( "/data/%1.tif" ).arg( i );
        a.kind = QStringLiteral( "raster" );
        a.state = QStringLiteral( "Ready" );
        a.displayName = QStringLiteral( "scene %1" ).arg( i );
        a.sensor = ( i % 2 == 0 ) ? QStringLiteral( "S2" ) : QStringLiteral( "L8" );
        a.modality = QStringLiteral( "optical" );
        assets.append( a );
    }
    REQUIRE( service.store().upsertAssets( assets ).operator bool() );

    RunRecord run;
    run.id = QStringLiteral( "run-g1" );
    run.workflowId = QStringLiteral( "wf-g" );
    run.state = QStringLiteral( "Completed" );
    REQUIRE( service.store().upsertRun( run ).operator bool() );

    ResultRecord result;
    result.id = ResultId::generate();
    result.semanticType = ResultSemanticType::Classification;
    result.header.name = QStringLiteral( "g-result" );
    result.producer = QJsonObject{ { QLatin1String( "runId" ), QLatin1String( "run-g1" ) } };
    REQUIRE( service.store().upsertResult( result ).operator bool() );

    sicnu::agent::spatial_tools::registerGovernanceTools();
    sicnu::agent::AgentServices::instance().setWorkspaceService( &service );
    auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();

    SECTION( "project:summary reports counts and facets" )
    {
        const auto toolopt = registry.find( "project:summary" );
        REQUIRE( toolopt.has_value() );
        auto *tool = toolopt->get();
        REQUIRE( tool != nullptr );
        const auto out = tool->execute( parseInput( QStringLiteral( "{}" ) ) );
        REQUIRE( out.success );
        REQUIRE( out.output["counts"]["assets"].asInt64() == 60 );
        REQUIRE( out.output["counts"]["results"].asInt64() == 1 );
        REQUIRE( out.output["facets"]["sensor"].isArray() );
    }

    SECTION( "project:search is paged and clamped" )
    {
        const auto toolopt = registry.find( "project:search" );
        REQUIRE( toolopt.has_value() );
        auto *tool = toolopt->get();
        REQUIRE( tool != nullptr );
        Json::Value input = parseInput( QStringLiteral( "{\"set\":\"assets\",\"limit\":10000}" ) );
        const auto out = tool->execute( input );
        REQUIRE( out.success );
        // limit is clamped to the tool bound (100), never the requested 10000.
        REQUIRE( out.output["items"].size() <= 100 );
        REQUIRE( out.output["total"].asInt64() == 60 );

        input = parseInput( QStringLiteral( "{\"set\":\"assets\",\"sensor\":\"S2\",\"limit\":10}" ) );
        const auto filtered = tool->execute( input );
        REQUIRE( filtered.success );
        REQUIRE( filtered.output["total"].asInt64() == 30 );
        REQUIRE( filtered.output["items"].size() == 10 );
    }

    SECTION( "asset:inspect returns the governed record" )
    {
        const auto toolopt = registry.find( "asset:inspect" );
        REQUIRE( toolopt.has_value() );
        auto *tool = toolopt->get();
        REQUIRE( tool != nullptr );
        const auto missing = tool->execute( parseInput( QStringLiteral( "{\"assetId\":\"nope\"}" ) ) );
        REQUIRE_FALSE( missing.success );
        REQUIRE( missing.errorCode == "NOT_FOUND" );

        const auto out = tool->execute(
            parseInput( QStringLiteral( "{\"assetId\":\"g-1\"}" ) ) );
        REQUIRE( out.success );
        REQUIRE( out.output["assetId"].asString() == "g-1" );
        REQUIRE( out.output["sensor"].asString() == "L8" );
    }

    SECTION( "result:inspect exposes lifecycle and producer" )
    {
        const auto toolopt = registry.find( "result:inspect" );
        REQUIRE( toolopt.has_value() );
        auto *tool = toolopt->get();
        REQUIRE( tool != nullptr );
        const auto out = tool->execute(
            parseInput( QStringLiteral( "{\"resultId\":\"%1\"}" ).arg( result.id.toString() ) ) );
        REQUIRE( out.success );
        REQUIRE( out.output["status"].asString() == "draft" );
        REQUIRE( out.output["producer"]["runId"].asString() == "run-g1" );
    }

    SECTION( "run:compare validates both ids" )
    {
        const auto toolopt = registry.find( "run:compare" );
        REQUIRE( toolopt.has_value() );
        auto *tool = toolopt->get();
        REQUIRE( tool != nullptr );
        const auto bad = tool->execute(
            parseInput( QStringLiteral( "{\"runIdA\":\"run-g1\",\"runIdB\":\"run-404\"}" ) ) );
        REQUIRE_FALSE( bad.success );
        REQUIRE( bad.errorCode == "NOT_FOUND" );

        const auto out = tool->execute(
            parseInput( QStringLiteral( "{\"runIdA\":\"run-g1\",\"runIdB\":\"run-g1\"}" ) ) );
        REQUIRE( out.success );
        REQUIRE( out.output["runA"]["runId"].asString() == "run-g1" );
    }

    SECTION( "lineage tools bound depth and report counts" )
    {
        const auto upopt = registry.find( "lineage:upstream" );
        REQUIRE( upopt.has_value() );
        auto *up = upopt->get();
        const auto downopt = registry.find( "lineage:downstream" );
        REQUIRE( downopt.has_value() );
        auto *down = downopt->get();
        REQUIRE( up != nullptr );
        REQUIRE( down != nullptr );
        // Chain g0 -> g1 -> g2 -> g3 (each output derives from previous).
        QVector<GovernanceStore::LineageEdge> edges;
        for ( int i = 1; i < 4; ++i )
            edges.append( GovernanceStore::LineageEdge{ QStringLiteral( "g-%1" ).arg( i ),
                                                        QStringLiteral( "g-%1" ).arg( i - 1 ),
                                                        1, QStringLiteral( "rs:x" ), QString(),
                                                        QString(), QString() } );
        REQUIRE( service.store().addLineageEdges( edges ).operator bool() );
        const auto out = up->execute( parseInput( QStringLiteral( "{\"assetId\":\"g-3\"}" ) ) );
        REQUIRE( out.success );
        REQUIRE( out.output["count"].asInt64() == 3 );
        const auto downOut = down->execute( parseInput( QStringLiteral( "{\"assetId\":\"g-0\"}" ) ) );
        REQUIRE( downOut.success );
        REQUIRE( downOut.output["count"].asInt64() == 3 );
    }

    sicnu::agent::AgentServices::instance().setWorkspaceService( nullptr );
}
