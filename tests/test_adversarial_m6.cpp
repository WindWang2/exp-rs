/***************************************************************************
 * tests/test_adversarial_m6.cpp
 * Adversarial Empirical Stress Suite for Milestone 6
 * Issues: #781, #782, #784, #802, #804, #805, #814, #815
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/cartography/composition.h"
#include "agent/cartography/style_compiler.h"
#include "agent/cartography/style_spec.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_conditions.h"
#include "agent/harness/recipe_catalog.h"

#include <qgsvectorlayer.h>
#include <qgsrulebasedrenderer.h>
#include <QApplication>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

#include <cmath>
#include <vector>
#include <string>

using Catch::Approx;
using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;
using namespace sicnu::agent::harness;

namespace
{
int fake_argc = 1;
char fake_argv0[] = "test_adversarial_m6";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app && !QCoreApplication::instance() )
        app = new QApplication( fake_argc, fake_argv );
    return app;
}
} // namespace

// ============================================================================
// Case 1: #781, #805, #814 - Multi-Pass Constraint Relaxation & fit_content
// ============================================================================
TEST_CASE( "Adversarial M6 - #781 & #805 & #814: Multi-pass relaxation & fit_content convergence",
           "[adversarial][m6][issue-781][issue-805][issue-814]" )
{
    Json::Value spec( Json::objectValue );
    spec["page"]["width_mm"] = 297.0;
    spec["page"]["height_mm"] = 210.0;
    spec["page"]["margin_mm"] = 10.0;

    // Items: title, subtitle, map frame, side note
    Json::Value t1( Json::objectValue );
    t1["id"] = "title-1";
    t1["type"] = "title";
    t1["content_mm"][0] = 120.0;
    t1["content_mm"][1] = 25.0;
    t1["rect_mm"][0] = 10.0;
    t1["rect_mm"][1] = 10.0;
    t1["rect_mm"][2] = 30.0; // initial stub width
    t1["rect_mm"][3] = 10.0; // initial stub height
    spec["items"].append( t1 );

    Json::Value sub( Json::objectValue );
    sub["id"] = "sub-1";
    sub["type"] = "subtitle";
    sub["rect_mm"][0] = 10.0;
    sub["rect_mm"][1] = 20.0;
    sub["rect_mm"][2] = 60.0;
    sub["rect_mm"][3] = 8.0;
    spec["items"].append( sub );

    Json::Value map( Json::objectValue );
    map["id"] = "map-1";
    map["type"] = "map_frame";
    map["rect_mm"][0] = 10.0;
    map["rect_mm"][1] = 30.0;
    map["rect_mm"][2] = 150.0;
    map["rect_mm"][3] = 100.0;
    spec["items"].append( map );

    Json::Value side( Json::objectValue );
    side["id"] = "side-1";
    side["type"] = "source_note";
    side["rect_mm"][0] = 150.0;
    side["rect_mm"][1] = 10.0;
    side["rect_mm"][2] = 40.0;
    side["rect_mm"][3] = 10.0;
    spec["items"].append( side );

    // Interleaved dependent constraints in reverse order to force multi-pass relaxation:
    // Constraint 1: stack map below sub (depends on sub)
    Json::Value c1( Json::objectValue );
    c1["id"] = "c1_map_below_sub";
    c1["kind"] = "stack";
    c1["direction"] = "below";
    c1["items"].append( "sub-1" );
    c1["items"].append( "map-1" );
    c1["gap_mm"] = 5.0;
    spec["constraints"].append( c1 );

    // Constraint 2: stack sub below title (depends on title)
    Json::Value c2( Json::objectValue );
    c2["id"] = "c2_sub_below_title";
    c2["kind"] = "stack";
    c2["direction"] = "below";
    c2["items"].append( "title-1" );
    c2["items"].append( "sub-1" );
    c2["gap_mm"] = 5.0;
    spec["constraints"].append( c2 );

    // Constraint 3: fit_content on title (single-item constraint #781)
    Json::Value c3( Json::objectValue );
    c3["id"] = "c3_fit_title";
    c3["kind"] = "fit_content";
    c3["items"].append( "title-1" );
    spec["constraints"].append( c3 );

    // Constraint 4: match height of side note to title
    Json::Value c4( Json::objectValue );
    c4["id"] = "c4_side_match_title";
    c4["kind"] = "match_height";
    c4["items"].append( "title-1" );
    c4["items"].append( "side-1" );
    spec["constraints"].append( c4 );

    const auto res = resolveComposition( spec, 10.0 );
    CHECK( res.constraintsSolved == 4 );
    CHECK( res.unsatisfied.empty() );

    // Exact numerical geometry assertions (#814):
    // 1. title-1 resized to content_mm: 120.0 x 25.0
    CHECK( spec["items"][0]["rect_mm"][2].asDouble() == Approx( 120.0 ).margin( 1e-6 ) );
    CHECK( spec["items"][0]["rect_mm"][3].asDouble() == Approx( 25.0 ).margin( 1e-6 ) );

    // 2. sub-1 placed below title-1: y = 10.0 + 25.0 + 5.0 = 40.0
    CHECK( spec["items"][1]["rect_mm"][1].asDouble() == Approx( 40.0 ).margin( 1e-6 ) );

    // 3. map-1 placed below sub-1: y = 40.0 + 8.0 + 5.0 = 53.0
    CHECK( spec["items"][2]["rect_mm"][1].asDouble() == Approx( 53.0 ).margin( 1e-6 ) );

    // 4. side-1 height matched to title-1: 25.0
    CHECK( spec["items"][3]["rect_mm"][3].asDouble() == Approx( 25.0 ).margin( 1e-6 ) );
}

// ============================================================================
// Case 2: #782 - Rule-Based Renderer Hierarchy
// ============================================================================
TEST_CASE( "Adversarial M6 - #782: Rule-based renderer root container and sibling rules",
           "[adversarial][m6][issue-782]" )
{
    ensureApp();
    auto *vl = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                   QStringLiteral( "test_points" ),
                                   QStringLiteral( "memory" ) );
    REQUIRE( vl->isValid() );

    Json::Value style( Json::objectValue );
    style["vector"]["renderertype"] = "rule_based";

    Json::Value r1( Json::objectValue );
    r1["expression"] = "class = 1";
    r1["label"] = "Forest";
    r1["color"] = "#228B22";

    Json::Value r2( Json::objectValue );
    r2["expression"] = "class = 2";
    r2["label"] = "Water";
    r2["color"] = "#1E90FF";

    Json::Value r3( Json::objectValue );
    r3["expression"] = "class = 3";
    r3["label"] = "Urban";
    r3["color"] = "#DC143C";

    style["vector"]["rules"].append( r1 );
    style["vector"]["rules"].append( r2 );
    style["vector"]["rules"].append( r3 );

    QString error;
    QStringList problems;
    bool ok = applyStyleSpecToLayer( vl, style, &error, &problems );
    CHECK( ok );
    CHECK( error.isEmpty() );

    auto *rb = dynamic_cast<QgsRuleBasedRenderer *>( vl->renderer() );
    REQUIRE( rb != nullptr );
    auto *root = rb->rootRule();
    REQUIRE( root != nullptr );

    // The root rule must be a container with 3 sibling children (#782)
    REQUIRE( root->children().size() == 3 );
    CHECK( root->children()[0]->filterExpression().toStdString() == "class = 1" );
    CHECK( root->children()[0]->children().isEmpty() );

    CHECK( root->children()[1]->filterExpression().toStdString() == "class = 2" );
    CHECK( root->children()[1]->children().isEmpty() );

    CHECK( root->children()[2]->filterExpression().toStdString() == "class = 3" );
    CHECK( root->children()[2]->children().isEmpty() );

    delete vl;
}

// ============================================================================
// Case 3: #784 - Recipe Parameter Gating Branch Independence
// ============================================================================
TEST_CASE( "Adversarial M6 - #784: Recipe parameter gating branch independence",
           "[adversarial][m6][issue-784]" )
{
    // #1736: the old body was `CHECK(listRecipes().size() >= 0)` — vacuous on
    // an unsigned size. Ported from the e2e twin (test_e2e_open_issues.cpp
    // "Tier 1 - #784") so the adversarial lane asserts the real regression
    // property: a closed gate degrades only its own branch; a global gate
    // leak would flip the parallel branch's params too.
    QTemporaryDir dir;
    const QString recipePath = dir.filePath( QStringLiteral( "adversarial_m6_gates.json" ) );
    const std::string recipeJson = R"JSON({
    "schema_version": "2.0",
    "kind": "harness_recipe",
    "recipe_id": "adv.m6_gates",
    "title": "branch gate adversarial",
    "intent": "test",
    "slots": [
      { "name": "optical", "required": false },
      { "name": "sar", "required": false }
    ],
    "outputs": [],
    "steps": [
      { "id": "optical_index", "operator_id": "op.optical", "when_param": "enable_optical",
        "params": { "input": "$optical.path", "mode": "full", "threshold": 0.5 } },
      { "id": "sar_index", "operator_id": "op.sar", "when_param": "enable_sar",
        "params": { "input": "$sar.path", "mode": "full", "threshold": 0.5 } },
      { "id": "sar_post", "operator_id": "op.post", "inputs": [ { "step": "sar_index" } ],
        "params": { "mode": "full", "threshold": 0.5 },
        "params_when_skipped": { "mode": "degraded", "threshold": 0 } }
    ]
  })JSON";
    QFile recipeFile( recipePath );
    REQUIRE( recipeFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    recipeFile.write( recipeJson.data(), static_cast<qint64>( recipeJson.size() ) );
    recipeFile.close();

    QTemporaryDir dataDir;
    auto writeDummy = [&dataDir]( const QString &name ) {
        QFile file( dataDir.filePath( name ) );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
            return false;
        file.write( "x" );
        file.close();
        return true;
    };
    REQUIRE( writeDummy( QStringLiteral( "optical.tif" ) ) );
    REQUIRE( writeDummy( QStringLiteral( "sar.tif" ) ) );

    auto &catalog = RecipeCatalog::instance();
    catalog.setDirectory( dir.path().toStdString() );
    catalog.reload();
    // The fixture really loaded — a validation failure would silently empty
    // every plan below and make the branch assertions vacuous in turn.
    const Json::Value loadedRecipes = catalog.listRecipes();
    REQUIRE( loadedRecipes.size() == 1 );
    REQUIRE( loadedRecipes[0]["recipe_id"].asString() == "adv.m6_gates" );

    auto instantiate = [&]( bool enableOptical, bool enableSar ) {
        Json::Value bindings( Json::objectValue );
        bindings["slots"]["optical"] = dataDir.filePath( QStringLiteral( "optical.tif" ) ).toStdString();
        bindings["slots"]["sar"] = dataDir.filePath( QStringLiteral( "sar.tif" ) ).toStdString();
        bindings["params"]["enable_optical"] = enableOptical;
        bindings["params"]["enable_sar"] = enableSar;
        bindings["output_dir"] = dir.filePath( QStringLiteral( "out" ) ).toStdString();
        sicnu::agent::harness::HarnessError error;
        return catalog.instantiateRecipe( "adv.m6_gates", bindings, error );
    };
    auto findStep = []( const Json::Value &plan, const std::string &id ) -> const Json::Value * {
        if ( !plan.isMember( "steps" ) || !plan["steps"].isArray() )
            return nullptr;
        for ( const auto &step : plan["steps"] )
            if ( step["id"].asString() == id )
                return &step;
        return nullptr;
    };

    // SAR gate closed: the SAR branch drops/degrades, the parallel optical
    // branch keeps its full template — a global anyGateClosed leak (#784)
    // would flip optical_index's params here.
    const Json::Value plan = instantiate( true, false );
    REQUIRE( plan.isObject() );
    CHECK( findStep( plan, "sar_index" ) == nullptr );
    const Json::Value *sarPost = findStep( plan, "sar_post" );
    REQUIRE( sarPost != nullptr );
    CHECK( (*sarPost)["params"]["mode"].asString() == "degraded" );
    CHECK( (*sarPost)["params"]["threshold"].asDouble() == Approx( 0.0 ) );
    const Json::Value *optical = findStep( plan, "optical_index" );
    REQUIRE( optical != nullptr );
    CHECK( (*optical)["params"]["mode"].asString() == "full" );
    CHECK( (*optical)["params"]["threshold"].asDouble() == Approx( 0.5 ) );
    CHECK( (*optical)["params"]["input"].asString().find( "optical.tif" ) != std::string::npos );
}

// ============================================================================
// Case 4: #802 - Condition Evaluation Accepts External Context
// ============================================================================
TEST_CASE( "Adversarial M6 - #802: Condition evaluation accepts external context & page_if",
           "[adversarial][m6][issue-802]" )
{
    Json::Value spec( Json::objectValue );
    spec["page"]["page_if"] = "has(mode) and mode == 'dark'";

    Json::Value item1( Json::objectValue );
    item1["id"] = "item-dark-only";
    item1["visible_if"] = "mode == 'dark' and zoom >= 10";
    spec["items"].append( item1 );

    Json::Value item2( Json::objectValue );
    item2["id"] = "item-light-only";
    item2["visible_if"] = "mode == 'light'";
    spec["items"].append( item2 );

    Json::Value externalContext( Json::objectValue );
    externalContext["mode"] = "dark";
    externalContext["zoom"] = 14;

    std::vector<std::string> errors;
    const auto ledger = resolveMapSpecConditions( spec, externalContext, &errors );
    CHECK( errors.empty() );
    CHECK( ledger.size() >= 2 );

    // item-dark-only must be kept, item-light-only must be hidden
    bool foundDark = false;
    bool foundLight = false;
    for ( const auto &entry : ledger )
    {
        if ( entry["id"].asString() == "item-dark-only" )
        {
            foundDark = true;
            CHECK( entry["outcome"].asString() == "visible" );
        }
        else if ( entry["id"].asString() == "item-light-only" )
        {
            foundLight = true;
            CHECK( entry["outcome"].asString() == "hidden" );
        }
    }
    CHECK( foundDark );
    CHECK( foundLight );
}

// ============================================================================
// Case 5: #804 - Condition AST Non-Short-Circuit Error Detection
// ============================================================================
TEST_CASE( "Adversarial M6 - #804: AST evaluates both branches without error suppression",
           "[adversarial][m6][issue-804]" )
{
    Json::Value context( Json::objectValue );
    context["flag"] = false;
    context["ready"] = true;

    bool value = false;
    std::string error;

    // 1. Conjunction: deciding branch references unknown property
    bool ok1 = evaluateCondition( "ready == true and missing.prop == 1", context, &value, &error );
    CHECK_FALSE( ok1 );
    CHECK_FALSE( error.empty() );

    // 2. Disjunction: deciding branch references unknown property
    error.clear();
    bool ok2 = evaluateCondition( "flag == true or invalid.node == 'bad'", context, &value, &error );
    CHECK_FALSE( ok2 );
    CHECK_FALSE( error.empty() );

    // 3. Valid evaluation with single & double quotes
    error.clear();
    bool ok3 = evaluateCondition( "ready == true and flag == false", context, &value, &error );
    CHECK( ok3 );
    CHECK( error.empty() );
    CHECK( value );
}

// ============================================================================
// Case 6: #815 - Multi-Hop Recursive Design Token Resolution & Cycle Safety
// ============================================================================
TEST_CASE( "Adversarial M6 - #815: Multi-hop recursive token resolution & cycle protection",
           "[adversarial][m6][issue-815]" )
{
    // 1. Multi-hop alias resolution: token:final_brand -> token:brand -> token:palette.primary -> "#007ACC"
    Json::Value tokens( Json::objectValue );
    tokens["palette"]["primary"] = "#007ACC";
    tokens["brand"] = "token:palette.primary";
    tokens["final_brand"] = "token:brand";

    Json::Value spec( Json::objectValue );
    spec["toolbar"]["background"] = "token:final_brand";
    spec["button"]["border"] = "token:brand";

    std::vector<std::string> problems;
    const auto resolved = resolveStyleTokens( spec, tokens, &problems );
    CHECK( problems.empty() );
    CHECK( resolved["toolbar"]["background"].asString() == "#007ACC" );
    CHECK( resolved["button"]["border"].asString() == "#007ACC" );

    // 2. Circular reference protection
    Json::Value cyclicTokens( Json::objectValue );
    cyclicTokens["loopA"] = "token:loopB";
    cyclicTokens["loopB"] = "token:loopA";

    Json::Value cyclicSpec( Json::objectValue );
    cyclicSpec["color"] = "token:loopA";

    std::vector<std::string> cyclicProblems;
    resolveStyleTokens( cyclicSpec, cyclicTokens, &cyclicProblems );
    CHECK_FALSE( cyclicProblems.empty() );
}
