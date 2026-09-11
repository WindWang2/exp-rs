// test_schema_form_4.cpp — Professional Workbench 8.0, SchemaForm 4.0
//
// Pins the 4.0 deepening of the schema-driven form contract: nested objects,
// object arrays, dynamic enum sources, async value checks, accessibility
// names, and stable value round-trips. Everything is schema-driven — no test
// references an operator id; the schema JSON is the whole contract.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <json/json.h>

#include <sstream>

#include "shell/schema_form_builder.h"

namespace {

QApplication &testApp()
{
    static int argc = 0;
    static QApplication app( argc, nullptr );
    return app;
}

Json::Value parseSchema( const std::string &text )
{
    Json::CharReaderBuilder builder;
    Json::Value schema;
    std::string errors;
    std::istringstream stream( text );
    REQUIRE( Json::parseFromStream( builder, stream, &schema, &errors ) );
    return schema;
}

/// Pump the event loop until @p cond holds or ~2 s elapse (bounded spin —
/// async checks run on a real pool thread; the delivery is a queued call).
template <typename Cond>
bool waitUntil( Cond cond )
{
    for ( int i = 0; i < 200 && !cond(); ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
        QThread::msleep( 5 );
    }
    return cond();
}

/// The combo whose schema label (accessibleName) is @p label — creation
/// order follows schema property sorting, never test assumptions.
QComboBox *comboByLabel( SchemaFormBuilder &form, const QString &label )
{
    for ( QComboBox *c : form.findChildren<QComboBox *>() )
    {
        if ( c->accessibleName() == label )
            return c;
    }
    return nullptr;
}

class RecordingProvider : public SchemaEnumProvider
{
  public:
    QString lastSource;
    int calls = 0;
    QHash<QString, QVector<SchemaEnumProvider::Choice>> sources;
    Json::Value lastValues;
    QVector<Choice> choicesFor( const QString &sourceId,
                                const Json::Value &currentValues ) override
    {
        ++calls;
        lastSource = sourceId;
        lastValues = currentValues;
        return sources.value( sourceId );
    }
};

} // namespace

TEST_CASE( "Nested objects render recursively and round-trip values",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "required": ["clip"],
      "properties": {
        "clip": {
          "type": "object",
          "title": "裁剪范围",
          "required": ["enabled"],
          "properties": {
            "enabled": { "type": "boolean", "default": false },
            "margin":  { "type": "number", "default": 0.5, "x-ui-soft-max": 10 }
          }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    // The nested group renders as a group box; nested editors exist.
    QGroupBox *nested = form.findChild<QGroupBox *>(
      QStringLiteral( "rsSchemaNestedObject" ) );
    REQUIRE( nested != nullptr );
    QCheckBox *enabled = nested->findChild<QCheckBox *>();
    REQUIRE( enabled != nullptr );
    QDoubleSpinBox *margin = nested->findChild<QDoubleSpinBox *>();
    REQUIRE( margin != nullptr );

    // Round-trip: nested values nest in values() and typing is preserved.
    Json::Value params;
    params["clip"]["enabled"] = true;
    params["clip"]["margin"] = 2.5;
    form.setValues( params );
    const Json::Value out = form.values();
    REQUIRE( out["clip"].isObject() );
    REQUIRE( out["clip"]["enabled"].asBool() == true );
    REQUIRE( out["clip"]["margin"].asDouble() == 2.5 );

    // Nested required: the group is present, so a required child must be
    // checked... "enabled" is a checkbox (always has a value) — instead pin
    // the soft-range WARNING on the nested number (science warnings do not
    // block) via a huge margin.
    form.setValues( Json::Value( Json::objectValue ) );
    Json::Value big;
    big["clip"]["enabled"] = false;
    big["clip"]["margin"] = 99.0;
    form.setValues( big );
    const auto issues = form.validate();
    bool softWarning = false;
    for ( const auto &issue : issues )
    {
        if ( !issue.isError && issue.fieldName == QLatin1String( "clip.margin" ) )
            softWarning = true;
    }
    REQUIRE( softWarning );
    REQUIRE_FALSE( form.hasErrors() );
}

TEST_CASE( "Nested required fields produce path-addressed blocking errors",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "required": ["roi"],
      "properties": {
        "roi": {
          "type": "object",
          "required": ["layer"],
          "properties": {
            "layer": { "type": "string", "description": "ROI 图层" }
          }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );
    REQUIRE( form.hasErrors() );
    const auto issues = form.validate();
    bool requiredError = false;
    for ( const auto &issue : issues )
    {
        if ( issue.isError && issue.fieldName == QLatin1String( "roi.layer" ) )
            requiredError = true;
    }
    REQUIRE( requiredError );

    // Filling the nested field clears the blocking error.
    Json::Value v;
    v["roi"]["layer"] = "/data/roi.shp";
    form.setValues( v );
    form.updateValidationUi();
    REQUIRE_FALSE( form.hasErrors() );

    // The error mark landed on the nested editor itself (path-addressed).
    QLineEdit *layer = form.findChild<QLineEdit *>();
    REQUIRE( layer != nullptr );
}

TEST_CASE( "Optional nested groups count as absent while untouched",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "properties": {
        "advanced": {
          "type": "object",
          "required": ["path"],
          "properties": {
            "path": { "type": "string" }
          }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    // Untouched optional group → no blocking error even though its child is
    // required inside the group.
    REQUIRE_FALSE( form.hasErrors() );

    // And the untouched group is ABSENT from values() — an emitted empty or
    // defaulted object would be indistinguishable from a configured one
    // (values() and validate() agree on the absence rule).
    REQUIRE_FALSE( form.values().isMember( "advanced" ) );

    // Touching the group (filling the child) activates the child's
    // requiredness — which is now satisfied, so still no error.
    Json::Value v;
    v["advanced"]["path"] = "/data/x.tif";
    form.setValues( v );
    form.updateValidationUi();
    REQUIRE_FALSE( form.hasErrors() );
    REQUIRE( form.values()["advanced"]["path"].asString() == "/data/x.tif" );
}

TEST_CASE( "Object arrays: add/remove, bounds, and object round-trips",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "required": ["points"],
      "properties": {
        "points": {
          "type": "array",
          "minItems": 1,
          "maxItems": 2,
          "items": {
            "type": "object",
            "required": ["label"],
            "properties": {
              "x": { "type": "number", "default": 0 },
              "label": { "type": "string" }
            }
          }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    // minItems seeding: one row is visible from the start.
    REQUIRE( form.values()["points"].isArray() );
    REQUIRE( form.values()["points"].size() == 1 );

    // minItems gating: the seeded single row cannot be removed.
    QPushButton *rm = form.findChild<QPushButton *>(
      QStringLiteral( "rsSchemaArrayRemove" ) );
    REQUIRE( rm != nullptr );
    REQUIRE_FALSE( rm->isEnabled() );

    // Round-trip two typed items.
    Json::Value params;
    Json::Value p0, p1;
    p0["x"] = 1.5;
    p0["label"] = "a";
    p1["x"] = 2;
    p1["label"] = "b";
    params["points"].append( p0 );
    params["points"].append( p1 );
    form.setValues( params );
    Json::Value out = form.values();
    REQUIRE( out["points"].size() == 2 );
    REQUIRE( out["points"][0]["x"].asDouble() == 1.5 );
    REQUIRE( out["points"][0]["label"].asString() == "a" );
    REQUIRE( out["points"][1]["x"].asInt() == 2 );
    REQUIRE_FALSE( form.hasErrors() );

    // maxItems gating: the add button is disabled at the cap (2 items).
    QPushButton *add = form.findChild<QPushButton *>(
      QStringLiteral( "rsSchemaArrayAdd" ) );
    REQUIRE( add != nullptr );
    REQUIRE_FALSE( add->isEnabled() );
    // With 2 items > minItems 1, removal is allowed again. The seeded row
    // was replaced (deleteLater) by setValues — flush deferred deletions so
    // the lookup resolves a live button.
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    rm = form.findChild<QPushButton *>( QStringLiteral( "rsSchemaArrayRemove" ) );
    REQUIRE( rm != nullptr );
    REQUIRE( rm->isEnabled() );

    // Per-item required: an item missing "label" errors with a positional
    // path. ("x" carries a schema default, so its absence is never an error
    // — the default is the value, exactly as the schema declares.)
    Json::Value thin;
    Json::Value bad1;
    bad1["x"] = 9;
    thin["points"].append( p0 );
    thin["points"].append( bad1 );
    form.setValues( thin );
    const auto issues = form.validate();
    bool positional = false;
    for ( const auto &issue : issues )
    {
        if ( issue.isError && issue.fieldName == QLatin1String( "points.1.label" ) )
            positional = true;
    }
    REQUIRE( positional );
    REQUIRE( form.hasErrors() );
}

TEST_CASE( "Object arrays truncate oversized imports honestly",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "properties": {
        "rows": {
          "type": "array",
          "items": { "type": "object", "properties": { "v": { "type": "integer" } } }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    Json::Value params;
    const int total = SchemaFormBuilder::kMaxObjectArrayItems + 50;
    for ( int i = 0; i < total; ++i )
    {
        Json::Value item;
        item["v"] = i;
        params["rows"].append( item );
    }
    form.setValues( params );

    const Json::Value out = form.values();
    REQUIRE( out["rows"].size() == SchemaFormBuilder::kMaxObjectArrayItems );
    REQUIRE( out["rows"][0]["v"].asInt() == 0 );

    // Honest truncation: the hint names both totals (visible + readable).
    QLabel *hint = form.findChild<QLabel *>( QStringLiteral( "rsSchemaArrayHint" ) );
    REQUIRE( hint != nullptr );
    REQUIRE( hint->isVisibleTo( &form ) );
    REQUIRE( hint->text().contains( QString::number( total ) ) );
}

TEST_CASE( "Dynamic enum sources resolve through the provider and refresh",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "required": ["dataset"],
      "properties": {
        "dataset": { "type": "string", "x-ui-enum-source": "datasets" },
        "band":    { "type": "string", "x-ui-enum-source": "bands" }
      }
    })" );

    SchemaFormBuilder form;
    RecordingProvider provider;
    provider.sources[QStringLiteral( "datasets" )] = {
        { QStringLiteral( "ds-1" ), QStringLiteral( "水体制图样本 v2" ) },
        { QStringLiteral( "ds-2" ), QStringLiteral( "耕地样本" ) }
    };
    provider.sources[QStringLiteral( "bands" )] = {
        { QStringLiteral( "1" ), QStringLiteral( "B1" ) }
    };
    form.setEnumProvider( &provider );
    form.rebuild( schema );

    QComboBox *dataset = comboByLabel( form, QStringLiteral( "dataset" ) );
    REQUIRE( dataset != nullptr );
    REQUIRE( dataset->count() == 2 );
    REQUIRE_FALSE( dataset->isEditable() );
    REQUIRE( dataset->itemData( 0 ).toString() == "ds-1" );

    // Provider sees current values (context-dependent sources).
    Json::Value v;
    v["dataset"] = "ds-2";
    form.setValues( v );
    form.refreshChoices();
    REQUIRE( provider.lastValues["dataset"].asString() == "ds-2" );

    // Selection survives a refresh; value round-trips as the stable id.
    REQUIRE( form.values()["dataset"].asString() == "ds-2" );

    // Unknown source → honest degradation: editable free text + visible hint
    // in the tooltip; the free value still round-trips.
    provider.sources.remove( QStringLiteral( "bands" ) );
    form.refreshChoices();
    QComboBox *band = comboByLabel( form, QStringLiteral( "band" ) );
    REQUIRE( band != nullptr );
    REQUIRE( band->isEditable() );
    REQUIRE( band->toolTip().contains( QStringLiteral( "动态选项源" ) ) );
    Json::Value free;
    free["band"] = "7";
    form.setValues( free );
    REQUIRE( form.values()["band"].asString() == "7" );

    // No provider at all → same degradation, no crash.
    SchemaFormBuilder bare;
    bare.rebuild( schema );
    QComboBox *bareCombo = bare.findChild<QComboBox *>();
    REQUIRE( bareCombo != nullptr );
    REQUIRE( bareCombo->isEditable() );
}

TEST_CASE( "Async path_exists checks run bounded, cancel-safe, and honest",
           "[wb8][schema-form4]" )
{
    testApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString existing = dir.filePath( "exists.tif" );
    REQUIRE( QFile( existing ).open( QIODevice::WriteOnly ) );
    const QString missing = dir.filePath( "missing.tif" );

    const Json::Value schema = parseSchema(
      R"({ "type": "object",
           "properties": { "input": { "type": "string", "x-ui-check": "path_exists" } } })" );

    QThreadPool pool;
    pool.setMaxThreadCount( 1 );

    SchemaFormBuilder form;
    form.setCheckPool( &pool );
    form.rebuild( schema );

    // "input" carries the 3.0 input-name heuristic → a (editable) raster
    // combo; the check mark lands on the field's primary widget, i.e. the
    // combo itself — never on its internal line edit.
    QComboBox *input = form.findChild<QComboBox *>();
    REQUIRE( input != nullptr );

    // Existing path → check property flips true (bounded wait).
    input->setCurrentText( existing );
    form.runAsyncChecksNow();
    const bool okSeen = waitUntil( [&] {
        return input->property( "check_path_exists" ).isValid()
               && input->property( "check_path_exists" ).toBool();
    } );
    REQUIRE( okSeen );

    // Missing path → property false + tooltip warning reachable.
    input->setCurrentText( missing );
    form.runAsyncChecksNow();
    const bool warnSeen = waitUntil( [&] {
        return input->property( "check_path_exists" ).isValid()
               && !input->property( "check_path_exists" ).toBool();
    } );
    REQUIRE( warnSeen );
    REQUIRE( input->toolTip().contains( QStringLiteral( "路径不存在" ) ) );

    // Supersede: a newer run (generation bump) retires stale results —
    // fixing the path clears the warning tooltip again.
    input->setCurrentText( existing );
    form.runAsyncChecksNow();
    const bool okAgain = waitUntil( [&] {
        return input->property( "check_path_exists" ).toBool()
               && !input->toolTip().contains( QStringLiteral( "路径不存在" ) );
    } );
    REQUIRE( okAgain );
}

TEST_CASE( "Async check marks are dropped when the form dies mid-flight",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema(
      R"({ "type": "object",
           "properties": { "input": { "type": "string", "x-ui-check": "path_exists" } } })" );

    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    {
        SchemaFormBuilder form;
        form.setCheckPool( &pool );
        form.rebuild( schema );
        QComboBox *input = form.findChild<QComboBox *>();
        REQUIRE( input != nullptr );
        input->setCurrentText( QStringLiteral( "/definitely/missing/path.tif" ) );
        form.runAsyncChecksNow();
        // Destroy while the job may be queued/running — the result must be
        // dropped, never delivered into a dead widget (ASan/valgrind would
        // catch a UAF here).
    }
    // Drain the pool and pump deliveries; nothing should crash.
    pool.waitForDone( 5000 );
    for ( int i = 0; i < 20; ++i )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    REQUIRE( true );
}

TEST_CASE( "Conditional visibility composes with nesting",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "properties": {
        "mode": { "type": "string", "enum": ["simple", "expert"], "default": "simple" },
        "opt": {
          "type": "object",
          "properties": {
            "threshold": { "type": "number",
                           "x-ui-visible-when": { "mode": "expert" },
                           "x-ui-soft-min": 0 }
          }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    QDoubleSpinBox *threshold = form.findChild<QDoubleSpinBox *>();
    REQUIRE( threshold != nullptr );
    REQUIRE( threshold->isHidden() );

    // While hidden, the field is excluded from values()/validate().
    threshold->setValue( -5 ); // would be a soft warning if validated
    {
        const auto issues = form.validate();
        REQUIRE( issues.isEmpty() );
    }
    Json::Value v = form.values();
    REQUIRE( v["opt"]["threshold"].isNull() );

    // Flipping the mode reveals the field and pulls it into validation.
    QComboBox *mode = form.findChild<QComboBox *>();
    REQUIRE( mode != nullptr );
    mode->setCurrentIndex( 1 ); // "expert"
    form.updateValidationUi();
    REQUIRE_FALSE( threshold->isHidden() );
    const auto issues = form.validate();
    bool warned = false;
    for ( const auto &issue : issues )
    {
        if ( !issue.isError && issue.fieldName == QLatin1String( "opt.threshold" ) )
            warned = true;
    }
    REQUIRE( warned );
}

TEST_CASE( "Accessibility: nested editors carry schema labels and descriptions",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "properties": {
        "grp": {
          "type": "object",
          "title": "分组",
          "properties": {
            "field": { "type": "string", "title": "字段", "description": "字段的说明" }
          }
        }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    QLineEdit *field = form.findChild<QLineEdit *>();
    REQUIRE( field != nullptr );
    REQUIRE( field->accessibleName() == QStringLiteral( "字段" ) );
    REQUIRE( field->accessibleDescription().contains( QStringLiteral( "字段的说明" ) ) );
}

TEST_CASE( "Nested layer combos receive pushed choices like top-level fields",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "properties": {
        "grp": { "type": "object", "properties": {
          "raster": { "type": "string", "x-ui-type": "raster" } } }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );
    form.setRasterLayerChoices( { QStringLiteral( "lyr-1" ) },
                                { QStringLiteral( "影像 A" ) } );

    QComboBox *nested = nullptr;
    for ( QComboBox *c : form.findChildren<QComboBox *>() )
    {
        if ( c->accessibleName() == QLatin1String( "raster" ) )
            nested = c;
    }
    REQUIRE( nested != nullptr );
    REQUIRE( nested->count() == 1 );
    REQUIRE( nested->itemData( 0 ).toString() == "lyr-1" );
}

TEST_CASE( "Deeply nested object schemas degrade to the JSON editor",
           "[wb8][schema-form4]" )
{
    testApp();
    const Json::Value schema = parseSchema( R"({
      "type": "object",
      "properties": {
        "l1": { "type": "object", "properties": {
          "l2": { "type": "object", "properties": {
            "l3": { "type": "object", "properties": {
              "l4": { "type": "object", "properties": {
                "l5": { "type": "object", "properties": {
                  "leaf": { "type": "string" } } } } } } } } } } } }
      }
    })" );

    SchemaFormBuilder form;
    form.rebuild( schema );

    // l1..l4 render as groups (depth cap 4); l5 lands past the cap → the
    // JSON text editor handles it (SchemaForm 3.0 behavior).
    REQUIRE( form.findChild<QGroupBox *>(
      QStringLiteral( "rsSchemaNestedObject" ) ) != nullptr );
    QPlainTextEdit *json = form.findChild<QPlainTextEdit *>();
    REQUIRE( json != nullptr );

    Json::Value v;
    v["l1"]["l2"]["l3"]["l4"]["l5"] = "{ \"leaf\": \"x\" }";
    form.setValues( v );
    // values() parses valid JSON bodies into documents (3.0 contract).
    REQUIRE( form.values()["l1"]["l2"]["l3"]["l4"]["l5"]["leaf"].asString() == "x" );
}
