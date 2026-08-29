// tests/test_atomic_registry_contract.cpp
//
// Canonical registry contract: unique IDs, stable descriptors, explicit
// initialize(), legacy facade compatibility, and composition (an atomic
// primitive's output feeds the next algorithm as a valid input).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/schema_validator.h"
#include "operators/rs/rs_change_primitives.h"
#include "operators/rs/rs_threshold_raster_operator.h"

#include <set>
#include <string>
#include <QTemporaryDir>

#include "operators/framework/rs_operator_registry.h"
#include <iostream>

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

TEST_CASE( "Registry exposes unique ids and stable descriptors", "[processing][registry][contract]" )
{
    sicnu::operators::RSOperatorRegistry::instance();
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const auto descriptors = registry.listDescriptors();
    REQUIRE( descriptors.size() >= 40 );

    std::set<std::string> ids;
    for ( const auto &d : descriptors )
        REQUIRE( ids.insert( d.id ).second ); // unique

    // Descriptors are stable across calls (no per-call mutation).
    const AlgorithmDescriptor first = registry.findAdapter( "rs:change_difference" )->descriptor();
    const AlgorithmDescriptor second = registry.findAdapter( "rs:change_difference" )->descriptor();
    REQUIRE( first.id == second.id );
    REQUIRE( first.outputs.size() == second.outputs.size() );
    REQUIRE( first.agentMetadata.memoryPolicy == second.agentMetadata.memoryPolicy );
    // ADR 0124: operator descriptors surface a determinism grade through the
    // same metadata surface (default bit_exact — the serial baseline).
    REQUIRE( first.agentMetadata.determinismGrade == "bit_exact" );
}

TEST_CASE( "Legacy facade IDs remain registered and executable", "[processing][registry][compat]" )
{
    sicnu::operators::RSOperatorRegistry::instance();
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    // The legacy multi-method facade is still present alongside the primitives.
    REQUIRE( registry.findAdapter( "rs:change_detection" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:change_difference" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:image_fusion" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:fusion_linear" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:fusion_gram_schmidt" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:threshold_raster" ) != nullptr );

    // Atmospheric correction facade and atomic operators
    REQUIRE( registry.findAdapter( "rs:atmospheric_correction" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:dn_to_radiance" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:atmospheric_dos1" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:atmospheric_dos2" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:atmospheric_quac" ) != nullptr );

    // Spectral index facade and atomic operators
    REQUIRE( registry.findAdapter( "rs:spectral_index" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:ndvi" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:evi" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:ndwi" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:savi" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:ndbi" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:mndwi" ) != nullptr );

    // Facade metadata points at its primitives.
    const auto changeFacadeDesc = registry.findAdapter( "rs:change_detection" )->descriptor();
    REQUIRE( changeFacadeDesc.agentMetadata.facadeOf.find( "rs:change_difference" ) != std::string::npos );
    const auto changePrimDesc = registry.findAdapter( "rs:change_difference" )->descriptor();
    REQUIRE( changePrimDesc.agentMetadata.facadeOf == "change_detection" );

    // Atmospheric correction facadeOf contract
    const auto atmosFacadeDesc = registry.findAdapter( "rs:atmospheric_correction" )->descriptor();
    REQUIRE( atmosFacadeDesc.agentMetadata.facadeOf.find( "rs:dn_to_radiance" ) != std::string::npos );
    REQUIRE( atmosFacadeDesc.agentMetadata.facadeOf.find( "rs:atmospheric_dos1" ) != std::string::npos );
    REQUIRE( atmosFacadeDesc.agentMetadata.facadeOf.find( "rs:atmospheric_dos2" ) != std::string::npos );
    REQUIRE( atmosFacadeDesc.agentMetadata.facadeOf.find( "rs:atmospheric_quac" ) != std::string::npos );

    const auto dnToRadDesc = registry.findAdapter( "rs:dn_to_radiance" )->descriptor();
    REQUIRE( dnToRadDesc.agentMetadata.facadeOf == "atmospheric_correction" );
    const auto dos1Desc = registry.findAdapter( "rs:atmospheric_dos1" )->descriptor();
    REQUIRE( dos1Desc.agentMetadata.facadeOf == "atmospheric_correction" );
    const auto dos2Desc = registry.findAdapter( "rs:atmospheric_dos2" )->descriptor();
    REQUIRE( dos2Desc.agentMetadata.facadeOf == "atmospheric_correction" );
    const auto quacDesc = registry.findAdapter( "rs:atmospheric_quac" )->descriptor();
    REQUIRE( quacDesc.agentMetadata.facadeOf == "atmospheric_correction" );

    // Spectral index facadeOf contract
    const auto spectralFacadeDesc = registry.findAdapter( "rs:spectral_index" )->descriptor();
    REQUIRE( spectralFacadeDesc.agentMetadata.facadeOf.find( "rs:ndvi" ) != std::string::npos );
    REQUIRE( spectralFacadeDesc.agentMetadata.facadeOf.find( "rs:evi" ) != std::string::npos );
    REQUIRE( spectralFacadeDesc.agentMetadata.facadeOf.find( "rs:ndwi" ) != std::string::npos );
    REQUIRE( spectralFacadeDesc.agentMetadata.facadeOf.find( "rs:savi" ) != std::string::npos );
    REQUIRE( spectralFacadeDesc.agentMetadata.facadeOf.find( "rs:ndbi" ) != std::string::npos );
    REQUIRE( spectralFacadeDesc.agentMetadata.facadeOf.find( "rs:mndwi" ) != std::string::npos );

    const auto ndviDesc = registry.findAdapter( "rs:ndvi" )->descriptor();
    REQUIRE( ndviDesc.agentMetadata.facadeOf == "spectral_index" );
    const auto eviDesc = registry.findAdapter( "rs:evi" )->descriptor();
    REQUIRE( eviDesc.agentMetadata.facadeOf == "spectral_index" );
    const auto ndwiDesc = registry.findAdapter( "rs:ndwi" )->descriptor();
    REQUIRE( ndwiDesc.agentMetadata.facadeOf == "spectral_index" );
    const auto saviDesc = registry.findAdapter( "rs:savi" )->descriptor();
    REQUIRE( saviDesc.agentMetadata.facadeOf == "spectral_index" );
    const auto ndbiDesc = registry.findAdapter( "rs:ndbi" )->descriptor();
    REQUIRE( ndbiDesc.agentMetadata.facadeOf == "spectral_index" );
    const auto mndwiDesc = registry.findAdapter( "rs:mndwi" )->descriptor();
    REQUIRE( mndwiDesc.agentMetadata.facadeOf == "spectral_index" );
}

TEST_CASE( "Composition: primitive output satisfies the next algorithm's input contract", "[processing][registry][compose]" )
{
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const auto diffDesc = registry.findAdapter( "rs:change_difference" )->descriptor();
    const auto thresholdDesc = registry.findAdapter( "rs:threshold_raster" )->descriptor();

    // The change primitive produces a Raster output named "output".
    const PortDescriptor *diffOut = nullptr;
    for ( const auto &out : diffDesc.outputs )
        if ( out.name == "output" && out.type == DataType::Raster )
            diffOut = &out;
    REQUIRE( diffOut != nullptr );

    // The threshold operator consumes a Raster input named "input".
    const PortDescriptor *thresholdIn = nullptr;
    for ( const auto &in : thresholdDesc.inputs )
        if ( in.name == "input" && in.type == DataType::Raster )
            thresholdIn = &in;
    REQUIRE( thresholdIn != nullptr );

    // Feeding the change output path as the threshold input validates.
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    Json::Value params( Json::objectValue );
    params["input"] = tempDir.filePath("magnitude.tif").toStdString(); // shape-valid raster path
    params["output"] = tempDir.filePath("mask.tif").toStdString();
    const auto result = validateParameters( params, thresholdDesc );
    REQUIRE( result.ok() );

    // Composition chain: dn_to_radiance -> atmospheric_dos1 -> ndvi -> threshold_raster
    const auto dnDesc = registry.findAdapter( "rs:dn_to_radiance" )->descriptor();
    const auto dos1Desc = registry.findAdapter( "rs:atmospheric_dos1" )->descriptor();
    const auto ndviDesc = registry.findAdapter( "rs:ndvi" )->descriptor();

    auto findPort = []( const std::vector<PortDescriptor> &ports, const std::string &name ) -> const PortDescriptor* {
        for ( const auto &p : ports ) {
            if ( p.name == name ) return &p;
        }
        return nullptr;
    };

    // Check dn_to_radiance output "output" is Raster
    const auto *dnOut = findPort( dnDesc.outputs, "output" );
    REQUIRE( dnOut != nullptr );
    REQUIRE( dnOut->type == DataType::Raster );

    // Check atmospheric_dos1 input "input" is Raster and output "output" is Raster
    const auto *dos1In = findPort( dos1Desc.inputs, "input" );
    REQUIRE( dos1In != nullptr );
    REQUIRE( dos1In->type == DataType::Raster );
    const auto *dos1Out = findPort( dos1Desc.outputs, "output" );
    REQUIRE( dos1Out != nullptr );
    REQUIRE( dos1Out->type == DataType::Raster );

    // Check ndvi input "input" is Raster and output "output" is Raster
    const auto *ndviIn = findPort( ndviDesc.inputs, "input" );
    REQUIRE( ndviIn != nullptr );
    REQUIRE( ndviIn->type == DataType::Raster );
    const auto *ndviOut = findPort( ndviDesc.outputs, "output" );
    REQUIRE( ndviOut != nullptr );
    REQUIRE( ndviOut->type == DataType::Raster );

    // Validating ndvi params
    Json::Value ndviParams( Json::objectValue );
    ndviParams["input"] = tempDir.filePath("surface_reflectance.tif").toStdString();
    ndviParams["output"] = tempDir.filePath("ndvi.tif").toStdString();
    ndviParams["nir"] = 4;
    ndviParams["red"] = 3;
    const auto ndviValidation = validateParameters( ndviParams, ndviDesc );
    REQUIRE( ndviValidation.ok() );
}

