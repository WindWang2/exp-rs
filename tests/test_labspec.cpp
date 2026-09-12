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
#include <QProcess>
#include <QStringList>

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
