// tests/test_cli_agent_ops.cpp
//
// The CLI `session` command: flag parsing, typed exit codes, and wire
// parity — the CLI envelope data IS the driver's document, so the CLI
// cannot drift from the MCP tool or the workbench panel.

// NOTE: the agent_loop/agent_ops headers come FIRST on purpose — Qt's
// `slots` (empty) macro would break FakeScenario's `slots` member if a Qt
// header were included before these.
#include "agent_loop/fake_seams.h"
#include "agent_ops/ops_driver.h"

#include <catch2/catch_test_macros.hpp>

#include "cli/cli_agent_ops_commands.h"
#include "cli/cli_commands.h"

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>
#include "exprs/version.h"

#include <QStringList>

#include <filesystem>

#include <iostream>
#include <sstream>
#include <string>

using namespace sicnu::agent_ops;
using namespace sicnu::agent_loop;

/// The real CliIO methods live in the CLI executable; tests link a faithful
/// local mirror (same envelope shape) that captures the JSON document.
namespace sicnu::cli {
thread_local std::string g_lastEnvelope;

int CliIO::finish(bool ok, const std::string &command, Json::Value data, int exitCode,
                  const Json::Value &diagnostics, const std::string &errorMessage) const
{
    if (json || jsonLines)
    {
        Json::Value envelope(Json::objectValue);
        envelope["ok"] = ok;
        envelope["command"] = command;
        if (!errorMessage.empty())
            envelope["error"] = errorMessage;
        envelope["data"] = data;
        if (!diagnostics.isNull())
            envelope["diagnostics"] = diagnostics;
        envelope["api_version"] = std::string(EXP_RS_PLUGIN_API_VERSION);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        g_lastEnvelope = Json::writeString(builder, envelope);
    }
    return exitCode;
}

void CliIO::reportProgress(int, int, double, const std::string &) const {}
void CliIO::reportLog(const std::string &, const std::string &) const {}
} // namespace sicnu::cli

namespace {

std::string qstr(const QString &text)
{
    return text.toStdString();
}

struct CliRun
{
    int exitCode = -1;
    Json::Value envelope;
    std::string errorMessage;
};

CliRun runSession(const QStringList &arguments, OpsDriver *driver)
{
    CliRun run;
    sicnu::cli::CliIO io;
    io.json = true;
    run.exitCode = sicnu::cli::commandAgentSession(arguments, io, driver);

    const std::string &out = sicnu::cli::g_lastEnvelope;
    if (!out.empty())
    {
        Json::CharReaderBuilder builder;
        std::string error;
        Json::CharReader *reader = builder.newCharReader();
        reader->parse(out.data(), out.data() + out.size(), &run.envelope, &error);
        delete reader;
    }
    return run;
}

OpsDriver::Options wiredOptions(FakeSeams &seams)
{
    OpsDriver::Options options;
    options.deps.seams = {&seams.dataProvider(), &seams.planner(), &seams.preflight(),
                          &seams.executor(), &seams.verifier(), &seams.diagnoser()};
    return options;
}

} // namespace

TEST_CASE("cli session command: usage, unknown action and unknown flags",
          "[cli][ops_driver]")
{
    auto none = runSession({}, nullptr);
    REQUIRE(none.exitCode == 6); // InvalidInput
    REQUIRE(none.envelope["error"].asString().find("usage:") != std::string::npos);

    auto bogusAction = runSession({"teleport"}, nullptr);
    REQUIRE(bogusAction.exitCode == 6);
    REQUIRE(bogusAction.envelope["error"].asString().find("unknown session action") !=
            std::string::npos);

    auto bogusFlag = runSession({"status", "--wat"}, nullptr);
    REQUIRE(bogusFlag.exitCode == 6);
    REQUIRE(bogusFlag.envelope["error"].asString().find("unknown flag") != std::string::npos);

    // A null driver is a typed availability failure, not a crash.
    auto noDriver = runSession({"actions"}, nullptr);
    REQUIRE(noDriver.exitCode == 7); // RuntimeUnavailable
    REQUIRE(noDriver.envelope["error"].asString() == "AGENT_OPS_UNAVAILABLE");
}

TEST_CASE("cli session command drives the same driver documents (wire parity)",
          "[cli][ops_driver][parity]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));

    // Reference document straight from the driver...
    Json::Value args(Json::objectValue);
    args["goal"] = "compute NDVI for the scene";
    args["intent"] = "ndvi";
    const Json::Value direct = driver.apply("run", args);
    REQUIRE(direct["ok"].asBool());

    // ...a second session through the CLI must carry the SAME fields.
    FakeScenario scenario2;
    FakeSeams seams2(scenario2);
    OpsDriver driver2(wiredOptions(seams2));
    auto cli = runSession({"run", "--goal", "compute NDVI for the scene",
                           "--intent", "ndvi"},
                          &driver2);
    REQUIRE(cli.exitCode == 0);
    REQUIRE(cli.envelope["command"].asString() == "session");
    const Json::Value &doc = cli.envelope["data"];
    REQUIRE(doc["ok"].asBool());
    // Core typed fields match the driver contract field-for-field.
    for (const char *field : {"schema", "tool", "session_id", "outcome", "stop_reason",
                              "projection", "delivery", "error", "reconcile",
                              "driver_schema"})
        REQUIRE(doc.isMember(field));
    REQUIRE(doc["schema"].asString() == direct["schema"].asString());
    REQUIRE(doc["tool"].asString() == direct["tool"].asString());
    REQUIRE(doc["driver_schema"].asString() == direct["driver_schema"].asString());
    REQUIRE(doc["delivery"]["goal"].asString() ==
            direct["delivery"]["goal"].asString());
}

TEST_CASE("cli session command: typed exit codes for typed failures",
          "[cli][ops_driver]")
{
    // No production seams in the default CLI host: run fails closed with
    // MissingDependency (5), not a fake success.
    static OpsDriver seamless(OpsDriver::Options{});
    auto seamsGate = runSession({"run", "--goal", "anything"}, &seamless);
    REQUIRE(seamsGate.exitCode == 5);
    REQUIRE(seamsGate.envelope["data"]["error"].asString() == "SEAMS_UNAVAILABLE");

    // status before any session: ValidationFailure (2) + typed NO_SESSION.
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));
    auto noSession = runSession({"status"}, &driver);
    REQUIRE(noSession.exitCode == 2);
    REQUIRE(noSession.envelope["data"]["error"].asString() == "NO_SESSION");

    // reconcile without required args: ValidationFailure + MISSING_ARGS.
    auto missing = runSession({"reconcile", "--session-id", "sess-x"}, &driver);
    REQUIRE(missing.exitCode == 2);
    REQUIRE(missing.envelope["data"]["error"].asString() == "MISSING_ARGS");

    // Control actions succeed and map to Ok (0).
    REQUIRE(runSession({"pause"}, &driver).exitCode == 0);
    REQUIRE(runSession({"clear-pause"}, &driver).exitCode == 0);
    REQUIRE(runSession({"cancel"}, &driver).exitCode == 0);
    auto aborted = runSession(
        {"run", "--goal", "compute NDVI for the scene", "--intent", "ndvi"}, &driver);
    INFO("envelope: " << aborted.envelope.toStyledString());
    REQUIRE(aborted.exitCode == 1); // GenericError: the session aborted
    // The loop is the authority: the typed stop reason rides the delivery.
    REQUIRE(aborted.envelope["data"]["delivery"]["stop_reason"].asString() == "CANCELLED");
    REQUIRE(runSession({"clear-cancel"}, &driver).exitCode == 0);
    auto relaunched = runSession(
        {"run", "--goal", "compute NDVI for the scene", "--intent", "ndvi"}, &driver);
    REQUIRE(relaunched.exitCode == 0);
    REQUIRE(relaunched.envelope["data"]["outcome"].asString() == "delivered");
}

TEST_CASE("cli session command: journal reconcile over the wire",
          "[cli][ops_driver][crash]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));

    // A delivered session journaled under a temp directory.
    const std::string dir =
        (std::filesystem::temp_directory_path() / "cli_agent_ops_reconcile").string();
    std::filesystem::create_directories(dir);
    Json::Value runArgs(Json::objectValue);
    runArgs["goal"] = "compute NDVI for the scene";
    runArgs["intent"] = "ndvi";
    runArgs["journal_directory"] = dir;
    auto done = driver.apply("run", runArgs);
    REQUIRE(done["ok"].asBool());
    const std::string sessionId = done["session_id"].asString();

    auto recon = runSession(
        {"reconcile", "--journal-dir", QString::fromStdString(dir),
         "--session-id", QString::fromStdString(sessionId)},
        &driver);
    REQUIRE(recon.exitCode == 0);
    const Json::Value &r = recon.envelope["data"]["reconcile"];
    REQUIRE(r["terminal_state"].asString() == "delivered");
    REQUIRE(r["resumable"].asBool() == false);
    REQUIRE(r["reason_code"].asString() == "ALREADY_TERMINAL");

    // Re-running the delivered session through the CLI is refused with the
    // typed duplicate-submit code (exit 2, ValidationFailure) — the driver
    // matches the coordinator resume path's refusal code.
    auto dup = runSession({"run", "--goal", "compute NDVI for the scene",
                           "--intent", "ndvi",
                           "--journal-dir", QString::fromStdString(dir),
                           "--session-id", QString::fromStdString(sessionId)},
                          &driver);
    REQUIRE(dup.exitCode == 2);
    REQUIRE(dup.envelope["data"]["error"].asString() == "DUPLICATE_SUBMIT_REFUSED");
}
