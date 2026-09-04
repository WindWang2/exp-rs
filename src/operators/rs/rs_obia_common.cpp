/***************************************************************************
 * rs_obia_common.cpp  —  see rs_obia_common.h
 ***************************************************************************/
#include "rs_obia_common.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_error.h"

#include <QFile>
#include <QTextStream>

#include <cerrno>
#include <cstdlib>
#include <sstream>

namespace sicnu::operators::rs {
namespace obia {

Json::Value objectParam(const Json::Value& params, const std::string& key) {
    const Json::Value& v = params[key];
    if (v.isObject())
        return v;
    if (v.isString() && !v.asString().empty()) {
        Json::Value parsed;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(v.asString());
        if (!Json::parseFromStream(builder, stream, &parsed, &errors) || !parsed.isObject())
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "`" + key + "` must be a JSON object (optionally string-encoded)");
        return parsed;
    }
    return Json::Value(Json::objectValue); // absent / empty
}

bool hasObjectParam(const Json::Value& params, const std::string& key) {
    const Json::Value& v = params[key];
    if (v.isObject())
        return !v.empty();
    return v.isString() && !v.asString().empty();
}

int intKey(const std::string& key, const std::string& paramName) {
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(key.c_str(), &end, 10);
    if (errno != 0 || end == key.c_str() || *end != '\0')
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "`" + paramName + "` keys must be integers (got \"" + key + "\")");
    return static_cast<int>(v);
}

RsClassifierBackendParams classifierHyperParams(const Json::Value& params) {
    RsClassifierBackendParams p;
    p.rfNumTrees = params::getInt(params, "rfNumTrees", p.rfNumTrees);
    p.rfMaxDepth = params::getInt(params, "rfMaxDepth", p.rfMaxDepth);
    p.rfMinSampleCount = params::getInt(params, "rfMinSampleCount", p.rfMinSampleCount);
    p.mlpHiddenLayerSize = params::getInt(params, "mlpHiddenLayerSize", p.mlpHiddenLayerSize);
    p.mlpMaxIter = params::getInt(params, "mlpMaxIter", p.mlpMaxIter);
    if (p.rfNumTrees <= 0 || p.rfMaxDepth <= 0 || p.rfMinSampleCount <= 0
        || p.mlpHiddenLayerSize <= 0 || p.mlpMaxIter <= 0)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "classifier hyperparameters must be > 0");
    return p;
}

QHash<int, QColor> parseClassColors(const Json::Value& colors) {
    QHash<int, QColor> out;
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const int classId = intKey(it.key().asString(), "classColors");
        const QColor color(QString::fromStdString(it->asString()));
        if (classId <= 0 || !color.isValid())
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "classColors entries must be {positive classId: \"#rrggbb\"}");
        out.insert(classId, color);
    }
    return out;
}

Json::Value accuracyToJson(const RsAccuracyAssessment::Result& acc) {
    Json::Value json(Json::objectValue);
    json["overallAccuracy"] = acc.overallAccuracy;
    json["kappa"] = acc.kappa;
    Json::Value classes(Json::arrayValue);
    for (int id : acc.classIds)
        classes.append(id);
    json["classes"] = classes;
    Json::Value confusion(Json::arrayValue);
    if (!acc.confusion.empty()) {
        for (int r = 0; r < acc.confusion.rows; ++r) {
            Json::Value row(Json::arrayValue);
            for (int c = 0; c < acc.confusion.cols; ++c)
                row.append(acc.confusion.at<int>(r, c));
            confusion.append(row);
        }
    }
    json["confusion"] = confusion;
    auto hashToJson = [](const QHash<int, double>& h) {
        Json::Value obj(Json::objectValue);
        for (auto it = h.constBegin(); it != h.constEnd(); ++it)
            obj[std::to_string(it.key())] = it.value();
        return obj;
    };
    json["producer"] = hashToJson(acc.producerAcc);
    json["user"] = hashToJson(acc.userAcc);
    json["f1"] = hashToJson(acc.f1);
    return json;
}

RsAccuracyAssessment::Result trainingAccuracy(const QMap<quint32, int>& trainLabels,
                                              const QMap<quint32, int>& segmentClasses) {
    QVector<int> yTrue;
    QVector<int> yPred;
    yTrue.reserve(trainLabels.size());
    yPred.reserve(trainLabels.size());
    for (auto it = trainLabels.constBegin(); it != trainLabels.constEnd(); ++it) {
        const auto predIt = segmentClasses.constFind(it.key());
        if (predIt == segmentClasses.constEnd())
            continue;
        yTrue.append(it.value());
        yPred.append(predIt.value());
    }
    if (yTrue.isEmpty())
        return {};
    return RsAccuracyAssessment::compute(yTrue, yPred);
}

void writeUncertaintyCsv(const std::string& path,
                         const QMap<quint32, double>& uncertainties,
                         const QMap<quint32, int>& segmentClasses) {
    QFile csv(QString::fromStdString(path));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotWritable, "Cannot write " + path);
    QTextStream out(&csv);
    out << "segment_id,entropy,class_id\n";
    for (auto it = uncertainties.constBegin(); it != uncertainties.constEnd(); ++it)
        out << it.key() << ',' << QString::number(it.value(), 'g', 17)
            << ',' << segmentClasses.value(it.key(), 0) << '\n';
    out.flush();
    if (csv.error() != QFile::NoError)
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed writing " + path + ": " + csv.errorString().toStdString());
    csv.close();
}

} // namespace obia
} // namespace sicnu::operators::rs
