// tests/test_platform6.cpp
//
// Knowledge Platform 6.0 — Milestone A declarative-correctness regressions:
//   #781 single-item fit_content, #782 rule-based renderer hierarchy,
//   #784 branch-local recipe gate degradation, #802 external condition
//   context, #804 full-AST condition validation, #815 multi-hop tokens.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/composition.h"
#include "agent/cartography/design_tokens.h"
#include "agent/cartography/style_compiler.h"
#include "agent/cartography/style_spec.h"
#include "agent/harness/recipe_catalog.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_conditions.h"

#include <qgsrulebasedrenderer.h>
#include <qgsvectorlayer.h>

#include <qgslayoutitem.h>
#include <qgslayoutitemregistry.h>
#include <qgslayoutmanager.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include "agent/mapspec/mapspec_compiler.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <map>
#include <set>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;
using sicnu::agent::harness::HarnessError;
using sicnu::agent::harness::RecipeCatalog;

namespace {

Json::Value makeSpec()
{
  Json::Value spec = makeMapSpec( "platform6", Json::Value() );
  return spec;
}

Json::Value rectItem( const std::string &id, double x, double y, double w, double h )
{
  Json::Value item( Json::objectValue );
  item["id"] = id;
  Json::Value rect( Json::arrayValue );
  rect.append( x );
  rect.append( y );
  rect.append( w );
  rect.append( h );
  item["rect_mm"] = rect;
  return item;
}

Json::Value pairConstraint( const std::string &id, const std::string &kind,
                            const std::string &a, const std::string &b )
{
  Json::Value constraint( Json::objectValue );
  constraint["id"] = id;
  constraint["kind"] = kind;
  Json::Value items( Json::arrayValue );
  items.append( a );
  if ( !b.empty() )
    items.append( b );
  constraint["items"] = items;
  return constraint;
}

bool writeTextFile( const QString &path, const std::string &contents )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  file.write( contents.data(), static_cast<qint64>( contents.size() ) );
  return true;
}

bool layoutHasItem( QgsPrintLayout *layout, const std::string &id )
{
  const QList<QGraphicsItem *> items = layout->items();
  for ( QGraphicsItem *sceneItem : items )
  {
    auto *item = dynamic_cast<QgsLayoutItem *>( sceneItem );
    if ( item && item->id() == QString::fromStdString( id ) )
      return true;
  }
  return false;
}

} // namespace

// ---------------------------------------------------------------------------
// #781 — constraint solver discards single-item fit_content
// ---------------------------------------------------------------------------

TEST_CASE( "fit_content resizes a single item to its declared content (#781)",
           "[platform6][composition]" )
{
  Json::Value spec = makeSpec();
  spec["titles"].append( rectItem( "t1", 20, 20, 100, 10 ) );
  Json::Value fit = pairConstraint( "fit-1", "fit_content", "t1", std::string() );
  Json::Value content( Json::arrayValue );
  content.append( 50 );
  content.append( 12 );
  fit["content_mm"] = content;
  spec["constraints"].append( fit );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  CHECK( result.constraintsSolved == 1 );
  CHECK( spec["titles"][0]["rect_mm"][2].asDouble() == Catch::Approx( 50.0 ) );
  CHECK( spec["titles"][0]["rect_mm"][3].asDouble() == Catch::Approx( 12.0 ) );
  // Origin is preserved.
  CHECK( spec["titles"][0]["rect_mm"][0].asDouble() == Catch::Approx( 20.0 ) );
}

TEST_CASE( "fit_content clamps against the item's min/max size bounds (#781)",
           "[platform6][composition]" )
{
  Json::Value spec = makeSpec();
  Json::Value item = rectItem( "t1", 20, 20, 100, 10 );
  Json::Value minSize( Json::arrayValue );
  minSize.append( 60 );
  minSize.append( 8 );
  item["min_size_mm"] = minSize;
  Json::Value maxSize( Json::arrayValue );
  maxSize.append( 80 );
  maxSize.append( 40 );
  item["max_size_mm"] = maxSize;
  spec["titles"].append( item );
  Json::Value fit = pairConstraint( "fit-1", "fit_content", "t1", std::string() );
  Json::Value content( Json::arrayValue );
  content.append( 50 );
  content.append( 12 );
  fit["content_mm"] = content;
  spec["constraints"].append( fit );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  CHECK( result.constraintsSolved == 1 );
  // 50 -> min 60; 12 within [8, 40].
  CHECK( spec["titles"][0]["rect_mm"][2].asDouble() == Catch::Approx( 60.0 ) );
  CHECK( spec["titles"][0]["rect_mm"][3].asDouble() == Catch::Approx( 12.0 ) );
}

// ---------------------------------------------------------------------------
// #805 — bounded constraint-graph solver (order-independent, diagnostic)
// ---------------------------------------------------------------------------

TEST_CASE( "Constraint outcome is independent of declaration order (#805)",
           "[platform6][composition]" )
{
  auto buildSpec = []() {
    Json::Value spec = makeSpec();
    spec["titles"].append( rectItem( "a", 20, 20, 60, 10 ) );
    spec["titles"].append( rectItem( "b", 200, 200, 60, 8 ) );
    spec["titles"].append( rectItem( "c", 40, 120, 60, 8 ) );
    Json::Value stack( Json::objectValue );
    stack["id"] = "s1";
    stack["kind"] = "stack";
    stack["direction"] = "below";
    stack["gap_mm"] = 2;
    Json::Value chain( Json::arrayValue );
    chain.append( "a" );
    chain.append( "b" );
    chain.append( "c" );
    stack["items"] = chain;
    spec["constraints"].append( stack );
    Json::Value align( Json::objectValue );
    align["id"] = "al1";
    align["kind"] = "align";
    align["edge"] = "left";
    Json::Value pair( Json::arrayValue );
    pair.append( "a" );
    pair.append( "c" );
    align["items"] = pair;
    spec["constraints"].append( align );
    return spec;
  };

  Json::Value first = buildSpec();
  const CompositionResult firstResult = resolveComposition( first, 10.0 );
  CHECK( firstResult.converged );
  CHECK( firstResult.constraintsSolved == 2 );
  CHECK( firstResult.passes >= 1 );
  CHECK( firstResult.passes <= 24 );

  Json::Value second = buildSpec();
  // Reorder: the align constraint is declared BEFORE the stack.
  const Json::Value alignCopy = second["constraints"][1];
  second["constraints"][1] = second["constraints"][0];
  second["constraints"][0] = alignCopy;
  const CompositionResult secondResult = resolveComposition( second, 10.0 );
  CHECK( secondResult.converged );

  // Consistent systems converge to the same geometry regardless of the
  // order the constraints were declared in.
  CHECK( second["titles"][1]["rect_mm"].toStyledString() ==
         first["titles"][1]["rect_mm"].toStyledString() );
  CHECK( second["titles"][2]["rect_mm"].toStyledString() ==
         first["titles"][2]["rect_mm"].toStyledString() );
  CHECK( second["titles"][1]["rect_mm"][1].asDouble() == Catch::Approx( 32.0 ) );
  CHECK( second["titles"][2]["rect_mm"][1].asDouble() == Catch::Approx( 42.0 ) );
  CHECK( second["titles"][2]["rect_mm"][0].asDouble() == Catch::Approx( 20.0 ) );
}

TEST_CASE( "Contradictory constraints are diagnosed, not silently absorbed (#805)",
           "[platform6][composition]" )
{
  Json::Value spec = makeSpec();
  spec["titles"].append( rectItem( "a", 20, 20, 60, 10 ) );
  spec["titles"].append( rectItem( "b", 100, 100, 60, 10 ) );
  Json::Value ab( Json::objectValue );
  ab["id"] = "c-ab";
  ab["kind"] = "below";
  Json::Value abItems( Json::arrayValue );
  abItems.append( "b" );
  abItems.append( "a" ); // a below b
  ab["items"] = abItems;
  ab["gap_mm"] = 2;
  spec["constraints"].append( ab );
  Json::Value ba = ab;
  ba["id"] = "c-ba";
  Json::Value baItems( Json::arrayValue );
  baItems.append( "a" );
  baItems.append( "b" ); // b below a — contradiction
  ba["items"] = baItems;
  spec["constraints"].append( ba );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  // Bounded: the relaxation stops and reports instead of looping forever.
  CHECK_FALSE( result.converged );
  CHECK( result.passes == 24 );
  bool sawCycle = false;
  bool sawNonConvergence = false;
  for ( const auto &note : result.unsatisfied )
  {
    sawCycle = sawCycle || note.find( "cyclic constraint dependency" ) != std::string::npos;
    sawNonConvergence =
      sawNonConvergence || note.find( "did not converge" ) != std::string::npos;
  }
  CHECK( sawCycle );
  CHECK( sawNonConvergence );
}

TEST_CASE( "Unresolvable and malformed constraints are reported individually (#805)",
           "[platform6][composition]" )
{
  Json::Value spec = makeSpec();
  spec["titles"].append( rectItem( "a", 20, 20, 60, 10 ) );
  Json::Value ghost = pairConstraint( "c-ghost", "below", "a", "missing-item" );
  ghost["gap_mm"] = 1;
  spec["constraints"].append( ghost );
  Json::Value fit = pairConstraint( "c-fit", "fit_content", "a", std::string() );
  spec["constraints"].append( fit ); // missing content_mm

  const CompositionResult result = resolveComposition( spec, 10.0 );
  CHECK( result.constraintsTotal == 2 );
  CHECK( result.constraintsSolved == 0 );
  REQUIRE_FALSE( result.unsatisfied.empty() );
  bool sawMissing = false;
  bool sawContent = false;
  for ( const auto &note : result.unsatisfied )
  {
    sawMissing = sawMissing || note.find( "fewer than 2 resolvable items" ) != std::string::npos;
    sawContent = sawContent || note.find( "fit_content needs content_mm" ) != std::string::npos;
  }
  CHECK( sawMissing );
  CHECK( sawContent );
}

// ---------------------------------------------------------------------------
// #802 — condition pruning requires redundant condition_context
// ---------------------------------------------------------------------------

TEST_CASE( "Conditional pruning evaluates against an external context (#802)",
           "[platform6][mapspec]" )
{
  Json::Value spec = makeSpec();
  Json::Value title = rectItem( "title-1", 12, 12, 100, 10 );
  title["text"] = "optional";
  title["visible_if"] = "statistics.ready";
  spec["titles"].append( title );

  // No condition_context member at all: the runtime context alone decides.
  Json::Value ctx( Json::objectValue );
  ctx["statistics"] = Json::Value( Json::objectValue );
  ctx["statistics"]["ready"] = false;
  std::vector<std::string> errors;
  const Json::Value ledger = resolveMapSpecConditions( spec, ctx, &errors );

  CHECK( errors.empty() );
  CHECK( findMapSpecItem( spec, "title-1" ).isNull() );
  REQUIRE( ledger.isArray() );
  REQUIRE( ledger.size() == 1 );
  CHECK( ledger[0]["outcome"].asString() == "hidden" );

  // A true external context keeps the item.
  Json::Value spec2 = makeSpec();
  Json::Value title2 = rectItem( "title-1", 12, 12, 100, 10 );
  title2["text"] = "optional";
  title2["visible_if"] = "statistics.ready";
  spec2["titles"].append( title2 );
  Json::Value ctx2( Json::objectValue );
  ctx2["statistics"] = Json::Value( Json::objectValue );
  ctx2["statistics"]["ready"] = true;
  resolveMapSpecConditions( spec2, ctx2, nullptr );
  CHECK_FALSE( findMapSpecItem( spec2, "title-1" ).isNull() );
}

TEST_CASE( "External context overrides the embedded condition_context (#802)",
           "[platform6][mapspec]" )
{
  Json::Value spec = makeSpec();
  Json::Value title = rectItem( "title-1", 12, 12, 100, 10 );
  title["text"] = "optional";
  title["visible_if"] = "flag";
  spec["titles"].append( title );
  Json::Value embedded( Json::objectValue );
  embedded["flag"] = true;
  embedded["only_embedded"] = 42;
  spec["condition_context"] = embedded;

  Json::Value external( Json::objectValue );
  external["flag"] = false; // runtime value wins over the stale embedded one

  std::vector<std::string> errors;
  resolveMapSpecConditions( spec, external, &errors );
  CHECK( errors.empty() );
  CHECK( findMapSpecItem( spec, "title-1" ).isNull() );
  CHECK_FALSE( spec.isMember( "condition_context" ) );
}

// ---------------------------------------------------------------------------
// #804 — short-circuit evaluation hides condition AST errors
// ---------------------------------------------------------------------------

TEST_CASE( "Condition errors surface only when they can change the outcome (#804)",
           "[platform6][conditions]" )
{
  Json::Value empty( Json::objectValue );
  std::string error;
  bool value = true;
  // The left operand cleanly decides: the right operand's unknown path can
  // not change the outcome, so evaluation succeeds (presence-guard idiom
  // "has(x) or x.status == ..." keeps working).
  error.clear();
  CHECK( evaluateCondition( "false and water.depth == 1", empty, &value, &error ) );
  CHECK_FALSE( value );
  CHECK( error.empty() );
  error.clear();
  CHECK( evaluateCondition( "true or water.depth == 1", empty, &value, &error ) );
  CHECK( value );
  CHECK( error.empty() );

  // The DECIDING operand carries an unknown path — the error must surface.
  Json::Value ctx( Json::objectValue );
  ctx["x"] = 5;
  error.clear();
  CHECK_FALSE( evaluateCondition( "water.depth == 1 and x == 5", ctx, &value, &error ) );
  CHECK( error.find( "water.depth" ) != std::string::npos );
  error.clear();
  CHECK_FALSE( evaluateCondition( "water.depth == 1 or true", ctx, &value, &error ) );
  CHECK( error.find( "water.depth" ) != std::string::npos );

  // Valid expressions keep their result semantics.
  error.clear();
  CHECK( evaluateCondition( "x == 5 and x < 10", ctx, &value, &error ) );
  CHECK( value );
  CHECK( evaluateCondition( "x == 4 or x < 10", ctx, &value, &error ) );
  CHECK( value );
  CHECK( error.empty() );
}

TEST_CASE( "Bare-literal conditions are rejected at validation time (#804)",
           "[platform6][conditions]" )
{
  std::vector<std::string> problems;
  CHECK_FALSE( validateConditionSyntax( "5", &problems ) );
  REQUIRE_FALSE( problems.empty() );
  CHECK( problems.front().find( "bare literal" ) != std::string::npos );

  problems.clear();
  CHECK_FALSE( validateConditionSyntax( "\"just text\"", &problems ) );
  CHECK_FALSE( problems.empty() );

  // Boolean literals stay legal degenerate conditions; comparisons and has()
  // remain valid.
  CHECK( validateConditionSyntax( "true", nullptr ) );
  CHECK( validateConditionSyntax( "has(water.extent)", nullptr ) );
  CHECK( validateConditionSyntax( "cloud_cover <= 20", nullptr ) );
}

// ---------------------------------------------------------------------------
// #782 — QGIS rule-based renderer hierarchy
// ---------------------------------------------------------------------------

TEST_CASE( "Rule-based renderer compiles declared rules as siblings (#782)",
           "[platform6][style]" )
{
  QgsVectorLayer layer( QStringLiteral( "Polygon?crs=EPSG:4326" ), QStringLiteral( "t" ),
                        QStringLiteral( "memory" ) );

  Json::Value style( Json::objectValue );
  style["schema_version"] = "1.0";
  style["kind"] = "style_spec";
  style["id"] = "style.test.rules";
  style["version"] = 1;
  style["applies_to"] = "vector";
  style["vector"]["renderertype"] = "rule_based";
  Json::Value rules( Json::arrayValue );
  Json::Value r1( Json::objectValue );
  r1["expression"] = "$area > 100";
  r1["color"] = "#ff0000";
  r1["label"] = "big";
  rules.append( r1 );
  Json::Value r2( Json::objectValue );
  r2["expression"] = "$area <= 100";
  r2["color"] = "#0000ff";
  r2["label"] = "small";
  rules.append( r2 );
  style["vector"]["rules"] = rules;

  QString error;
  QStringList problems;
  REQUIRE( applyStyleSpecToLayer( &layer, style, &error, &problems ) );
  CHECK( problems.isEmpty() );

  auto *renderer = dynamic_cast<QgsRuleBasedRenderer *>( layer.renderer() );
  REQUIRE( renderer != nullptr );
  QgsRuleBasedRenderer::Rule *root = renderer->rootRule();
  REQUIRE( root != nullptr );
  // The root is a symbol-less group; the declared rules are its children.
  CHECK( root->symbol() == nullptr );
  REQUIRE( root->children().size() == 2 );
  CHECK( root->children().at( 0 )->filterExpression() == QLatin1String( "$area > 100" ) );
  CHECK( root->children().at( 1 )->filterExpression() == QLatin1String( "$area <= 100" ) );
  CHECK( root->children().at( 0 )->children().isEmpty() );
}

TEST_CASE( "Rule-based renderer preserves declared sub-rule nesting (#782)",
           "[platform6][style]" )
{
  QgsVectorLayer layer( QStringLiteral( "Polygon?crs=EPSG:4326" ), QStringLiteral( "t2" ),
                        QStringLiteral( "memory" ) );

  Json::Value style( Json::objectValue );
  style["schema_version"] = "1.0";
  style["kind"] = "style_spec";
  style["id"] = "style.test.nested-rules";
  style["version"] = 1;
  style["applies_to"] = "vector";
  style["vector"]["renderertype"] = "rule_based";
  Json::Value rules( Json::arrayValue );
  Json::Value water( Json::objectValue );
  water["expression"] = "\"class\" = 'water'";
  water["color"] = "#a6cee3";
  Json::Value subRules( Json::arrayValue );
  Json::Value sub( Json::objectValue );
  sub["expression"] = "\"depth\" > 5";
  sub["color"] = "#2c7fb8";
  subRules.append( sub );
  water["rules"] = subRules;
  rules.append( water );
  style["vector"]["rules"] = rules;

  QString error;
  REQUIRE( applyStyleSpecToLayer( &layer, style, &error, nullptr ) );
  auto *renderer = dynamic_cast<QgsRuleBasedRenderer *>( layer.renderer() );
  REQUIRE( renderer != nullptr );
  QgsRuleBasedRenderer::Rule *root = renderer->rootRule();
  REQUIRE( root->children().size() == 1 );
  QgsRuleBasedRenderer::Rule *waterRule = root->children().at( 0 );
  CHECK( waterRule->filterExpression() == QLatin1String( "\"class\" = 'water'" ) );
  REQUIRE( waterRule->children().size() == 1 );
  CHECK( waterRule->children().at( 0 )->filterExpression() == QLatin1String( "\"depth\" > 5" ) );
}

// ---------------------------------------------------------------------------
// #815 — design-token resolution lacks multi-hop recursion
// ---------------------------------------------------------------------------

TEST_CASE( "Style token references resolve through alias chains (#815)",
           "[platform6][tokens]" )
{
  Json::Value tokenSet( Json::objectValue );
  tokenSet["id"] = "test-alias-set";
  tokenSet["version"] = 1;
  tokenSet["colors"]["base"] = "#123456";
  tokenSet["colors"]["accent"] = "token:colors.base"; // one alias hop

  QTemporaryDir dir;
  REQUIRE( QDir().mkpath( dir.filePath( QStringLiteral( "tokens" ) ) ) );
  REQUIRE( writeTextFile( dir.filePath( QStringLiteral( "tokens/test-alias.json" ) ),
                          tokenSet.toStyledString() ) );
  TokenSetRegistry::instance().setDirectory( dir.path() );

  // resolveTokenSet materializes alias chains inside the resolved document.
  Json::Value styleRef( Json::objectValue );
  styleRef["token_set"] = "test-alias-set";
  const Json::Value resolved = resolveTokenSet( styleRef );
  CHECK( resolved["colors"]["accent"].asString() == "#123456" );
  CHECK_FALSE( ( resolved.isMember( "resolved" ) && resolved["resolved"].isMember( "token_problems" ) ) );

  // Two hops: accent2 -> accent -> base.
  const Json::Value doc = TokenSetRegistry::instance().find( QStringLiteral( "test-alias-set" ) );
  Json::Value extended = doc;
  extended["colors"]["accent2"] = "token:colors.accent";
  extended["id"] = "test-alias-set2";
  QString regError;
  REQUIRE( TokenSetRegistry::instance().registerTokenSet( extended, &regError ) );
  Json::Value styleRef2( Json::objectValue );
  styleRef2["token_set"] = "test-alias-set2";
  const Json::Value resolved2 = resolveTokenSet( styleRef2 );
  CHECK( resolved2["colors"]["accent2"].asString() == "#123456" );

  // A style document referencing the alias resolves to the final hex value.
  Json::Value style( Json::objectValue );
  style["schema_version"] = "1.0";
  style["kind"] = "style_spec";
  style["id"] = "style.test.alias";
  style["version"] = 1;
  style["applies_to"] = "vector";
  style["token_set_ref"] = "test-alias-set";
  style["vector"]["renderertype"] = "simple";
  style["vector"]["color"] = "token:colors.accent";
  std::vector<std::string> problems;
  const Json::Value resolvedStyle = resolveStyleTokens( style, resolved, &problems );
  CHECK( problems.empty() );
  CHECK( resolvedStyle["vector"]["color"].asString() == "#123456" );

  // Restore the default catalog for the other tests.
  TokenSetRegistry::instance().setDirectory( QString() );
}

TEST_CASE( "Token alias cycles are reported and never silently resolved (#815)",
           "[platform6][tokens]" )
{
  Json::Value tokenSet( Json::objectValue );
  tokenSet["id"] = "test-cycle-set";
  tokenSet["version"] = 1;
  tokenSet["colors"]["x"] = "token:colors.y";
  tokenSet["colors"]["y"] = "token:colors.x";

  std::vector<std::string> problems;
  const Json::Value resolved = resolveTokenReferenceChain( tokenSet, tokenSet["colors"]["x"], problems );
  REQUIRE_FALSE( problems.empty() );
  CHECK( problems.front().find( "cycle" ) != std::string::npos );
  // The original reference survives verbatim — no wrong value is invented.
  CHECK( resolved.asString() == "token:colors.y" );
}

// ---------------------------------------------------------------------------
// #784 — global recipe gate state contaminates parallel branches
// ---------------------------------------------------------------------------

TEST_CASE( "Recipe gate degradation propagates per branch, not globally (#784)",
           "[platform6][harness]" )
{
  QTemporaryDir dir;
  const QString recipePath = dir.filePath( QStringLiteral( "test_gates.json" ) );
  const std::string recipeJson = R"JSON({
    "schema_version": "2.0",
    "kind": "harness_recipe",
    "recipe_id": "test.gates",
    "title": "branch gate test",
    "intent": "test",
    "slots": [
      { "name": "optical", "required": false },
      { "name": "sar", "required": false }
    ],
    "outputs": [],
    "steps": [
      { "id": "optical_index", "operator_id": "op.optical", "when_slot": "optical",
        "params": { "input": "$optical.path", "output": "$outputs.optical_index_out",
                    "mode": "full" } },
      { "id": "optical_post", "operator_id": "op.post", "inputs": [ { "step": "optical_index" } ],
        "params": { "input": "$outputs.optical_index_out", "mode": "full" },
        "params_when_skipped": { "input": "", "mode": "degraded" } },
      { "id": "sar_index", "operator_id": "op.sar", "when_slot": "sar",
        "params": { "input": "$sar.path", "output": "$outputs.sar_index_out",
                    "mode": "full" } },
      { "id": "sar_post", "operator_id": "op.post", "inputs": [ { "step": "sar_index" } ],
        "params": { "input": "$outputs.sar_index_out", "mode": "full" },
        "params_when_skipped": { "input": "", "mode": "degraded" } },
      { "id": "fuse", "operator_id": "op.fuse", "inputs": [ { "step": "optical_post" }, { "step": "sar_post" } ],
        "params": { "mode": "full" },
        "params_when_skipped": { "mode": "degraded" } }
    ]
  })JSON";
  REQUIRE( writeTextFile( recipePath, recipeJson ) );

  QTemporaryDir dataDir;
  REQUIRE( writeTextFile( dataDir.filePath( QStringLiteral( "optical.tif" ) ), "x" ) );

  RecipeCatalog::instance().setDirectory( dir.path().toStdString() );
  RecipeCatalog::instance().reload();

  Json::Value bindings( Json::objectValue );
  bindings["slots"]["optical"] = dataDir.filePath( QStringLiteral( "optical.tif" ) ).toStdString();
  bindings["output_dir"] = dir.filePath( QStringLiteral( "out" ) ).toStdString();
  // The SAR slot stays unbound: only the SAR branch degrades.

  HarnessError error;
  const Json::Value plan = RecipeCatalog::instance().instantiateRecipe( "test.gates", bindings, error );
  REQUIRE( plan.isObject() );

  std::map<std::string, Json::Value> byId;
  for ( const auto &step : plan["steps"] )
    byId[step["id"].asString()] = step;

  // Open branch: both steps run with their real params.
  REQUIRE( byId.count( "optical_index" ) == 1 );
  CHECK( byId["optical_index"]["params"]["mode"].asString() == "full" );
  CHECK( byId["optical_index"]["params"]["input"].asString().find( "optical.tif" ) !=
         std::string::npos );
  REQUIRE( byId.count( "optical_post" ) == 1 );
  // Regression: this step's own gate is open and its upstream ran — it must
  // keep its full template even though the unrelated SAR gate closed.
  CHECK( byId["optical_post"]["params"]["mode"].asString() == "full" );

  // Closed branch: the gated step drops, the downstream runs degraded.
  CHECK( byId.count( "sar_index" ) == 0 );
  REQUIRE( byId.count( "sar_post" ) == 1 );
  CHECK( byId["sar_post"]["params"]["mode"].asString() == "degraded" );

  // The fusion step depends on a degraded branch -> degraded as well.
  REQUIRE( byId.count( "fuse" ) == 1 );
  CHECK( byId["fuse"]["params"]["mode"].asString() == "degraded" );

  // Both slots bound: no gate closed anywhere, everything runs full.
  QTemporaryDir dataDir2;
  REQUIRE( writeTextFile( dataDir2.filePath( QStringLiteral( "sar.tif" ) ), "x" ) );
  Json::Value bindings2( Json::objectValue );
  bindings2["slots"]["optical"] = dataDir.filePath( QStringLiteral( "optical.tif" ) ).toStdString();
  bindings2["slots"]["sar"] = dataDir2.filePath( QStringLiteral( "sar.tif" ) ).toStdString();
  bindings2["output_dir"] = dir.filePath( QStringLiteral( "out2" ) ).toStdString();
  const Json::Value plan2 =
    RecipeCatalog::instance().instantiateRecipe( "test.gates", bindings2, error );
  std::map<std::string, Json::Value> byId2;
  for ( const auto &step : plan2["steps"] )
    byId2[step["id"].asString()] = step;
  CHECK( byId2.size() == 5 );
  CHECK( byId2["sar_index"]["params"]["mode"].asString() == "full" );
  CHECK( byId2["sar_post"]["params"]["mode"].asString() == "full" );
  CHECK( byId2["fuse"]["params"]["mode"].asString() == "full" );

  // Restore the default catalog.
  RecipeCatalog::instance().setDirectory(
    ( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" ) );
  RecipeCatalog::instance().reload();
}

// ---------------------------------------------------------------------------
// Milestone J — structural visual matrix (constraint-heavy double compile)
// ---------------------------------------------------------------------------

TEST_CASE( "Constraint-heavy layout compiles deterministically (J/V10/V14)",
           "[platform6][visual]" )
{
  auto buildSpec = []() {
    Json::Value spec = makeSpec();
    Json::Value frame = rectItem( "map-1", 12, 30, 200, 150 );
    Json::Value extent( Json::arrayValue );
    extent.append( 116.0 );
    extent.append( 39.0 );
    extent.append( 117.0 );
    extent.append( 40.0 );
    frame["extent"] = extent;
    spec["map_frames"].append( frame );
    Json::Value title = rectItem( "title-1", 12, 10, 200, 12 );
    title["text"] = "多约束布局 Flood extent — long English suffix";
    spec["titles"].append( title );
    spec["legends"].append( rectItem( "legend-1", 220, 30, 65, 80 ) );
    spec["scale_bars"].append( rectItem( "scalebar-1", 14, 186, 48, 8 ) );
    spec["north_arrows"].append( rectItem( "north-1", 70, 186, 10, 10 ) );
    Json::Value sourceNote = rectItem( "source-1", 90, 188, 180, 8 );
    sourceNote["text"] = "Data: SICNU GEO RS / exp-rs — CRS: EPSG:4326";
    spec["source_notes"].append( sourceNote );
    Json::Value fit = pairConstraint( "c-fit", "fit_content", "legend-1", std::string() );
    Json::Value content( Json::arrayValue );
    content.append( 65 );
    content.append( 90 );
    fit["content_mm"] = content;
    spec["constraints"].append( fit );
    Json::Value keep = pairConstraint( "c-keep", "keep_with", "scalebar-1", "north-1" );
    keep["gap_mm"] = 4;
    spec["constraints"].append( keep );
    Json::Value source( Json::objectValue );
    source["id"] = "c-source";
    source["kind"] = "above";
    Json::Value pair( Json::arrayValue );
    pair.append( "scalebar-1" );
    pair.append( "source-1" );
    source["items"] = pair;
    spec["constraints"].append( source );
    return spec;
  };

  Json::Value specA = buildSpec();
  Json::Value specB = buildSpec();
  // Permuted declaration order must converge to the same geometry.
  std::swap( specB["constraints"][0], specB["constraints"][2] );

  const CompositionResult a = resolveComposition( specA, 10.0 );
  const CompositionResult b = resolveComposition( specB, 10.0 );
  CHECK( a.converged );
  CHECK( b.converged );
  CHECK( specA["legends"][0]["rect_mm"].toStyledString() ==
         specB["legends"][0]["rect_mm"].toStyledString() );
  CHECK( specA["north_arrows"][0]["rect_mm"].toStyledString() ==
         specB["north_arrows"][0]["rect_mm"].toStyledString() );
  CHECK( specA["source_notes"][0]["rect_mm"].toStyledString() ==
         specB["source_notes"][0]["rect_mm"].toStyledString() );

  // The solver-resolved spec compiles, and compiles identically twice:
  // every item lands at the same scene geometry (structural hash).
  // NB: geometryA is captured before the second compile because compiling
  // the same layout name replaces the previous layout.
  auto geometryMap = []( QgsPrintLayout *layout ) {
    std::map<std::string, std::string> out;
    const QList<QGraphicsItem *> items = layout->items();
    for ( QGraphicsItem *sceneItem : items )
    {
      auto *item = dynamic_cast<QgsLayoutItem *>( sceneItem );
      if ( !item || item->type() == QgsLayoutItemRegistry::LayoutPage )
        continue;
      const QRectF rect = item->mapToScene( item->rect() ).boundingRect();
      const std::string id = item->id().isEmpty()
                               ? std::string( "type-" ) + std::to_string( item->type() )
                               : item->id().toStdString();
      out[id] = QString::number( rect.left(), 'f', 2 ).toStdString() + "," +
                QString::number( rect.top(), 'f', 2 ).toStdString() + "," +
                QString::number( rect.width(), 'f', 2 ).toStdString() + "," +
                QString::number( rect.height(), 'f', 2 ).toStdString();
    }
    return out;
  };
  QString error;
  QgsPrintLayout *layoutA = MapSpecCompiler::compile( specA, &error );
  REQUIRE( layoutA != nullptr );
  const auto geometryA = geometryMap( layoutA );
  QgsPrintLayout *layoutB = MapSpecCompiler::compile( specB, &error );
  REQUIRE( layoutB != nullptr );
  const auto geometryB = geometryMap( layoutB );
  CHECK( geometryA == geometryB );
  CHECK( geometryA.count( "legend-1" ) == 1 );
  QgsProject::instance()->layoutManager()->clear();
}

TEST_CASE( "Conditional branches prune before compile and survivors compile (J/V9)",
           "[platform6][visual]" )
{
  Json::Value spec = makeSpec();
  Json::Value frame = rectItem( "map-1", 12, 30, 200, 150 );
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 117.0 );
  extent.append( 40.0 );
  frame["extent"] = extent;
  spec["map_frames"].append( frame );
  Json::Value title = rectItem( "title-1", 12, 10, 200, 12 );
  title["text"] = "conditional";
  title["visible_if"] = "flags.publish";
  spec["titles"].append( title );
  Json::Value note = rectItem( "note-1", 12, 188, 120, 8 );
  note["text"] = "uncertainty note";
  note["visible_if"] = "flags.publish";
  spec["source_notes"].append( note );

  Json::Value flags( Json::objectValue );
  flags["flags"] = Json::Value( Json::objectValue );
  flags["flags"]["publish"] = true;
  std::vector<std::string> errors;
  resolveMapSpecConditions( spec, flags, &errors );
  CHECK( errors.empty() );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  CHECK( layoutHasItem( layout, "title-1" ) );
  CHECK( layoutHasItem( layout, "note-1" ) );

  // A false runtime flag prunes both items; the map frame stays.
  Json::Value spec2 = makeSpec();
  spec2["map_frames"].append( frame );
  Json::Value title2 = rectItem( "title-1", 12, 10, 200, 12 );
  title2["text"] = "conditional";
  title2["visible_if"] = "flags.publish";
  spec2["titles"].append( title2 );
  Json::Value off( Json::objectValue );
  off["flags"] = Json::Value( Json::objectValue );
  off["flags"]["publish"] = false;
  resolveMapSpecConditions( spec2, off, &errors );
  CHECK( errors.empty() );
  CHECK( findMapSpecItem( spec2, "title-1" ).isNull() );
  QgsProject::instance()->layoutManager()->clear();
}
