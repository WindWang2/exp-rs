// tests/test_r32_t05_ops_sdk.cpp
// R32-T05 regressions for the agent-ops approval single-shot semantics
// (#2078), typed DecisionRecord inner-field validation (#2079), digest
// case-insensitivity (#2112) and non-ASCII plugin path handling (#2111,
// Linux leg: ACP decoding is a Windows failure mode; here the same flows
// must keep working end-to-end through the UTF-8 constructors).
#include <catch2/catch_test_macros.hpp>
#ifdef _WIN32
#include "exprs/msvc_posix_shim.h"
#else
#include <unistd.h>
#endif

#include "agent_loop/decision_record.h"
#include "agent_loop/fake_seams.h"
#include "agent_ops/operations_coordinator.h"
#include "agent_ops/repair_approval.h"

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_package.h"
#include "exprs/version.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::agent_ops;
using namespace sicnu::agent_loop;
using namespace exprs;

namespace {

std::string uniqueTemp(const char *tag)
{
    auto base = std::filesystem::temp_directory_path() /
                (std::string("r32_t05_ops_") + tag + "." + std::to_string(::getpid()));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    return base.string();
}

OpsRunRequest ndviRequest()
{
    OpsRunRequest req;
    req.session.goal = "compute NDVI for the scene";
    return req;
}

class FakeCapabilityProvider : public sicnu::repair::RepairCapabilityProvider
{
  public:
    void add(const std::string &kind, const Json::Value &entry) { mFamilies[kind].append(entry); }

    std::vector<Json::Value> capabilitiesForRequirement(
        const std::string &requirementKind) const override
    {
        const auto it = mFamilies.find(requirementKind);
        if (it == mFamilies.end())
            return {};
        std::vector<Json::Value> out;
        for (const Json::Value &entry : it->second)
            out.push_back(entry);
        return out;
    }

    bool knowsRequirementKind(const std::string &requirementKind) const override
    {
        return mFamilies.count(requirementKind) > 0;
    }

  private:
    std::map<std::string, Json::Value> mFamilies;
};

Json::Value capabilityOf(const char *id, const char *family, const char *costClass)
{
    Json::Value entry(Json::objectValue);
    entry["id"] = id;
    entry["family"] = family;
    entry["resource"]["cost_class"] = costClass;
    return entry;
}

OpDiagnostic preflightFixableDiagnostic(const std::string &code = "GRID_MISMATCH")
{
    OpDiagnostic diag;
    diag.code = "ops.preflight.FIXABLE";
    diag.rootCauseCode = "PREFLIGHT_FIXABLE";
    diag.repairable = true;
    diag.advisoryNext = recovery_action::kRepair;
    diag.confidence = 0.9;
    Json::Value report(Json::objectValue);
    report["verdict"] = "fixable";
    Json::Value issues(Json::arrayValue);
    Json::Value issue(Json::objectValue);
    issue["code"] = code;
    issue["severity"] = "error";
    issue["message"] = "grid does not match the reference grid";
    issues.append(issue);
    report["issues"] = issues;
    diag.sources["preflight"] = report;
    return diag;
}

/// Coordinator with a projected grid repair plan, matching the r27/t03
/// approve-the-plan flow.
struct WiredWithPlan
{
    FakeCapabilityProvider provider;
    FakeScenario scenario;
    FakeSeams seams;
    OperationsCoordinator::Dependencies deps;
    std::unique_ptr<OperationsCoordinator> coord;

    WiredWithPlan() : scenario(), seams(scenario)
    {
        provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
        deps.seams = {&seams.dataProvider(), &seams.planner(), &seams.preflight(),
                      &seams.executor(), &seams.verifier(), &seams.diagnoser()};
        deps.repairCapabilityProvider = &provider;
        coord = std::make_unique<OperationsCoordinator>(deps);
    }

    void project(const char *code = "GRID_MISMATCH")
    {
        RecoveryContext ctx;
        auto decision = coord->evaluateRecovery(preflightFixableDiagnostic(code), ctx);
        REQUIRE(decision.repairPlan["status"].asString() == "planned");
    }
};

Json::Value minimalRecordDoc()
{
    Json::Value doc(Json::objectValue);
    doc["schema_version"] = kDecisionRecordSchemaVersion;
    doc["decision_id"] = "dec-r32-t05";
    doc["session_id"] = "sess-r32-t05";
    doc["stage"] = "execute";
    doc["recorded_at"] = Json::Int64(1000);
    doc["reason"] = "fixture";
    doc["alternatives"] = Json::Value(Json::arrayValue);
    doc["evidence"] = Json::Value(Json::arrayValue);
    return doc;
}

} // namespace

TEST_CASE("#2078: an invalid direct approval token is refused without an armed-approval rescue, and a valid armed approval still launches once",
          "[r32t05][agent_ops][approval]")
{
    WiredWithPlan wired;
    wired.project();
    std::string err;
    Json::Value token = wired.coord->mintRepairApprovalForOperator(1000, 60000, &err);
    REQUIRE(token.isObject());
    REQUIRE(wired.coord->armRepairApproval(token, 1000).empty());
    REQUIRE(wired.coord->hasPendingRepairApproval());

    // Malformed direct token beside the VALID armed one: typed refusal, the
    // armed entry is consumed either way, and nothing approves silently.
    OpsRunRequest malformedReq = ndviRequest();
    malformedReq.approvalNowMs = 1001;
    malformedReq.repairApproval = Json::Value(Json::objectValue); // no digest
    auto malformed = wired.coord->run(malformedReq);
    REQUIRE(malformed.approvalError == "APPROVAL_MALFORMED");
    REQUIRE_FALSE(wired.coord->hasPendingRepairApproval());

    // Positive control: a valid armed approval on a clean launch reports no
    // approval error and is consumed exactly once.
    wired.project();
    Json::Value valid = wired.coord->mintRepairApprovalForOperator(2000, 60000, &err);
    REQUIRE(valid.isObject());
    REQUIRE(wired.coord->armRepairApproval(valid, 2000).empty());
    OpsRunRequest cleanReq = ndviRequest();
    cleanReq.approvalNowMs = 2001;
    auto clean = wired.coord->run(cleanReq);
    REQUIRE(clean.approvalError.empty());
    REQUIRE_FALSE(wired.coord->hasPendingRepairApproval());

    // Replay: presenting the already-consumed token as a direct token is a
    // typed replay, never a second authorization.
    OpsRunRequest replayReq = ndviRequest();
    replayReq.approvalNowMs = 2002;
    replayReq.repairApproval = valid;
    auto replay = wired.coord->run(replayReq);
    REQUIRE(replay.approvalError == "APPROVAL_REPLAYED");
}

TEST_CASE("#2079: persisted DecisionRecord inner fields are type-validated",
          "[r32t05][agent_loop][decision]")
{
    std::string error;

    Json::Value good = minimalRecordDoc();
    good["inputs"]["run_id"] = "run-1";
    good["selected"]["action"] = "run";
    good["selected"]["plan_id"] = "plan-1";
    REQUIRE(DecisionRecord::fromJson(good, &error).has_value());
    REQUIRE(error.empty());

    const std::pair<const char *, Json::Value> badInputs[] = {
        {"inputs.run_id", Json::Int64(42)},
        {"inputs.run_id", Json::Value(Json::arrayValue)},
        {"inputs.run_id", Json::Value()},
    };
    for (const auto &[field, value] : badInputs)
    {
        Json::Value doc = minimalRecordDoc();
        doc["inputs"][std::strchr(field, '.') + 1] = value;
        error.clear();
        auto parsed = DecisionRecord::fromJson(doc, &error);
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(error.find(field) != std::string::npos);
    }

    const std::pair<const char *, Json::Value> badSelected[] = {
        {"selected.action", Json::Int64(7)},
        {"selected.action", Json::Value(Json::arrayValue)},
        {"selected.plan_id", Json::Value()},
    };
    for (const auto &[field, value] : badSelected)
    {
        Json::Value doc = minimalRecordDoc();
        doc["selected"][std::strchr(field, '.') + 1] = value;
        error.clear();
        auto parsed = DecisionRecord::fromJson(doc, &error);
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(error.find(field) != std::string::npos);
    }

    // Older compatible history without those optional fields still parses.
    Json::Value legacy = minimalRecordDoc();
    legacy["inputs"] = Json::Value(Json::objectValue);
    error.clear();
    REQUIRE(DecisionRecord::fromJson(legacy, &error).has_value());
}

namespace {

struct ScratchGuard
{
    const std::string path;
    explicit ScratchGuard(const std::string &leaf)
        : path(std::string("/tmp/") + leaf + "." + std::to_string(::getpid()))
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path);
    }
    ~ScratchGuard()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ScratchGuard(const ScratchGuard &) = delete;
    ScratchGuard &operator=(const ScratchGuard &) = delete;
};

void writeManifest(const std::string &dir, const std::string &version, const std::string &checksumLine)
{
    std::ofstream manifest(dir + "/plugin.json", std::ios::trunc);
    manifest << R"({
        "manifest_version": 1,
        "id": "org.r32.t05",
        "name": "R32T05",
        "version": ")" << version << R"(",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "operators": [],
        "package": { "checksums": { "payload.txt": ")" << checksumLine << R"(" } }
    })";
}

} // namespace

TEST_CASE("#2112: uppercase and mixed-case hex digests verify as the same bytes",
          "[r32t05][plugin][package]")
{
    ScratchGuard scratch("r32_t05_digest_case");
    const std::string source = scratch.path + "/org.r32.t05";
    std::filesystem::create_directories(source);
    {
        std::ofstream payload(source + "/payload.txt", std::ios::trunc);
        payload << "payload-for-checksum-verification\n";
    }
    // sha256("payload-for-checksum-verification\n") — the known-answer
    // digest already pinned by test_exprs_plugin_system's lowercase case.
    const char *lower = "31492367e97f4ab8dd5f118e03a606f4ae02c586a46a46810e247e5fe9958dd6";
    std::string upper, mixed;
    for (const char *c = lower; *c; ++c)
    {
        upper += static_cast<char>(std::toupper(*c));
        mixed += (std::distance(lower, c) % 2) ? static_cast<char>(std::toupper(*c)) : *c;
    }

    PluginDiagnosticLog log;
    std::string installed;
    writeManifest(source, "1.0.0", upper);
    REQUIRE(PluginPackage::install(source, installed, log));
    REQUIRE_FALSE(installed.empty());

    writeManifest(source, "1.1.0", mixed);
    log = PluginDiagnosticLog();
    REQUIRE(PluginPackage::install(source, installed, log));

    // A genuinely wrong digest of correct spelling is still refused and the
    // previous install survives.
    writeManifest(source, "2.0.0",
                  "DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEF");
    log = PluginDiagnosticLog();
    REQUIRE_FALSE(PluginPackage::install(source, installed, log));
    REQUIRE(log.hasErrors());
    PluginDiagnostic parseError;
    PluginManifest survivor;
    REQUIRE(loadManifestFromFile(installed + "/plugin.json", survivor, parseError));
    REQUIRE(survivor.version == "1.1.0");

    REQUIRE(PluginPackage::uninstall("org.r32.t05", log));
}

TEST_CASE("#2111: install, discover and uninstall keep working through non-ASCII UTF-8 paths",
          "[r32t05][plugin][package]")
{
    // Linux leg: the same flows must hold end-to-end when every path
    // component carries non-ASCII text. (Windows ACP decoding is the
    // failure mode the UTF-8 constructors exist for; that lane is verified
    // separately.)
    ScratchGuard scratch("r32_t05_插件目录");
    const std::string source = scratch.path + "/中文包😀/org.r32.na";
    std::filesystem::create_directories(source);
    {
        std::ofstream payload(source + "/数据.txt", std::ios::trunc);
        payload << "中文 payload 内容\n";
    }
    std::ofstream manifest(source + "/plugin.json", std::ios::trunc);
    manifest << R"({
        "manifest_version": 1,
        "id": "org.r32.na",
        "name": "非ASCII插件",
        "version": "1.0.0",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "operators": []
    })";
    manifest.close();

    PluginDiagnosticLog log;
    std::string installed;
    REQUIRE(PluginPackage::install(source, installed, log));
    REQUIRE_FALSE(installed.empty());
    // The payload file (non-ASCII name) actually landed with its bytes.
    std::ifstream landed(installed + "/数据.txt");
    REQUIRE(landed.is_open());
    std::string content((std::istreambuf_iterator<char>(landed)), std::istreambuf_iterator<char>());
    REQUIRE(content == "中文 payload 内容\n");

    // Discovery through a non-ASCII root still finds it.
    PluginDiscoveryOptions options;
    options.roots = {scratch.path + "/中文包😀"};
    const auto records = PluginDiscovery::scan(options, log);
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].manifest.id == "org.r32.na");
    REQUIRE(records[0].state == PluginState::Discovered);

    REQUIRE(PluginPackage::uninstall("org.r32.na", log));
}
