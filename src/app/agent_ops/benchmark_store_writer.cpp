/***************************************************************************
 * benchmark_store_writer.cpp
 ***************************************************************************/
#include "app/agent_ops/benchmark_store_writer.h"

#include "experiment/benchmark_runner.h"
#include "experiment/experiment_store.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace sicnu::app::agent_ops {

namespace {

QJsonObject jsonCppToQJson(const Json::Value &value)
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    const std::string s = Json::writeString(b, value);
    return QJsonDocument::fromJson(QByteArray::fromStdString(s)).object();
}

} // namespace

sicnu::experiment::BenchmarkResult toBenchmarkResult(
    const sicnu::agent_ops::BenchmarkPersistDocument &doc)
{
    sicnu::experiment::BenchmarkResult result;
    result.setResultId(QString::fromStdString(doc.resultId));
    result.setBenchmarkId(QString::fromStdString(doc.suiteId));
    // suite version is a string in agentbench; store as metadata when non-numeric
    bool ok = false;
    const quint64 ver = QString::fromStdString(doc.suiteVersion).toULongLong(&ok);
    result.setBenchmarkVersion(ok ? ver : 1);
    result.setDefinitionDigest(QString::fromStdString(doc.packDigest));
    if (doc.status == "completed")
        result.setStatus(sicnu::experiment::BenchmarkRunStatus::Completed);
    else
        result.setStatus(sicnu::experiment::BenchmarkRunStatus::Failed);
    result.setSoftwareRevision(QStringLiteral("agent_ops"));
    QJsonObject meta = jsonCppToQJson(doc.toJson());
    // BenchmarkResult may not expose arbitrary metadata setters on all tips;
    // failure message carries a bounded pointer for audit.
    result.setFailureMessage(QStringLiteral("agent_ops.benchmark_persist:%1")
                                 .arg(QString::fromStdString(doc.resultId)));
    if (doc.status != "completed")
        result.setFailureCode(QStringLiteral("agentbench.suite_failed"));
    Q_UNUSED(meta);
    return result;
}

ExperimentStoreBenchmarkSink::ExperimentStoreBenchmarkSink(
    sicnu::experiment::ExperimentStore *store)
    : m_store(store)
{
}

bool ExperimentStoreBenchmarkSink::save(const sicnu::agent_ops::BenchmarkPersistDocument &doc,
                                        std::string *error)
{
    if (!m_store)
    {
        if (error)
            *error = "null ExperimentStore";
        return false;
    }
    const auto result = toBenchmarkResult(doc);
    const auto r = m_store->saveBenchmarkResult(result);
    if (!r)
    {
        if (error)
        {
            if (!r.diagnostics().isEmpty())
                *error = r.diagnostics().front().message.toStdString();
            else
                *error = "saveBenchmarkResult failed";
        }
        return false;
    }
    return true;
}

} // namespace sicnu::app::agent_ops
