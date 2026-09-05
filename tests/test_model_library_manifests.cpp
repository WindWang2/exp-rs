// tests/test_model_library_manifests.cpp — Platform 3.0 model library gate:
// every shipped manifest must parse cleanly, declare a unique name, carry
// honest readiness (template weights are never committed), and keep contracts
// internally consistent (goal §11).
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>

#include "operators/framework/model_catalog.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::operators;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_model_library_manifests";
char *appArgv[] = { appArgv0, nullptr };

struct LibraryGuard
{
    LibraryGuard()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
#ifdef CMAKE_SOURCE_DIR
        {
            const QDir sourceModels(
                QDir( QString::fromUtf8( CMAKE_SOURCE_DIR ) ).filePath( QStringLiteral( "models" ) ) );
            if ( sourceModels.exists() )
            {
                ModelCatalog::instance().setDirectory(
                    sourceModels.absolutePath().toStdString() );
                ModelCatalog::instance().reload();
                return;
            }
        }
#endif
        ModelCatalog::instance().setDirectory( ModelCatalog::defaultModelsDirectory() );
        ModelCatalog::instance().reload();
    }
};

} // namespace

TEST_CASE( "Shipped model library parses without catalog issues", "[models][library]" )
{
    const LibraryGuard guard;
    const auto issues = ModelCatalog::instance().issues();
    INFO( issues.size() << " catalog issues" );
    for ( const auto &issue : issues )
        FAIL( issue.manifestPath + ": " + issue.message );
    REQUIRE( issues.empty() );
}

TEST_CASE( "Model library size meets the platform 3.0 bar", "[models][library]" )
{
    const LibraryGuard guard;
    const auto models = ModelCatalog::instance().models();
    // 2 baseline templates + the Platform 3.0 library.
    REQUIRE( models.size() >= 24 );
}

TEST_CASE( "Model names are unique across the library", "[models][library]" )
{
    const LibraryGuard guard;
    const auto models = ModelCatalog::instance().models();
    std::set<std::string> names;
    for ( const auto &m : models )
    {
        REQUIRE( names.insert( m.name ).second );
        REQUIRE( !m.task.empty() );
        REQUIRE( !m.description.empty() );
        REQUIRE( !m.tags.empty() );
    }
}

TEST_CASE( "Template manifests report missing weights, not invalid contracts",
           "[models][library]" )
{
    const LibraryGuard guard;
    const auto models = ModelCatalog::instance().models();
    for ( const auto &m : models )
    {
        INFO( m.name << " readiness: " << static_cast<int>( m.readiness ) << " " << m.readinessReason );
        // Weights are never committed: every shipped entry must be a template
        // whose only blocker is the missing artifact (never a broken contract).
        REQUIRE( m.readiness == ModelReadiness::MissingArtifact );
    }
}

TEST_CASE( "Model input contracts carry band roles and valid domains", "[models][library]" )
{
    const LibraryGuard guard;
    const auto models = ModelCatalog::instance().models();
    for ( const auto &m : models )
    {
        INFO( m.name );
        // Single- or multi-input: every declared input must name its band roles
        // (model-vs-data matching is the platform's ranking currency).
        for ( const auto &input : m.inputs )
            REQUIRE_FALSE( input.bandRoles.empty() );
        // Multi-input manifests (inputs > 1) must name every input.
        if ( m.inputs.size() > 1 )
        {
            for ( const auto &input : m.inputs )
                REQUIRE_FALSE( input.name.empty() );
        }
        // Modality vocabulary.
        for ( const auto &modality : m.modalities )
        {
            INFO( m.name << " modality " << modality );
            REQUIRE( ( modality == "optical" || modality == "sar" || modality == "dem"
                       || modality == "auxiliary" || modality == "multimodal" ) );
        }
        // Polarization vocabulary.
        for ( const auto &pol : m.polarizations )
        {
            INFO( m.name << " polarization " << pol );
            REQUIRE( ( pol == "VV" || pol == "VH" || pol == "HH" || pol == "HV" ) );
        }
        // Uncertainty vocabulary (engine consumes entropy/margin).
        if ( !m.output.uncertainty.empty() && m.output.uncertainty != "none" )
        {
            INFO( m.name << " uncertainty " << m.output.uncertainty );
            REQUIRE( ( m.output.uncertainty == "entropy" || m.output.uncertainty == "margin" ) );
        }
        // Every manifest documents its provenance/reference.
        REQUIRE( !m.sourceManifest.empty() );
    }
}

TEST_CASE( "Key platform families are covered by the library", "[models][library]" )
{
    const LibraryGuard guard;
    const auto models = ModelCatalog::instance().models();
    auto hasTask = [ & ]( const std::string &t ) {
        return std::any_of( models.begin(), models.end(),
                            [ & ]( const ModelInfo &m ) { return m.task == t; } );
    };
    auto hasTag = [ & ]( const std::string &t ) {
        return std::any_of( models.begin(), models.end(),
                            [ & ]( const ModelInfo &m ) {
                              return std::find( m.tags.begin(), m.tags.end(), t ) != m.tags.end();
                            } );
    };
    REQUIRE( hasTask( "segmentation" ) );
    REQUIRE( hasTask( "detection" ) );
    REQUIRE( hasTask( "classification" ) );
    REQUIRE( hasTask( "change_detection" ) );
    REQUIRE( hasTask( "embedding" ) );
    REQUIRE( hasTag( "sar" ) );            // SAR-specific models exist
    REQUIRE( hasTag( "temporal" ) );       // temporal (T-frame) models exist
    REQUIRE( hasTag( "siamese" ) );        // pair/change models exist
    // At least one optical-SAR fusion model (multi-modality inputs).
    const bool fusion = std::any_of( models.begin(), models.end(), []( const ModelInfo &m ) {
        return m.modalities.size() >= 2;
    } );
    REQUIRE( fusion );
}
