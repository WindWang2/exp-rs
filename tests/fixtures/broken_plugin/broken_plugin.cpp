// tests/fixtures/broken_plugin/broken_plugin.cpp — fixture native plugin
// whose FAILURE MODE is selected by the environment so the registry's
// mid-load failure contracts (half-initialized states) can be exercised
// against a REAL misbehaving payload — the fixture itself is the adversary.
//
//   SICNU_BROKEN_PLUGIN_MODE (read at every EXPRS_createPluginV1 call):
//     ok          healthy: registers one operator factory (positive control)
//     nullptr     entrypoint returns nullptr          (LibraryLoadFailed)
//     throw       entrypoint throws std::runtime_error (LibraryLoadFailed,
//                 hand-written entry — the EXPRS_EXPORT_PLUGIN wrapper turns
//                 throws into nullptr, which would test the wrong branch)
//     initfail    initialize() returns false           (InitializationFailed)
//     initthrow   initialize() throws                  (InitializationFailed)
//     idmismatch  pluginId() disagrees with the manifest (InitializationFailed)
//
// The mode is read per call (not cached) so one test process can flip modes
// between loads without reloading the library.
#include "exprs/plugin_interface.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

std::string brokenMode()
{
    const char *mode = std::getenv( "SICNU_BROKEN_PLUGIN_MODE" );
    return mode ? std::string( mode ) : std::string( "ok" );
}

class BrokenOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "test:broken"; }
    std::string displayName() const override { return "Broken Fixture"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Fixture operator for failure-mode tests"; }
    std::string determinismGrade() const override { return "bit_exact"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &, sicnu::operators::RSOperatorContext &context ) override
    {
        context.reportProgress( 1.0, "broken" );
        Json::Value result( Json::objectValue );
        result["success"] = true;
        return result;
    }
};

class BrokenPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override
    {
        if ( brokenMode() == "idmismatch" )
            return "org.exprs.test.not-the-manifest-id";
        return "org.exprs.test.broken-plugin";
    }

    bool initialize( exprs::HostServicesV1 & ) override
    {
        const std::string mode = brokenMode();
        if ( mode == "initfail" )
            return false;
        if ( mode == "initthrow" )
            throw std::runtime_error( "broken fixture initialize() threw" );
        return true;
    }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        context.registerOperatorFactory(
            "test:broken", []() -> std::unique_ptr<sicnu::operators::RSOperator> {
                return std::make_unique<BrokenOperator>();
            } );
    }

    void shutdown() override {}
};

} // namespace

/// Hand-written entry point (NOT EXPRS_EXPORT_PLUGIN): the "throw" mode must
/// reach the loader's exception leg, which the macro's catch-and-return-null
/// wrapper would silently convert into the nullptr leg.
extern "C" EXPRS_PLUGIN_ENTRY_EXPORT ::exprs::PluginV1 *EXPRS_createPluginV1()
{
    if ( brokenMode() == "throw" )
        throw std::runtime_error( "broken fixture entrypoint threw" );
    if ( brokenMode() == "nullptr" )
        return nullptr;
    return new BrokenPlugin();
}
