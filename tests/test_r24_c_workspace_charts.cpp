// tests/test_r24_c_workspace_charts.cpp
// R24-C WP1 (#L1): the workspace-state contract must project the live
// ChartRegistry (the same registry the cartography:chart_* tools mutate)
// into the cross-conversation workspace document. Before this fix the
// "charts" array was a hardcoded empty Json::Value even with charts
// present (src/agent/workspace_state.cpp "Phase L registry; empty until
// charts are created" — the registry existed but was never consulted).
//
// Contract mirrored from the layouts projection: entity id (stable across
// captures via WorkspaceEntityRegistry), source id, kind, title; bounded at
// 25 entries per chart_registry.h ("workspace summary caps at 25").
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/cartography/chart_registry.h"
#include "agent/workspace_state.h"

#include <qgsapplication.h>
#include <qgsproject.h>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  sicnu::agent::WorkspaceEntityRegistry::instance().clearInProcess();
  const int result = Catch::Session().run( argc, argv );
  sicnu::agent::cartography::ChartRegistry::instance().clear();
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

namespace
{

Json::Value makeInlineBarChart( const std::string &title )
{
  Json::Value spec( Json::objectValue );
  spec["kind"] = "bar";
  spec["title"] = title;
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  Json::Value data( Json::arrayValue );
  Json::Value row( Json::objectValue );
  row["label"] = "class A";
  row["value"] = 12.5;
  data.append( row );
  row["label"] = "class B";
  row["value"] = 7.25;
  data.append( row );
  binding["data"] = data;
  spec["binding"] = binding;
  return spec;
}

} // namespace

TEST_CASE( "workspace state projects ChartRegistry charts with stable entity ids",
           "[agent][workspace_state][r24_c]" )
{
  QgsProject::instance()->clear();
  sicnu::agent::WorkspaceEntityRegistry::instance().clearInProcess();
  auto &charts = sicnu::agent::cartography::ChartRegistry::instance();
  charts.clear();

  QString error;
  const QString idA = charts.createChart( makeInlineBarChart( "NDVI mean per class" ), &error );
  REQUIRE( idA == QStringLiteral( "chart-1" ) );
  const QString idB = charts.createChart( makeInlineBarChart( "Change matrix summary" ), &error );
  REQUIRE( idB == QStringLiteral( "chart-2" ) );

  const Json::Value doc = sicnu::agent::buildWorkspaceState( nullptr, nullptr );
  REQUIRE( doc["kind"].asString() == "workspace_state" );
  REQUIRE( doc["charts"].isArray() );
  REQUIRE( doc["charts"].size() == 2 );

  const Json::Value &first = doc["charts"][0];
  CHECK( first["id"].asString() == "chart-1" );
  CHECK( first["chart_id"].asString() == idA.toStdString() );
  CHECK( first["kind"].asString() == "bar" );
  CHECK( first["title"].asString() == "NDVI mean per class" );

  const Json::Value &second = doc["charts"][1];
  CHECK( second["chart_id"].asString() == idB.toStdString() );

  // Stability: a second capture resolves identical entity ids (the registry
  // natural keys are stable, so entity ids must not be reallocated).
  const Json::Value doc2 = sicnu::agent::buildWorkspaceState( nullptr, nullptr );
  REQUIRE( doc2["charts"].size() == 2 );
  CHECK( doc2["charts"][0]["id"].asString() == first["id"].asString() );
  CHECK( doc2["charts"][1]["id"].asString() == second["id"].asString() );

  // Empty-registry negative control: clearing returns the honest empty
  // projection, not a stale cached list.
  charts.clear();
  const Json::Value doc3 = sicnu::agent::buildWorkspaceState( nullptr, nullptr );
  CHECK( doc3["charts"].size() == 0 );
}

TEST_CASE( "workspace state charts projection stays bounded and ignores invalid specs",
           "[agent][workspace_state][r24_c]" )
{
  QgsProject::instance()->clear();
  sicnu::agent::WorkspaceEntityRegistry::instance().clearInProcess();
  auto &charts = sicnu::agent::cartography::ChartRegistry::instance();
  charts.clear();

  // Invalid spec (unknown kind) must never enter the registry, hence never
  // the workspace document — the projection trusts the registry's own
  // validation, it does not re-lift arbitrary caller JSON.
  Json::Value bad = makeInlineBarChart( "invalid" );
  bad["kind"] = "hologram";
  QString error;
  CHECK( charts.createChart( bad, &error ).isEmpty() );

  for ( int i = 0; i < 30; ++i )
  {
    const QString id = charts.createChart(
      makeInlineBarChart( "chart " + std::to_string( i ) ), &error );
    REQUIRE( !id.isEmpty() );
  }
  REQUIRE( charts.listCharts().size() == 30 );

  const Json::Value doc = sicnu::agent::buildWorkspaceState( nullptr, nullptr );
  CHECK( doc["charts"].size() ==
         sicnu::agent::cartography::kMaxWorkspaceSummaryCharts );

  charts.clear();
}
