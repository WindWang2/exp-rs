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
#include "operators/rs/rs_spectral_index_operator.h"
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
    // Optional numerics stay ABSENT until the user edits them or setValues
    // supplies the key — operators treat isMember as an explicit override
    // (numComponents 0 = "all bands" is the kernel default, not a form fill).
    const Json::Value collected = form.values();
    REQUIRE( collected["input"].asString() == "/tmp/in.tif" );
    REQUIRE( collected["output"].asString() == "/tmp/out.tif" );
    REQUIRE_FALSE( collected.isMember( "numComponents" ) );
    REQUIRE( components->value() == 0 );

    Json::Value withComponents = collected;
    withComponents["numComponents"] = 3;
    form.setValues( withComponents );
    REQUIRE( form.values()["numComponents"].asInt() == 3 );
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

#include <QDoubleSpinBox>
#include <catch2/catch_approx.hpp>

// ── Workbench 6.0 Milestone H: units, recommended values, soft ranges ──────

TEST_CASE( "SchemaFormBuilder 3.0: unit labels, recommended hints, soft-range warnings",
           "[ux6][schema-form]" )
{
    testApp();
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    Json::Value res;
    res["type"] = "number";
    res["title"] = "分辨率";
    res["x-ui-unit"] = "m";
    res["x-ui-recommended"] = "10";
    res["x-ui-soft-min"] = 1.0;
    res["x-ui-soft-max"] = 100.0;
    res["default"] = 10.0;
    props["resolution"] = res;
    schema["properties"] = props;

    SchemaFormBuilder form;
    form.rebuild( schema );

    QDoubleSpinBox *spin = form.findChild<QDoubleSpinBox *>();
    REQUIRE( spin != nullptr );

    // Unit hint surfaces in the label (Milestone H).
    bool labelHasUnit = false;
    const QList<QLabel *> labels = form.findChildren<QLabel *>();
    for ( const QLabel *l : labels )
    {
        if ( l->text().contains( QLatin1String( "(m)" ) ) )
            labelHasUnit = true;
    }
    REQUIRE( labelHasUnit );

    // Recommended value surfaces as a tooltip (and accessible hint).
    REQUIRE_FALSE( spin->toolTip().isEmpty() );
    CHECK( spin->toolTip().contains( QLatin1String( "10" ) ) );

    // Within the soft range: no issues at all.
    spin->setValue( 10.0 );
    CHECK( form.validate().isEmpty() );

    // Above the soft (but inside the hard) range: a WARNING, never blocking.
    spin->setValue( 500.0 );
    bool sawWarning = false;
    bool sawBlocking = false;
    for ( const auto &issue : form.validate() )
    {
        if ( issue.fieldName != QLatin1String( "resolution" ) )
            continue;
        if ( issue.isError )
            sawBlocking = true;
        else
            sawWarning = true;
    }
    REQUIRE( sawWarning );
    REQUIRE_FALSE( sawBlocking );
}

TEST_CASE( "SchemaFormBuilder 3.0: x-ui-visible-when hides and excludes conditional fields",
           "[ux6][schema-form]" )
{
    testApp();
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    Json::Value mode;
    mode["type"] = "string";
    Json::Value modes = Json::Value( Json::arrayValue );
    modes.append( "auto" );
    modes.append( "manual" );
    mode["enum"] = modes;
    mode["default"] = "auto";
    props["mode"] = mode;

    Json::Value thresh;
    thresh["type"] = "number";
    thresh["title"] = "阈值";
    Json::Value when;
    when["mode"] = "manual";
    thresh["x-ui-visible-when"] = when;
    thresh["default"] = 0.5;
    props["threshold"] = thresh;
    schema["properties"] = props;

    SchemaFormBuilder form;
    form.rebuild( schema );

    // auto mode: the dependent field is hidden AND excluded from values().
    form.updateValidationUi();
    Json::Value autoValues = form.values();
    CHECK( autoValues.isMember( "mode" ) );
    CHECK_FALSE( autoValues.isMember( "threshold" ) );

    // manual mode: the field becomes visible and is collected again.
    QComboBox *combo = form.findChild<QComboBox *>();
    REQUIRE( combo != nullptr );
    combo->setCurrentText( QStringLiteral( "manual" ) );
    form.updateValidationUi();
    Json::Value manualValues = form.values();
    CHECK( manualValues.isMember( "threshold" ) );
    CHECK( manualValues["threshold"].asDouble() == Catch::Approx( 0.5 ) );
}

// ── Review L #3: setValues re-evaluates conditional visibility ─────────────

TEST_CASE( "SchemaFormBuilder 3.0: setValues refreshes x-ui-visible-when state",
           "[ux6][schema-form][review-l]" )
{
    testApp();
    Json::Value schema;
    schema["type"] = "object";
    Json::Value props;
    Json::Value mode;
    mode["type"] = "string";
    Json::Value modes = Json::Value( Json::arrayValue );
    modes.append( "auto" );
    modes.append( "manual" );
    mode["enum"] = modes;
    mode["default"] = "auto";
    props["mode"] = mode;

    Json::Value thresh;
    thresh["type"] = "number";
    thresh["title"] = "阈值";
    Json::Value when;
    when["mode"] = "manual";
    thresh["x-ui-visible-when"] = when;
    thresh["default"] = 0.5;
    props["threshold"] = thresh;
    schema["properties"] = props;

    SchemaFormBuilder form;
    form.rebuild( schema );

    // setValues suppresses widget signals — the conditional re-evaluation
    // must still happen, or the dependent field stays hidden AND excluded
    // from values()/validate() even though its condition now holds.
    Json::Value values;
    values["mode"] = "manual";
    values["threshold"] = 0.5;
    form.setValues( values );

    Json::Value collected = form.values();
    CHECK( collected.isMember( "threshold" ) );
    CHECK( collected["threshold"].asDouble() == Catch::Approx( 0.5 ) );
}

TEST_CASE( "Optional numeric defaults stay absent until edited",
           "[ux4][schema-form][issue-927]" )
{
    testApp();
    Json::Value schema;
    schema["type"] = "object";
    schema["required"].append( "input" );
    schema["required"].append( "count" );
    Json::Value props;
    Json::Value input;
    input["type"] = "string";
    input["x-ui-type"] = "raster";
    props["input"] = input;
    Json::Value count;
    count["type"] = "integer";
    count["default"] = 2;
    count["minimum"] = 1;
    count["maximum"] = 10;
    props["count"] = count;
    Json::Value cellSize;
    cellSize["type"] = "number";
    cellSize["default"] = 30;
    props["cellSize"] = cellSize;
    Json::Value nir;
    nir["type"] = "integer";
    nir["default"] = 4;
    props["nir"] = nir;
    schema["properties"] = props;

    SchemaFormBuilder form;
    form.rebuild( schema );

    Json::Value untouched = form.values();
    REQUIRE( untouched.isMember( "count" ) );
    REQUIRE( untouched["count"].asInt() == 2 );
    REQUIRE_FALSE( untouched.isMember( "cellSize" ) );
    REQUIRE_FALSE( untouched.isMember( "nir" ) );

    const QList<QSpinBox *> spins = form.findChildren<QSpinBox *>();
    REQUIRE( spins.size() >= 2 );
    QSpinBox *nirSpin = nullptr;
    for ( QSpinBox *spin : spins )
    {
        if ( spin->value() == 4 )
            nirSpin = spin;
    }
    REQUIRE( nirSpin != nullptr );
    nirSpin->setValue( 8 );
    Json::Value edited = form.values();
    REQUIRE( edited.isMember( "nir" ) );
    REQUIRE( edited["nir"].asInt() == 8 );
    REQUIRE_FALSE( edited.isMember( "cellSize" ) );

    Json::Value explicitValues;
    explicitValues["input"] = "/tmp/in.tif";
    explicitValues["cellSize"] = 15.0;
    form.setValues( explicitValues );
    Json::Value afterSet = form.values();
    REQUIRE( afterSet["cellSize"].asDouble() == Catch::Approx( 15.0 ) );
    REQUIRE( afterSet.isMember( "nir" ) );
}

TEST_CASE( "rs:spectral_index omits untouched band-role integers",
           "[ux4][schema-form][issue-927]" )
{
    testApp();
    sicnu::operators::rs::RsSpectralIndexOperator op;
    SchemaFormBuilder form;
    form.rebuild( op.schema() );

    const Json::Value collected = form.values();
    REQUIRE( collected.isMember( "index" ) );
    REQUIRE_FALSE( collected.isMember( "nir" ) );
    REQUIRE_FALSE( collected.isMember( "red" ) );
    REQUIRE_FALSE( collected.isMember( "green" ) );
    REQUIRE_FALSE( collected.isMember( "blue" ) );
    REQUIRE_FALSE( collected.isMember( "swir" ) );
    REQUIRE_FALSE( collected.isMember( "rededge" ) );
}
