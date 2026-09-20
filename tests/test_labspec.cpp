// test_labspec.cpp — LabSpec drift guards
//
// Keeps the shipped lab JSONs honest against the rest of the system:
//   * every lab file parses and validates (labspec.schema.json rules, enforced
//     by the loader);
//   * every step operator_id resolves in the Processing Registry;
//   * every step's params validate against the operator descriptor's input
//     ports (the shared validateParameters seam — same check the ToolCall
//     dispatcher applies to agent callers);
//   * every grading_ref resolves to an existing pipeline file;
//   * committed markdown equals generated markdown (docs are build output).
#include <catch2/catch_test_macros.hpp>

#include <app/widgets/lab_spec_loader.h>
#include <processing/framework/atomic_algorithm_registry.h>
#include <processing/framework/schema_validator.h>
#include <operators/framework/rs_operator_registry.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <algorithm>

using namespace sicnu::processing;

namespace {

QString labsRoot()
{
    return QStringLiteral( CMAKE_SOURCE_DIR );
}

QList<lab::LabSpec> loadShippedLabs( lab::LabLoadResult *resultOut = nullptr )
{
    const QString dir = labsRoot() + QStringLiteral( "/data/labs" );
    lab::LabLoadResult result = lab::loadLabSpecsFromDir( dir );
    if ( resultOut )
        *resultOut = result;
    return result.labs;
}

} // namespace

TEST_CASE( "Shipped lab inventory loads cleanly", "[labspec][drift]" )
{
    lab::LabLoadResult result;
    const auto labs = loadShippedLabs( &result );

    // The canonical set reconciles the 7 documented labs with the 10 former
    // hardcoded workflows — the union is 11; the gate is ">= 10".
    REQUIRE( labs.size() >= 10 );
    INFO( lab::errorStrings( result ).join( QStringLiteral( "; " ) ).toStdString() );
    REQUIRE( result.ok() );

    // Lab ids are unique and match their file names (loader enforces both,
    // but pin the invariant explicitly).
    QStringList ids;
    for ( const auto &lab : labs )
        ids << lab.id;
    REQUIRE( ids.size() == QSet<QString>( ids.cbegin(), ids.cend() ).size() );

    for ( const auto &lab : labs )
    {
        INFO( lab.id.toStdString() );
        REQUIRE( !lab.titleZh.isEmpty() );
        REQUIRE( !lab.objective.isEmpty() );
        REQUIRE( !lab.steps.isEmpty() );
    }
}

TEST_CASE( "Every lab operator_id resolves in the Processing Registry", "[labspec][drift]" )
{
    sicnu::operators::RSOperatorRegistry::instance();
    AtomicAlgorithmRegistry::instance().initialize();
    auto &registry = AtomicAlgorithmRegistry::instance();

    const auto labs = loadShippedLabs();
    REQUIRE( labs.size() >= 10 );

    for ( const auto &lab : labs )
    {
        for ( int i = 0; i < lab.steps.size(); ++i )
        {
            const auto &step = lab.steps[i];
            if ( !step.hasOperator() )
                continue;
            INFO( lab.id.toStdString() << " step " << i + 1 << " operator " << step.operatorId.toStdString() );
            auto adapter = registry.findAdapter( step.operatorId.toStdString() );
            REQUIRE( adapter != nullptr );
        }
    }
}

TEST_CASE( "Every lab step's params match the operator schema", "[labspec][drift]" )
{
    sicnu::operators::RSOperatorRegistry::instance();
    AtomicAlgorithmRegistry::instance().initialize();
    auto &registry = AtomicAlgorithmRegistry::instance();

    const auto labs = loadShippedLabs();
    for ( const auto &lab : labs )
    {
        for ( int i = 0; i < lab.steps.size(); ++i )
        {
            const auto &step = lab.steps[i];
            if ( !step.hasOperator() )
                continue;
            auto adapter = registry.findAdapter( step.operatorId.toStdString() );
            REQUIRE( adapter != nullptr );
            const AlgorithmDescriptor desc = adapter->descriptor();
            const auto validationResult = validateParameters( step.params, desc, UnknownParameterPolicy::Error );
            INFO( lab.id.toStdString() << " step " << i + 1 << " ("
                  << step.operatorId.toStdString() << "): "
                  << [&]
                  {
                      std::string messages;
                      for ( const auto &issue : validationResult.errors )
                          messages += issue.message + "; ";
                      return messages;
                  }( ) );
            REQUIRE( validationResult.ok() );
        }
    }
}

TEST_CASE( "Every grading_ref resolves to an existing pipeline", "[labspec][drift]" )
{
    const auto labs = loadShippedLabs();
    for ( const auto &lab : labs )
    {
        if ( lab.gradingPipeline.isEmpty() )
            continue;
        INFO( lab.id.toStdString() << " -> " << lab.gradingPipeline.toStdString() );
        REQUIRE( QFile::exists( labsRoot() + QLatin1Char( '/' ) + lab.gradingPipeline ) );
    }
}

TEST_CASE( "Generated lab documentation is in sync (zero diff)", "[labspec][drift][docs]" )
{
    QProcess gen;
    gen.setWorkingDirectory( labsRoot() );
    gen.setProgram( QStringLiteral( "python3" ) );
    gen.setArguments( { labsRoot() + QStringLiteral( "/scripts/gen_lab_docs.py" ),
                        QStringLiteral( "--check" ) } );
    gen.start();
    REQUIRE( gen.waitForStarted( 10000 ) );
    REQUIRE( gen.waitForFinished( 120000 ) );
    const QByteArray out = gen.readAllStandardOutput() + gen.readAllStandardError();
    INFO( out.toStdString() );
    REQUIRE( gen.exitCode() == 0 );
}

// ---------------------------------------------------------------------------
// LabSpec 2 (spec_version 2) — version negotiation and v2 field validation
// (lab platform 12.0). The v1 set is exercised by the drift guards above;
// these cases pin the versioned contract from docs/labs/LABSPEC.md.
// ---------------------------------------------------------------------------

namespace {

/// Minimal v1 document with an optional extra member applied by the caller.
QString writeTempSpec( const QDir &dir, const QVariantMap &extra,
                       const QString &name = QStringLiteral( "lab99_v2_probe.lab.json" ) )
{
    const QByteArray doc = R"({
  "spec_version": 1,
  "id": "lab99_v2_probe",
  "title": "V2 Probe",
  "title_zh": "版本探测",
  "objective": "probe",
  "steps": [
    { "title": "Look", "title_zh": "观察", "description_zh": "看影像。",
      "action": "addRasterLayer" }
  ]
})";
    QJsonDocument parsed = QJsonDocument::fromJson( doc );
    QJsonObject object = parsed.object();
    for ( auto it = extra.begin(); it != extra.end(); ++it )
        object.insert( it.key(), QJsonValue::fromVariant( it.value() ) );
    const QString path = dir.filePath( name );
    QFile file( path );
    file.open( QIODevice::WriteOnly );
    file.write( QJsonDocument( object ).toJson() );
    return path;
}

lab::LabSpecError loadOne( const QString &path )
{
    lab::LabSpecError error;
    lab::loadLabSpecFile( path, &error );
    return error;
}

} // namespace

TEST_CASE( "Loader accepts spec_version 1 and 2", "[labspec][v2]" )
{
    QTemporaryDir dir;
    const QString path = writeTempSpec( QDir( dir.path() ), {} );
    lab::LabSpecError error;
    lab::LabSpec spec = lab::loadLabSpecFile( path, &error );
    INFO( error.reason.toStdString() );
    REQUIRE( error.reason.isEmpty() );
    REQUIRE( spec.id == QStringLiteral( "lab99_v2_probe" ) );
}

TEST_CASE( "v2-only keys in a v1 document are rejected (version-strict)",
           "[labspec][v2]" )
{
    for ( const char *key : { "objective_zh", "glossary", "expected_artifacts",
                              "param_ranges", "grading_rules", "principles",
                              "prerequisite_knowledge" } )
    {
        QTemporaryDir dir;
        const QString path = writeTempSpec(
            QDir( dir.path() ), { { key, QVariantMap {} } } );
        const lab::LabSpecError error = loadOne( path );
        INFO( key << ": " << error.reason.toStdString() );
        REQUIRE( error.reason.contains( QStringLiteral( "requires spec_version 2" ) ) );
    }
}

TEST_CASE( "Unsupported spec_version is refused with a typed message",
           "[labspec][v2]" )
{
    QTemporaryDir dir;
    const QString path = writeTempSpec( QDir( dir.path() ),
                                        { { "spec_version", 3 } } );
    const lab::LabSpecError error = loadOne( path );
    REQUIRE( error.reason.contains( QStringLiteral( "unsupported spec_version 3" ) ) );
}

TEST_CASE( "LabSpec 2 structured fields validate", "[labspec][v2]" )
{
    // A fully-populated v2 document loads cleanly.
    {
        QTemporaryDir dir;
        const QString path = writeTempSpec( QDir( dir.path() ), {
            { "spec_version", 2 },
            { "objective_zh", "中文目标" },
            { "prerequisite_knowledge", QVariantList { "lab01（先修）" } },
            { "principles", QVariantList { QVariantMap {
                { "heading", "原理" }, { "body", "正文" },
                { "formulas", QVariantList { "NDVI = (NIR - Red) / (NIR + Red)" } } } } },
            { "glossary", QVariantList { QVariantMap {
                { "term", "NDVI" }, { "term_zh", "归一化植被指数" },
                { "definition_zh", "定义" } } } },
            { "expected_artifacts", QVariantList { QVariantMap {
                { "path", "outputs/lab99/out.tif" }, { "kind", "raster" },
                { "note_zh", "成果" } } } },
            { "param_ranges", QVariantMap {
                { "rs:spectral_index", QVariantMap {
                    { "red", QVariantMap { { "min", 1 }, { "max", 7 } } } } } } },
            { "grading_rules", "data/labs/grading/ndvi_basics.rules.json" },
        } );
        lab::LabSpecError error;
        lab::loadLabSpecFile( path, &error );
        INFO( error.reason.toStdString() );
        REQUIRE( error.reason.isEmpty() );
    }

    struct BadCase
    {
        const char *label;
        QVariantMap extra;
        QString fragment;
    };
    const std::vector<BadCase> bad = {
        { "empty objective_zh",
          { { "spec_version", 2 }, { "objective_zh", "" } },
          QStringLiteral( "objective_zh" ) },
        { "glossary entry missing definition",
          { { "spec_version", 2 },
            { "glossary", QVariantList { QVariantMap {
                { "term", "t" }, { "term_zh", "t" } } } } },
          QStringLiteral( "glossary" ) },
        { "unknown principles key",
          { { "spec_version", 2 },
            { "principles", QVariantList { QVariantMap {
                { "heading", "h" }, { "body", "b" }, { "nope", 1 } } } } },
          QStringLiteral( "unknown principles key" ) },
        { "artifact bad kind",
          { { "spec_version", 2 },
            { "expected_artifacts", QVariantList { QVariantMap {
                { "path", "outputs/x.tif" }, { "kind", "mesh" } } } } },
          QStringLiteral( "raster|vector|file" ) },
        { "param range empty",
          { { "spec_version", 2 },
            { "param_ranges", QVariantMap {
                { "rs:spectral_index", QVariantMap {
                    { "red", QVariantMap { { "note_zh", "只有备注" } } } } } } } },
          QStringLiteral( "needs min, max or values" ) },
        { "param range min exceeds max",
          { { "spec_version", 2 },
            { "param_ranges", QVariantMap {
                { "rs:spectral_index", QVariantMap {
                    { "red", QVariantMap { { "min", 7 }, { "max", 1 } } } } } } } },
          QStringLiteral( "min exceeds max" ) },
        { "param_ranges key not an operator id",
          { { "spec_version", 2 },
            { "param_ranges", QVariantMap {
                { "spectral_index", QVariantMap {} } } } },
          QStringLiteral( "not an operator id" ) },
        { "empty prerequisite_knowledge entry",
          { { "spec_version", 2 },
            { "prerequisite_knowledge", QVariantList { "" } } },
          QStringLiteral( "prerequisite_knowledge entries must be non-empty strings" ) },
    };
    for ( const BadCase &item : bad )
    {
        QTemporaryDir dir;
        const QString path = writeTempSpec( QDir( dir.path() ), item.extra );
        const lab::LabSpecError error = loadOne( path );
        INFO( item.label << ": " << error.reason.toStdString() );
        REQUIRE_FALSE( error.reason.isEmpty() );
        REQUIRE( error.reason.contains( item.fragment ) );
    }
}
