/***************************************************************************
 * rs_stack_register_operator.cpp — F13
 ***************************************************************************/
#include "rs_stack_register_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/registration/registration_quality.h"
#include "processing/algorithms/registration/stack_registrator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// jsoncpp scalar/object -> Qt JSON (bounded depth; stack inputs are small).
QJsonObject toQtObject(const Json::Value& value, int depth = 0)
{
    QJsonObject obj;
    if (depth > 16 || !value.isObject())
        return obj;
    for (const auto& key : value.getMemberNames()) {
        const Json::Value& v = value[key];
        if (v.isDouble())
            obj.insert(QString::fromStdString(key), v.asDouble());
        else if (v.isIntegral())
            obj.insert(QString::fromStdString(key),
                       static_cast<qint64>(v.asInt64()));
        else if (v.isString())
            obj.insert(QString::fromStdString(key),
                       QString::fromStdString(v.asString()));
        else if (v.isBool())
            obj.insert(QString::fromStdString(key), v.asBool());
    }
    return obj;
}

void appendObservation(std::vector<sicnu::registration::StackPairObservation>& obs,
                       const QJsonObject& obj)
{
    sicnu::registration::StackPairObservation o;
    // Both spellings are accepted: the operator documents camelCase while
    // the agent tool surfaces snake_case (P3 review finding).
    o.fromId = obj.value(QStringLiteral("fromId")).toString(
        obj.value(QStringLiteral("from_id")).toString());
    o.toId = obj.value(QStringLiteral("toId")).toString(
        obj.value(QStringLiteral("to_id")).toString());
    o.tx = obj.value(QStringLiteral("tx")).toDouble();
    o.ty = obj.value(QStringLiteral("ty")).toDouble();
    o.confidence = obj.value(QStringLiteral("confidence")).toDouble(1.0);
    o.inlierCount = obj.value(QStringLiteral("inlierCount")).toInt(
        obj.value(QStringLiteral("inlier_count")).toInt(1));
    if (!o.fromId.isEmpty() && !o.toId.isEmpty())
        obs.push_back(o);
}

} // namespace

Json::Value RsStackRegisterOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["scenes"] = makeStringParam("scenes", "JSON array of scene ids", "[]");
    props["observations"] = makeStringParam(
        "observations",
        "JSON array of pairwise observations {fromId, toId, tx, ty, confidence, inlierCount}",
        "[]");
    props["reference"] = makeStringParam("reference", "Reference scene id (optional)", "");
    props["reportPath"] = makeStringParam(
        "reportPath", "Optional JSON sidecar path for the adjustment solution", "");

    Json::Value outputs(Json::objectValue);
    outputs["reference"] = makeStringParam("reference", "Solved reference scene id", "");
    outputs["status"] = makeStringParam("status", "success | refused", "");
    outputs["maxEdgeResidualPx"] = makeNumberParam("maxEdgeResidualPx",
                                                   "Worst post-adjustment edge residual (px)",
                                                   0.0);
    outputs["rmsEdgeResidualPx"] = makeNumberParam("rmsEdgeResidualPx",
                                                   "RMS edge residual (px)", 0.0);
    outputs["disconnectedScenes"] = makeIntegerParam("disconnectedScenes",
                                                     "Scenes outside the connected subgraph", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"scenes", "observations"});
    return root;
}

Json::Value RsStackRegisterOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("registration");
    meta["tags"].append("stack");
    meta["tags"].append("block-adjustment");
    meta["purpose"] = "Combine pairwise translations into one drift-audited stack solution";
    meta["prerequisites"].append(
        "Pairwise translations measured by rs:register_images (or the agent tool)");
    meta["workflowHints"].append(
        "maxEdgeResidualPx is the distributed loop-closure error — large values mean the "
        "pair graph disagrees with itself");
    meta["workflowHints"].append("Disconnected scenes are reported, not silently dropped");
    return meta;
}

Json::Value RsStackRegisterOperator::executionEstimate() const
{
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    // Dense normal equations over <= 256 scenes: (2*256)^2 doubles ~= 2 MiB.
    est["estimatedRamBytes"] = 4194304;
    est["temporaryDiskBytes"] = 0;
    return est;
}

Json::Value RsStackRegisterOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    context.reportProgress(0.1, "Parsing stack inputs");
    context.throwIfCancelled();

    std::vector<QString> ids;
    if (p.isMember("scenes")) {
        if (p["scenes"].isString()) {
            const auto doc =
                QJsonDocument::fromJson(QString::fromStdString(p["scenes"].asString()).toUtf8());
            for (const auto& v : doc.array())
                ids.push_back(v.toString());
        } else if (p["scenes"].isArray()) {
            for (const auto& v : p["scenes"])
                ids.push_back(QString::fromStdString(v.asString()));
        }
    }
    std::vector<sicnu::registration::StackPairObservation> obs;
    if (p.isMember("observations")) {
        if (p["observations"].isString()) {
            const auto doc = QJsonDocument::fromJson(
                QString::fromStdString(p["observations"].asString()).toUtf8());
            for (const auto& v : doc.array())
                appendObservation(obs, v.toObject());
        } else if (p["observations"].isArray()) {
            for (const auto& v : p["observations"])
                appendObservation(obs, toQtObject(v));
        }
    }

    const std::string reference =
        p.isMember("reference") && p["reference"].isString() ? p["reference"].asString()
                                                             : std::string();
    const std::string reportPath =
        p.isMember("reportPath") && p["reportPath"].isString() ? p["reportPath"].asString()
                                                               : std::string();

    sicnu::registration::StackOptions options;
    if (!reference.empty())
        options.referenceId = QString::fromStdString(reference);

    const auto sol = sicnu::registration::StackRegistrator::solveTranslations(ids, obs, options);

    if (sol.status == sicnu::registration::RegistrationStatus::Refused)
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Stack registration refused (" + sol.reason.toStdString() + ")");

    context.reportProgress(0.9, "Solved");

    Json::Value result(Json::objectValue);
    result["reference"] = sol.referenceId.toStdString();
    result["status"] = sicnu::registration::statusToString(sol.status).toStdString();
    result["maxEdgeResidualPx"] = sol.maxEdgeResidual;
    result["rmsEdgeResidualPx"] = sol.rmsEdgeResidual;
    result["disconnectedScenes"] = sol.disconnectedScenes;
    Json::Value scenes(Json::arrayValue);
    for (const auto& s : sol.scenes) {
        Json::Value sc(Json::objectValue);
        sc["sceneId"] = s.sceneId.toStdString();
        sc["offsetX"] = s.offsetX;
        sc["offsetY"] = s.offsetY;
        sc["hopCount"] = s.hopCount;
        sc["maxEdgeResidualPx"] = s.maxEdgeResidual;
        sc["connected"] = s.connected;
        scenes.append(sc);
    }
    result["scenes"] = scenes;

    if (!reportPath.empty()) {
        QJsonObject doc;
        doc.insert(QStringLiteral("schema"), QStringLiteral("exp_rs_stack_registration/1"));
        doc.insert(QStringLiteral("status"), sicnu::registration::statusToString(sol.status));
        doc.insert(QStringLiteral("reference"), sol.referenceId);
        doc.insert(QStringLiteral("maxEdgeResidualPx"), sol.maxEdgeResidual);
        doc.insert(QStringLiteral("rmsEdgeResidualPx"), sol.rmsEdgeResidual);
        doc.insert(QStringLiteral("disconnectedScenes"), sol.disconnectedScenes);
        QJsonArray sceneArr;
        for (const auto& s : sol.scenes) {
            QJsonObject sc;
            sc.insert(QStringLiteral("sceneId"), s.sceneId);
            sc.insert(QStringLiteral("offsetX"), s.offsetX);
            sc.insert(QStringLiteral("offsetY"), s.offsetY);
            sc.insert(QStringLiteral("hopCount"), s.hopCount);
            sc.insert(QStringLiteral("maxEdgeResidualPx"), s.maxEdgeResidual);
            sc.insert(QStringLiteral("connected"), s.connected);
            sceneArr.append(sc);
        }
        doc.insert(QStringLiteral("scenes"), sceneArr);
        if (!sicnu::registration::RegistrationQuality::writeReportAtomic(
                QString::fromStdString(reportPath), doc))
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Failed to write the stack report sidecar");
    }

    context.reportProgress(1.0, "Stack registration complete");
    return result;
}

} // namespace sicnu::operators::rs
