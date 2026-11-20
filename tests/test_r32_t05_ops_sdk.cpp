// tests/test_r32_t05_ops_sdk.cpp
// R32-T05 regressions for the agent-ops approval single-shot semantics
// (#2078), typed DecisionRecord inner-field validation (#2079), digest
// case-insensitivity (#2112) and non-ASCII plugin path handling (#2111,
// #2134): discovery and bounded snapshots preserve UTF-8 path identities.
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
#include "exprs/plugin_snapshot.h"
#include "exprs/version.h"
#include "geospatial/util/sha256.h"
#include "platform/portable.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
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
    return sicnu::portable::pathToUtf8(base);
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
    doc["inputs"] = Json::Value(Json::objectValue);
    doc["selected"] = Json::Value(Json::objectValue);
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
        : path(sicnu::portable::pathToUtf8(
              std::filesystem::temp_directory_path() /
              sicnu::portable::pathFromUtf8(leaf + "." + std::to_string(::getpid()))))
    {
        std::error_code ec;
        std::filesystem::remove_all(sicnu::portable::pathFromUtf8(path), ec);
        std::filesystem::create_directories(sicnu::portable::pathFromUtf8(path));
    }
    ~ScratchGuard()
    {
        std::error_code ec;
        std::filesystem::remove_all(sicnu::portable::pathFromUtf8(path), ec);
    }
    ScratchGuard(const ScratchGuard &) = delete;
    ScratchGuard &operator=(const ScratchGuard &) = delete;
};

void writeManifest(const std::string &dir, const std::string &version, const std::string &checksumLine)
{
    std::ofstream manifest(sicnu::portable::pathFromUtf8(dir + "/plugin.json"), std::ios::trunc);
    REQUIRE(manifest.is_open());
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
    std::filesystem::create_directories(sicnu::portable::pathFromUtf8(source));
    {
        std::ofstream payload(sicnu::portable::pathFromUtf8(source + "/payload.txt"),
                              std::ios::binary | std::ios::trunc);
        REQUIRE(payload.is_open());
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
    // Use native paths for fixture I/O too: a narrow UTF-8 std::string
    // would create ACP-mangled filenames on Windows before reaching the SDK.
    ScratchGuard scratch("r32_t05_插件目录");
    const std::string source = scratch.path + "/中文包😀/org.r32.na";
    std::filesystem::create_directories(sicnu::portable::pathFromUtf8(source));
    {
        std::ofstream payload(sicnu::portable::pathFromUtf8(source + "/数据.txt"),
                              std::ios::binary | std::ios::trunc);
        REQUIRE(payload.is_open());
        payload << "中文 payload 内容\n";
    }
    std::ofstream manifest(sicnu::portable::pathFromUtf8(source + "/plugin.json"), std::ios::trunc);
    REQUIRE(manifest.is_open());
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
    const bool installOk = PluginPackage::install(source, installed, log);
    INFO("SDK UTF-8 install diagnostics: " << redactSecrets(log.toJson()).toStyledString());
    REQUIRE(installOk);
    REQUIRE_FALSE(installed.empty());
    // The payload file (non-ASCII name) actually landed with its bytes.
    std::ifstream landed(sicnu::portable::pathFromUtf8(installed + "/数据.txt"), std::ios::binary);
    REQUIRE(landed.is_open());
    std::string content((std::istreambuf_iterator<char>(landed)), std::istreambuf_iterator<char>());
    REQUIRE(content == "中文 payload 内容\n");
    // Windows cannot remove a payload while this inspection stream owns it.
    landed.close();
    REQUIRE_FALSE(landed.is_open());

    // Discovery through a non-ASCII root still finds it.
    PluginDiscoveryOptions options;
    options.roots = {scratch.path + "/中文包😀"};
    const auto records = PluginDiscovery::scan(options, log);
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].manifest.id == "org.r32.na");
    REQUIRE(records[0].state == PluginState::Discovered);

    const bool uninstallOk = PluginPackage::uninstall("org.r32.na", log);
    INFO("SDK UTF-8 uninstall diagnostics: " << redactSecrets(log.toJson()).toStyledString());
    REQUIRE(uninstallOk);
}

TEST_CASE("SDK discovery preserves Unicode root and plugin directory identities",
          "[r32t05][plugin][sdk_utf8][sdk_utf8_discovery]")
{
    namespace fs = std::filesystem;
    ScratchGuard scratch("r32_t05_发现根😀");
    const fs::path root = sicnu::portable::pathFromUtf8(scratch.path + "/插件 根𐐷");
    const fs::path plugin = root / sicnu::portable::pathFromUtf8("插件目录😀");
    fs::create_directories(plugin);
    {
        std::ofstream payload(plugin / "payload.txt", std::ios::binary);
        REQUIRE(payload.is_open());
        payload << "payload-for-checksum-verification\n";
    }
    writeManifest(sicnu::portable::pathToUtf8(plugin), "1.0.0",
                  "31492367e97f4ab8dd5f118e03a606f4ae02c586a46a46810e247e5fe9958dd6");

    PluginDiscoveryOptions options;
    options.roots = {sicnu::portable::pathToUtf8(root)};
    options.useCache = true;
    PluginDiagnosticLog log;
    const auto records = PluginDiscovery::scan(options, log);
    REQUIRE_FALSE(log.hasErrors());
    REQUIRE(records.size() == 1);
    CHECK(records.front().manifest.id == "org.r32.t05");
    CHECK(records.front().state == PluginState::Discovered);
    CHECK(sicnu::portable::pathFromUtf8(records.front().directory).lexically_normal()
          == plugin.lexically_normal());
    CHECK(sicnu::portable::pathFromUtf8(records.front().manifestPath).lexically_normal()
          == (plugin / "plugin.json").lexically_normal());

    const auto cached = PluginDiscovery::scan(options, log);
    REQUIRE(cached.size() == 1);
    CHECK(cached.front().directory == records.front().directory);
    CHECK(cached.front().manifestPath == records.front().manifestPath);
    CHECK(cached.front().manifest.id == "org.r32.t05");
}

TEST_CASE("SDK snapshots capture restore and recover Unicode payload paths",
          "[r32t05][plugin][sdk_utf8][sdk_utf8_snapshot]")
{
    namespace fs = std::filesystem;
    const auto utf8 = [](const fs::path &path) { return sicnu::portable::pathToUtf8(path); };
    const auto write = [](const fs::path &path, const std::string &bytes) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        REQUIRE(stream.is_open());
        stream << bytes;
        stream.close();
        REQUIRE_FALSE(stream.fail());
    };
    const auto read = [](const fs::path &path) {
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.is_open());
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    };
    ScratchGuard scratch("r32_t05_快照根😀");
    const fs::path temp = sicnu::portable::pathFromUtf8(scratch.path + "/恢复 工作区𐐷");
    const fs::path plugins = temp / sicnu::portable::pathFromUtf8("插件 根😀");
    const std::string id = "org.r32.utf8";
    const fs::path source = plugins / id;
    const fs::path payload = source / sicnu::portable::pathFromUtf8("数据𐐷.txt");
    const fs::path obsolete = source / sicnu::portable::pathFromUtf8("应删除😀.txt");
    fs::create_directories(source);
    write(payload, "original\n");
    const fs::path snapshots = sicnu::portable::pathFromUtf8(pluginSnapshotRoot(utf8(temp)));
    const fs::path captured = snapshots / "last-good-org.r32.utf8";
    PluginSnapshotBudget budget;
    auto result = capturePluginSnapshot(utf8(source), utf8(captured), id, budget);
    INFO(result.message);
    REQUIRE(result.ok());
    CHECK(result.files == 1);
    CHECK(result.bytes == 9);
    std::string error;
    REQUIRE(verifyPluginSnapshot(utf8(captured), id, error));
    CHECK(read(captured / payload.filename()) == "original\n");

    write(payload, "modified\n");
    write(obsolete, "extra\n");
    REQUIRE(restorePluginSnapshot(utf8(captured), utf8(source), id, error));
    CHECK(read(payload) == "original\n");
    CHECK_FALSE(fs::exists(obsolete));
    CHECK_FALSE(fs::exists(captured));
    CHECK_FALSE(fs::exists(source / kPluginSnapshotMarker));

    // Recover a dead owner's complete upgrade copy through the public GC
    // seam; both snapshot and target roots must cross UTF-8 boundaries intact.
    const fs::path upgrade = snapshots /
        ("upgrade-" + id + "-" + std::to_string(std::numeric_limits<int>::max()));
    REQUIRE(capturePluginSnapshot(utf8(source), utf8(upgrade), id, budget).ok());
    const fs::path foreign = snapshots / sicnu::portable::pathFromUtf8("保留😀");
    fs::create_directories(foreign);
    write(payload, "changed again\n");
    write(obsolete, "extra\n");
    CHECK(sweepPluginSnapshots(utf8(temp), {id}, {utf8(plugins)}) == 1);
    CHECK(read(payload) == "original\n");
    CHECK_FALSE(fs::exists(obsolete));
    CHECK_FALSE(fs::exists(upgrade));
    CHECK(fs::is_directory(foreign));

    budget.maxFileBytes = 1;
    result = capturePluginSnapshot(utf8(source), utf8(captured), id, budget);
    CHECK(result.status == PluginSnapshotStatus::BudgetExceeded);
    CHECK(result.message.find("数据𐐷.txt") != std::string::npos);
    CHECK_FALSE(fs::exists(captured));
}


TEST_CASE("plugin packages cover every payload file and support an external trust store (#1806)",
          "[r32t05][plugin][package][trust]")
{
    struct EnvironmentGuard
    {
        std::string root = sicnu::portable::envUtf8("SICNU_PLUGIN_USER_ROOT");
        std::string store = sicnu::portable::envUtf8("SICNU_PLUGIN_TRUST_STORE");
        std::string required = sicnu::portable::envUtf8("SICNU_PLUGIN_REQUIRE_TRUST");
        ~EnvironmentGuard()
        {
            const auto restore = [](const char *name, const std::string &value) {
                if (value.empty())
                    ::unsetenv(name);
                else
                    ::setenv(name, value.c_str(), 1);
            };
            restore("SICNU_PLUGIN_USER_ROOT", root);
            restore("SICNU_PLUGIN_TRUST_STORE", store);
            restore("SICNU_PLUGIN_REQUIRE_TRUST", required);
        }
    } env;

    ScratchGuard scratch("r32_t05_trust_store");
    const std::string userRoot = scratch.path + "/plugins";
    const std::string trustStore = scratch.path + "/trusted.json";
    ::setenv("SICNU_PLUGIN_USER_ROOT", userRoot.c_str(), 1);
    ::setenv("SICNU_PLUGIN_TRUST_STORE", trustStore.c_str(), 1);
    ::setenv("SICNU_PLUGIN_REQUIRE_TRUST", "0", 1);

    const std::string source = scratch.path + "/org.r32.t05";
    std::filesystem::create_directories(sicnu::portable::pathFromUtf8(source));
    {
        std::ofstream payload(sicnu::portable::pathFromUtf8(source + "/payload.txt"),
                              std::ios::binary | std::ios::trunc);
        REQUIRE(payload.is_open());
        payload << "payload-for-checksum-verification\n";
    }
    writeManifest(source, "1.0.0",
                  "31492367e97f4ab8dd5f118e03a606f4ae02c586a46a46810e247e5fe9958dd6");
    {
        std::ofstream extra(sicnu::portable::pathFromUtf8(source + "/extra.txt"),
                            std::ios::binary | std::ios::trunc);
        extra << "undeclared";
    }
    PluginDiagnosticLog log;
    std::string installed;
    REQUIRE_FALSE(PluginPackage::install(source, installed, log));
    REQUIRE(log.hasErrors());
    std::filesystem::remove(sicnu::portable::pathFromUtf8(source + "/extra.txt"));

    {
        std::ofstream store(sicnu::portable::pathFromUtf8(trustStore), std::ios::trunc);
        store << "{\"org.r32.t05\":\"" << std::string(64, '0') << "\"}";
    }
    ::setenv("SICNU_PLUGIN_REQUIRE_TRUST", "1", 1);
    log = PluginDiagnosticLog();
    REQUIRE_FALSE(PluginPackage::install(source, installed, log));
    REQUIRE(log.hasErrors());

    std::ifstream manifestInput(sicnu::portable::pathFromUtf8(source + "/plugin.json"),
                                std::ios::binary);
    const std::string manifestBytes((std::istreambuf_iterator<char>(manifestInput)),
                                    std::istreambuf_iterator<char>());
    const std::string digest = sicnu::geo::sha256Hex(manifestBytes);
    {
        std::ofstream store(sicnu::portable::pathFromUtf8(trustStore), std::ios::trunc);
        store << "{\"org.r32.t05\":\"" << digest << "\"}";
    }
    log = PluginDiagnosticLog();
    REQUIRE(PluginPackage::install(source, installed, log));
    REQUIRE_FALSE(installed.empty());
    REQUIRE(PluginPackage::uninstall("org.r32.t05", log));
}
