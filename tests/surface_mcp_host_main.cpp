// tests/surface_mcp_host_main.cpp — Surface-11 WP-H E2E host.
//
// A minimal headless host that speaks the REAL stdio MCP wire protocol
// (line-delimited JSON on stdin/stdout) through the same McpServer the
// desktop binary (`sicnu_geo_rs --mcp`) hosts — without the GUI stack, so
// the protocol E2E stays hermetic. The E2E test spawns this binary and
// drives initialize → tools/list → schema → run → progress → cancel →
// artifact_read over actual OS pipes.
//
// Registered fixtures (test-only, same pattern as test_mcp_server):
//   rs:surface_noop  — completes immediately
//   rs:surface_sleep — sleeps in 100 ms slices honouring isCancelled, so the
//                      notifications/cancelled path is observable end to end.
#include <QCoreApplication>

#include <chrono>
#include <thread>

#include "agent/mcp_server.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"

namespace {

class NoopOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:surface_noop"; }
    Json::Value run(const Json::Value &, sicnu::operators::RSOperatorContext &) override
    {
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/surface_noop.tif";
        return result;
    }
};

class SleepOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:surface_sleep"; }
    Json::Value run(const Json::Value &params, sicnu::operators::RSOperatorContext &ctx) override
    {
        int slices = 100; // default 10 s; the E2E cancels long before that
        if (params.isMember("slices") && params["slices"].isInt())
            slices = std::max(1, std::min(600, params["slices"].asInt()));
        for (int i = 0; i < slices; ++i)
        {
            if (ctx.isCancelled())
            {
                Json::Value error(Json::objectValue);
                error["cancelled"] = true;
                error["slicesDone"] = i;
                return error;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ctx.reportProgress(i * 100.0 / slices, "sleeping");
        }
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/surface_sleep.tif";
        return result;
    }
};

void registerSurfaceHostOperators()
{
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    registry.registerOperator("rs:surface_noop",
                              []() { return std::make_unique<NoopOperator>(); });
    registry.registerOperator("rs:surface_sleep",
                              []() { return std::make_unique<SleepOperator>(); });
    auto &adapters = sicnu::processing::AtomicAlgorithmRegistry::instance();
    for (const std::string id : { "rs:surface_noop", "rs:surface_sleep" })
    {
        if (!adapters.findAdapter(id))
        {
            auto op = sicnu::operators::RSOperatorRegistry::instance().create(id);
            if (op)
                adapters.registerAdapter(
                    std::make_shared<sicnu::processing::RsOperatorAdapter>(std::move(op)));
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    registerSurfaceHostOperators();
    McpServer server;
    server.start(&app);
    return app.exec();
}
