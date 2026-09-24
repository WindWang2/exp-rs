// test_preflight_report_schema.cpp — RS14-02 Slice A
//
// Contract tests for the Scientific Preflight finding/report schema
// (sicnu.preflight.report/1): deterministic canonical serialization,
// tamper-sensitive digest, fail-closed readers.
//
// These tests must FAIL (compile or assert) until src/preflight exists.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <json/json.h>

#include "preflight/finding.h"
#include "preflight/report.h"
#include "preflight/sha256.h"

using namespace sicnu::preflight;

namespace {

Json::Value stringArray( std::initializer_list<const char *> items )
{
    Json::Value arr(Json::arrayValue);
    for ( const char *item : items )
        arr.append(item);
    return arr;
}

Json::Value makeEvidence()
{
    Json::Value e(Json::objectValue);
    e["declared_state"]  = "dn";
    e["acceptable"]      = stringArray({"surface_reflectance", "toa"});
    return e;
}

PreflightFinding sampleFinding()
{
    PreflightFinding f;
    f.code         = "SPF_RADIOMETRIC_STATE_MISMATCH";
    f.severity     = PreflightSeverity::Block;
    f.ruleId       = "preflight.radiometric_state_policy";
    f.ruleRevision = 1;
    f.domain       = "radiometric";
    f.subject      = "primary";
    f.affectedInputs = {"scene_a.tif"};
    f.evidence     = makeEvidence();
    f.basis        = "observed";
    f.humanExplanation = "NDVI 需要反射率域输入，而 primary 声明为 raw DN。";
    f.machineExplanation["expectation"] = "radiometric_state in [surface_reflectance, toa]";
    f.machineExplanation["actual"]      = "dn";
    f.machineExplanation["remediation"] = stringArray({"run radiometric calibration to toa"});
    return f;
}

} // namespace

TEST_CASE("finding severity wire strings round-trip", "[preflight][schema]")
{
    REQUIRE(severityToString(PreflightSeverity::Block) == "block");
    REQUIRE(severityToString(PreflightSeverity::RequireAck) == "require_ack");
    REQUIRE(severityToString(PreflightSeverity::Warn) == "warn");
    REQUIRE(severityToString(PreflightSeverity::Info) == "info");

    REQUIRE(severityFromString("block") == PreflightSeverity::Block);
    REQUIRE(severityFromString("require_ack") == PreflightSeverity::RequireAck);
    REQUIRE(severityFromString("warn") == PreflightSeverity::Warn);
    REQUIRE(severityFromString("info") == PreflightSeverity::Info);
    REQUIRE_FALSE(severityFromString("error").has_value());
    REQUIRE_FALSE(severityFromString("").has_value());
}

TEST_CASE("finding toJson carries the full typed shape", "[preflight][schema]")
{
    const Json::Value j = sampleFinding().toJson();
    REQUIRE(j["code"].asString() == "SPF_RADIOMETRIC_STATE_MISMATCH");
    REQUIRE(j["severity"].asString() == "block");
    REQUIRE(j["rule_id"].asString() == "preflight.radiometric_state_policy");
    REQUIRE(j["rule_revision"].asInt() == 1);
    REQUIRE(j["domain"].asString() == "radiometric");
    REQUIRE(j["subject"].asString() == "primary");
    REQUIRE(j["affected_inputs"].size() == 1);
    REQUIRE(j["affected_inputs"][0].asString() == "scene_a.tif");
    REQUIRE(j["evidence"]["declared_state"].asString() == "dn");
    REQUIRE(j["basis"].asString() == "observed");
    REQUIRE(j["acknowledged"].asBool() == false);
    REQUIRE_FALSE(j["human_explanation"].asString().empty());
    REQUIRE(j["machine_explanation"]["expectation"].asString().find("radiometric_state")
            != std::string::npos);
    REQUIRE(j["machine_explanation"]["remediation"].size() == 1);
}

TEST_CASE("finding fromJson is fail-closed", "[preflight][schema]")
{
    const Json::Value good = sampleFinding().toJson();
    auto parsed = PreflightFinding::fromJson(good);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->code == "SPF_RADIOMETRIC_STATE_MISMATCH");
    REQUIRE(parsed->severity == PreflightSeverity::Block);
    REQUIRE(parsed->machineExplanation == sampleFinding().machineExplanation);

    Json::Value badSeverity = good;
    badSeverity["severity"] = "fatal";
    REQUIRE_FALSE(PreflightFinding::fromJson(badSeverity).has_value());

    Json::Value noCode = good;
    noCode.removeMember("code");
    REQUIRE_FALSE(PreflightFinding::fromJson(noCode).has_value());

    Json::Value badBasis = good;
    badBasis["basis"] = "guessed";
    REQUIRE_FALSE(PreflightFinding::fromJson(badBasis).has_value());
}

TEST_CASE("sha256 is stable and shortDigest truncates", "[preflight][schema]")
{
    // Reference digests computed independently (echo -n ... | sha256sum).
    REQUIRE(sha256Hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(sha256Hex("sicnu-preflight") ==
            sha256Hex("sicnu-preflight"));
    REQUIRE(sha256Hex("a") != sha256Hex("b"));
    REQUIRE(shortDigest("sicnu-preflight").size() == 16);
    REQUIRE(shortDigest("sicnu-preflight") == shortDigest("sicnu-preflight"));
}

TEST_CASE("report canonical JSON is byte-deterministic", "[preflight][report]")
{
    PreflightReport r = PreflightReport::makeEmpty("rs:ndvi", "teaching");
    r.verdict        = "blocked";
    r.requestDigest  = shortDigest("req");
    r.rulesRevision  = shortDigest("rules");
    r.findings.push_back(sampleFinding());

    PreflightFinding w = sampleFinding();
    w.code     = "SPF_GRID_RESOLUTION_MISMATCH";
    w.severity = PreflightSeverity::RequireAck;
    w.ruleId   = "preflight.pair_resolution_ratio";
    r.findings.push_back(w);

    const std::string a = canonicalReportJson(r);
    const std::string b = canonicalReportJson(r);
    REQUIRE(a == b);
    REQUIRE_FALSE(a.empty());
    // Sorted keys, no whitespace variance: canonical text must embed the
    // schema id and kind and both finding codes.
    REQUIRE(a.find("sicnu.preflight.report/1") != std::string::npos);
    REQUIRE(a.find("preflight_report") != std::string::npos);
    REQUIRE(a.find("SPF_RADIOMETRIC_STATE_MISMATCH") != std::string::npos);
    REQUIRE(a.find("SPF_GRID_RESOLUTION_MISMATCH") != std::string::npos);
    // No wall-clock / nondeterministic fields allowed.
    REQUIRE(a.find("timestamp") == std::string::npos);
    REQUIRE(a.find("generated_at") == std::string::npos);
}

TEST_CASE("report digest is tamper sensitive", "[preflight][report]")
{
    PreflightReport r = PreflightReport::makeEmpty("rs:ndvi", "agent");
    r.verdict       = "ok";
    r.requestDigest = shortDigest("req");
    r.rulesRevision = shortDigest("rules");

    const std::string d1 = reportDigest(r);
    REQUIRE(d1.size() == 64);

    r.verdict = "blocked";
    const std::string d2 = reportDigest(r);
    REQUIRE(d1 != d2);
}

TEST_CASE("report fromJson round-trips and is fail-closed", "[preflight][report]")
{
    PreflightReport r = PreflightReport::makeEmpty("rs:ndvi", "teaching");
    r.verdict        = "blocked"; // consistent with the Block finding below
    r.requestDigest  = shortDigest("req");
    r.rulesRevision  = shortDigest("rules");
    r.findings.push_back(sampleFinding());

    const Json::Value j = r.toJson();
    REQUIRE(j["schema_version"].asString() == "1");
    REQUIRE(j["kind"].asString() == "preflight_report");
    REQUIRE(j["verdict"].asString() == "blocked");
    REQUIRE(j["findings"].size() == 1);
    REQUIRE(j["evaluated"].isArray());
    REQUIRE(j["budgets"].isObject());

    auto parsed = PreflightReport::fromJson(j);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->verdict == "blocked");
    REQUIRE(parsed->findings.size() == 1);
    REQUIRE(parsed->findings.front().code == "SPF_RADIOMETRIC_STATE_MISMATCH");
    REQUIRE(canonicalReportJson(*parsed) == canonicalReportJson(r));

    Json::Value badVersion = j;
    badVersion["schema_version"] = "2";
    REQUIRE_FALSE(PreflightReport::fromJson(badVersion).has_value());

    Json::Value badKind = j;
    badKind["kind"] = "something_else";
    REQUIRE_FALSE(PreflightReport::fromJson(badKind).has_value());

    Json::Value badVerdict = j;
    badVerdict["verdict"] = "maybe";
    REQUIRE_FALSE(PreflightReport::fromJson(badVerdict).has_value());

    // A verdict that contradicts its own findings is corrupt: render would
    // project a can_proceed the findings refute. Fail closed.
    PreflightReport lying = PreflightReport::makeEmpty("rs:ndvi", "teaching");
    lying.verdict = "ok"; // claims a clean run...
    lying.findings.push_back(sampleFinding()); // ...while carrying a Block
    REQUIRE_FALSE(PreflightReport::fromJson(lying.toJson()).has_value());
}
