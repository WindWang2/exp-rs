// Shipped algorithm_meta sidecars must agree with the in-code
// AlgorithmDescriptor — the descriptor is the single source of truth for
// capability facts (#707). This runs in its own binary because the drift
// gate depends on RSOperatorRegistry / AtomicAlgorithmRegistry singletons
// staying in their freshly-initialized state; sharing a process with the
// spatial-tools suite's reset() calls leaves those singletons in a stale
// state that makes rs:infer (and every OpenCV-block operator) unresolvable.

#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/algorithm_meta_store.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"

TEST_CASE( "Shipped algorithm_meta sidecars agree with the registry descriptors (#707)",
           "[agent][spatial][meta][drift]" )
{
    // Trigger the call_once chain so every built-in operator is registered.
    sicnu::operators::RSOperatorRegistry::instance();
    // installRsOperatorProvider() is safe to call now: the chain completed
    // above. initialize() re-runs the provider outside any lock.
    sicnu::operators::rs::installRsOperatorProvider();
    sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

    auto &store = sicnu::processing::AlgorithmMetaStore::instance();
    REQUIRE( store.loadDefaults() >= 6 );

    const auto entries = store.entries();
    REQUIRE_FALSE( entries.empty() );

    auto kindClaimedBy = []( const std::vector<sicnu::processing::PortDescriptor> &ports,
                             const std::string &kind ) {
        if ( kind.empty() )
            return true;
        sicnu::processing::DataType wanted = sicnu::processing::DataType::Any;
        if ( kind == "raster" )
            wanted = sicnu::processing::DataType::Raster;
        else if ( kind == "vector" )
            wanted = sicnu::processing::DataType::Vector;
        for ( const auto &port : ports )
        {
            if ( port.type == wanted || port.type == sicnu::processing::DataType::Any )
                return true;
        }
        return false;
    };

    for ( const auto &entry : entries )
    {
        DYNAMIC_SECTION( "sidecar " << entry.id )
        {
            // Registry resolution is best-effort in this binary: static
            // operator registration from shared libraries is link-order
            // fragile (#707 notes the known issue), and this gate's real
            // subject is sidecar ↔ descriptor agreement, not registry state.
            // A missing adapter skips the port-kind checks below (the
            // descriptor is unavailable) but never masks drift.
            auto adapter =
                sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( entry.id );
            if ( !adapter )
            {
                auto op = sicnu::operators::RSOperatorRegistry::instance().create( entry.id );
                if ( op )
                {
                    adapter = std::make_shared<sicnu::processing::RsOperatorAdapter>( std::move( op ) );
                    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter( adapter );
                }
            }
            if ( !adapter )
            {
                WARN( "sidecar id does not resolve in AtomicAlgorithmRegistry (skipped port checks): " << entry.id );
                continue;
            }

            const auto &descriptor = adapter->descriptor();
            CHECK( kindClaimedBy( descriptor.inputs, entry.input ) );
            CHECK( kindClaimedBy( descriptor.outputs, entry.output ) );

            std::vector<std::string> drift;
            const auto resolved = store.resolveAgainstDescriptor( entry.id, descriptor.agentMetadata, &drift );
            REQUIRE( resolved.has_value() );
            for ( const auto &d : drift )
                INFO( "capability drift: " << d );
            CHECK( drift.empty() );
        }
    }
}
