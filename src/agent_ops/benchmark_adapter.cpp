// src/agent_ops/benchmark_adapter.cpp
#include "agent_ops/benchmark_adapter.h"

namespace sicnu::agent_ops {

Json::Value BenchmarkPersistDocument::toJson() const
{
    Json::Value doc(Json::objectValue);
    doc["kind"] = kind;
    doc["suite_id"] = suiteId;
    doc["suite_version"] = suiteVersion;
    doc["pack_digest"] = packDigest;
    doc["result_id"] = resultId;
    doc["status"] = status;
    doc["summary"] = summary;
    doc["cases"] = cases;
    doc["raw_report"] = rawReport;
    return doc;
}

std::optional<BenchmarkPersistDocument> BenchmarkPersistDocument::fromJson(
    const Json::Value &doc, std::string *error)
{
    if (!doc.isObject() || doc.get("kind", "").asString() != kBenchmarkPersistKind)
    {
        if (error)
            *error = "invalid BenchmarkPersistDocument";
        return std::nullopt;
    }
    BenchmarkPersistDocument d;
    d.suiteId = doc.get("suite_id", "").asString();
    d.suiteVersion = doc.get("suite_version", "").asString();
    d.packDigest = doc.get("pack_digest", "").asString();
    d.resultId = doc.get("result_id", "").asString();
    d.status = doc.get("status", "").asString();
    d.summary = doc.get("summary", Json::objectValue);
    d.cases = doc.get("cases", Json::arrayValue);
    d.rawReport = doc.get("raw_report", Json::objectValue);
    return d;
}

namespace {

/// First digest with at least `width` hex-ish characters, else whatever is
/// available; never an empty result id component.
std::string digestComponent(const std::string &primary, const std::string &fallback)
{
    const std::string &pick = primary.size() >= 8 ? primary : fallback;
    return pick.empty() ? "nodigest" : pick.substr(0, 8);
}

} // namespace

bool InMemoryBenchmarkSink::save(const BenchmarkPersistDocument &doc, std::string *error)
{
    (void)error;
    mSaved.push_back(doc);
    return true;
}

BenchmarkPersistDocument BenchmarkAdapter::projectSuiteReport(
    const sicnu::agentbench::SuiteReport &report) const
{
    BenchmarkPersistDocument d;
    d.suiteId = report.suiteId;
    d.suiteVersion = report.version;
    d.packDigest = report.packDigest;
    d.resultId = "bpr-" + report.suiteId + "@" + report.version + "-" +
                 digestComponent(report.packDigest, report.digest);
    d.status = (report.summary.failCount == 0) ? "completed" : "failed";
    d.summary["pass"] = report.summary.passCount;
    d.summary["warnings"] = report.summary.warningsCount;
    d.summary["fail"] = report.summary.failCount;
    d.summary["digest"] = report.digest;
    for (const auto &c : report.cases)
    {
        Json::Value row(Json::objectValue);
        row["case_id"] = c.caseId;
        row["task_family"] = c.taskFamily;
        row["verdict"] = c.verdict;
        row["failure_class"] = c.failureClass;
        row["evaluation_digest"] = c.evaluationDigest;
        row["metrics"] = c.metrics;
        d.cases.append(row);
    }
    d.rawReport = sicnu::agentbench::suiteReportToJson(report);
    return d;
}

bool BenchmarkAdapter::persist(const BenchmarkPersistDocument &doc, IBenchmarkResultSink &sink,
                               std::string *error) const
{
    return sink.save(doc, error);
}

} // namespace sicnu::agent_ops
