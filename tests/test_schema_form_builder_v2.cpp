// test_schema_form_builder_v2.cpp — Schema-driven Operator UI (UX 4.0, Milestone B)
//
// Pins the form-generation contract: operator schemas drive the parameter
// editors; schema defaults are the single source of truth (the form must not
// invent defaults); required/range/minItems/JSON validation produces inline,
// actionable issues; advanced fields collapse but stay reachable.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStyle>

#include <json/json.h>

#include <sstream>

#include "operators/rs/rs_pca_operator.h"
#include "operators/rs/rs_band_tools_operators.h"
#include "shell/schema_form_builder.h"
#include "widgets/crs_selector.h"

namespace {

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

const QGroupBox *advancedBox( const SchemaFormBuilder &form )
{
    return form.findChild<QGroupBox *>( QStringLiteral( "rsTaskPanelSection" ) );
}

} // namespace

TEST_CASE( "Schema defaults drive the generated form (rs:pca)", "[ux4][schema-form]" )
{
    testApp();
    sicnu::operators::rs::RsPcaOperator pca;
    const Json::Value schema = pca.schema();

    SchemaFormBuilder form;
    form.rebuild( schema );

    // No silent mismatch: the numComponents control's default equals the
    // schema default declared by the operator.
    QSpinBox *components = form.findChild<QSpinBox *>();
    REQUIRE( components != nullptr );
    const Json::Value &prop = schema["properties"]["numComponents"];
    if ( prop.isMember( "default" ) )
        REQUIRE( components->value() == prop["default"].asInt() );

    // Required parameters produce actionable validation errors until filled.
    REQUIRE( form.hasErrors() );
    const QList<SchemaFormBuilder::ValidationIssue> issues = form.validate();
    bool mentionsInput = false;
    for ( const SchemaFormBuilder::ValidationIssue &issue : issues )
    {
        if ( issue.isError && issue.fieldName == QLatin1String( "input" ) )
            mentionsInput = true;
    }
    REQUIRE( mentionsInput );

    // Filling the required fields clears the blocking errors.
    Json::Value values = form.values();
    values["input"] = "/tmp/in.tif";
    values["output"] = "/tmp/out.tif";
    form.setValues( values );
    form.updateValidationUi();
    REQUIRE_FALSE( form.hasErrors() );

    // values() round-trips the collected parameters as a JSON object.
    const Json::Value collected = form.values();
    REQUIRE( collected["input"].asString() == "/tmp/in.tif" );
    REQUIRE( collected["output"].asString() == "/tmp/out.tif" );
    REQUIRE( collected["numComponents"].asInt() == components->value() );
}

TEST_CASE( "rs:band_ratio schema generates mode-aware editors", "[ux4][schema-form]" )
{
    testApp();
    sicnu::operators::rs::RsBandRatioOperator op;

    SchemaFormBuilder form;
    form.rebuild( op.schema() );

    // Band numbers must be clamped to the schema range (1..10000).
    const QList<QSpinBox *> spins = form.findChildren<QSpinBox *>();
    REQUIRE( !spins.isEmpty() );
    for ( QSpinBox *spin : spins )
        REQUIRE( spin->minimum() >= 1 );

    // The mode enum arrives from the schema with its declared default.
    const Json::Value collected = form.values();
    REQUIRE( collected.isMember( "mode" ) );
    REQUIRE( collected["mode"].asString() == "ratio" );
}

TEST_CASE( "x-ui-type hints select the declared editors", "[ux4][schema-form]" )
{
    testApp();
    const Json::CharReaderBuilder builder;
    Json::Value schema;
    std::string errors;
    {
        std::istringstream stream( R"({
          "type": "object",
          "required": ["input", "model"],
          "properties": {
            "input":  { "type": "string", "x-ui-type": "raster", "description": "输入" },
            "vector": { "type": "string", "x-ui-type": "vector" },
            "crs":    { "type": "string", "x-ui-type": "crs", "default": "EPSG:4326" },
            "model":  { "type": "string", "x-ui-type": "model" },
            "asset":  { "type": "string", "x-ui-type": "asset" },
            "color":  { "type": "string", "x-ui-type": "color", "default": "#0B6E4F" },
            "jsonCfg":{ "type": "object", "description": "高级配置", "x-ui-advanced": true },
            "bands":  { "type": "array", "items": { "type": "integer" }, "minItems": 2 }
          }
        })" );
        REQUIRE( Json::parseFromStream( builder, stream, &schema, &errors ) );
    }

    SchemaFormBuilder form;
    form.rebuild( schema );

    REQUIRE( form.findChild<CrsSelector *>() != nullptr );
    REQUIRE( form.findChild<QPlainTextEdit *>() != nullptr );

    // Asset/model combos expose the declared choices with stable ids as data.
    form.setAssetChoices( { QStringLiteral( "asset-7" ) }, { QStringLiteral( "scene_a.tif" ) } );
    form.setModelChoices( { QStringLiteral( "water-unet" ), QStringLiteral( "roads-yolo" ) } );

    Json::Value values = form.values();
    values["input"] = "/data/in.tif";
    values["vector"] = "/data/points.shp";
    values["model"] = "water-unet";
    values["asset"] = "asset-7";
    values["bands"] = "3, 4";
    form.setValues( values );
    form.updateValidationUi();
    REQUIRE_FALSE( form.hasErrors() );

    const Json::Value collected = form.values();
    REQUIRE( collected["model"].asString() == "water-unet" );
    REQUIRE( collected["asset"].asString() == "asset-7" );
    REQUIRE( collected["crs"].asString() == "EPSG:4326" );
    REQUIRE( collected["color"].asString() == "#0B6E4F" );
    REQUIRE( collected["bands"].isArray() );
    REQUIRE( collected["bands"].size() == 2 );

    // minItems contract: a single band is an actionable inline error.
    form.setValues( Json::Value( Json::objectValue ) );
    Json::Value thin = form.values();
    thin["input"] = "/data/in.tif";
    thin["model"] = "water-unet";
    thin["bands"] = "3";
    form.setValues( thin );
    form.updateValidationUi();
    REQUIRE( form.hasErrors() );
    bool minItemsError = false;
    for ( const SchemaFormBuilder::ValidationIssue &issue : form.validate() )
    {
        if ( issue.isError && issue.fieldName == QLatin1String( "bands" ) )
            minItemsError = true;
    }
    REQUIRE( minItemsError );
}

TEST_CASE( "Advanced fields collapse but stay reachable", "[ux4][schema-form]" )
{
    testApp();
    Json::Value schema;
    const Json::CharReaderBuilder builder;
    std::string errors;
    {
        std::istringstream stream( R"({
          "type": "object",
          "properties": {
            "plain": { "type": "string" },
            "advancedParam": { "type": "string", "x-ui-advanced": true, "default": "keep-me" }
          }
        })" );
        REQUIRE( Json::parseFromStream( builder, stream, &schema, &errors ) );
    }

    SchemaFormBuilder form;
    form.rebuild( schema );

    // The advanced section exists (never invisible) and starts collapsed.
    const QList<QGroupBox *> sections = form.findChildren<QGroupBox *>();
    QGroupBox *advanced = nullptr;
    for ( QGroupBox *box : sections )
    {
        if ( box->isCheckable() )
            advanced = box;
    }
    REQUIRE( advanced != nullptr );
    REQUIRE( !advanced->isChecked() );

    // Its value still round-trips through values().
    REQUIRE( form.values()["advancedParam"].asString() == "keep-me" );
}

TEST_CASE( "Invalid JSON and invalid color produce inline errors", "[ux4][schema-form]" )
{
    testApp();
    Json::Value schema;
    const Json::CharReaderBuilder builder;
    std::string errors;
    {
        std::istringstream stream( R"({
          "type": "object",
          "properties": {
            "color":   { "type": "string", "x-ui-type": "color" },
            "jsonCfg": { "type": "object", "x-ui-type": "json" }
          }
        })" );
        REQUIRE( Json::parseFromStream( builder, stream, &schema, &errors ) );
    }

    SchemaFormBuilder form;
    form.rebuild( schema );

    Json::Value bad = form.values();
    bad["color"] = "not-a-color";
    bad["jsonCfg"] = "{ this is not json ";
    form.setValues( bad );
    form.updateValidationUi();
    REQUIRE( form.hasErrors() );
}
