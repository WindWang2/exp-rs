/***************************************************************************
 * rs_pipeline_runner.cpp  —  Headless pipeline executor for RSOperators
 ***************************************************************************/
#include "rs_pipeline_runner.h"

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTextStream>

#include <cstdlib>
#include <sstream>

namespace sicnu::cli {

namespace {

constexpr int kMaxPipelineSteps = 100;

bool absolutePathOutsideWorkspace(const std::string &pathValue,
                                  const QString &workspaceRoot,
                                  std::string *detail)
{
    if (pathValue.empty())
        return false;

    QString path = QString::fromStdString(pathValue);
    if (path.startsWith(QLatin1Char('~')))
        path = QDir::homePath() + path.mid(1);

    const QFileInfo fi(path);
    if (!fi.isAbsolute())
        return false;

    QString workspaceCanon = QDir(workspaceRoot).canonicalPath();
    if (workspaceCanon.isEmpty())
        workspaceCanon = QFileInfo(workspaceRoot).absoluteFilePath();
    if (workspaceCanon.isEmpty())
        return false;

    QString resolved;
    if (fi.exists()) {
        resolved = fi.canonicalFilePath();
    } else {
        QDir parent = fi.dir();
        QString parentCanon = parent.canonicalPath();
        if (parentCanon.isEmpty())
            parentCanon = parent.absolutePath();
        resolved = QDir(parentCanon).filePath(fi.fileName());
    }

    const QString normResolved = QDir::cleanPath(resolved);
    const QString normWorkspace = QDir::cleanPath(workspaceCanon);

    if (normResolved == normWorkspace)
        return false;
    if (normResolved.startsWith(normWorkspace + QLatin1Char('/')))
        return false;

    if (detail)
        *detail = "Path outside SICNU_PIPELINE_WORKSPACE: " + pathValue;
    return true;
}

bool jsonValueOutsideWorkspace(const Json::Value &value,
                               const QString &workspaceRoot,
                               std::string *detail)
{
    if (value.isString())
        return absolutePathOutsideWorkspace(value.asString(), workspaceRoot, detail);

    if (value.isArray()) {
        for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
            if (jsonValueOutsideWorkspace(value[i], workspaceRoot, detail))
                return true;
        }
        return false;
    }

    if (value.isObject()) {
        const auto names = value.getMemberNames();
        for (const auto &name : names) {
            if (jsonValueOutsideWorkspace(value[name], workspaceRoot, detail))
                return true;
        }
        return false;
    }

    return false;
}

} // namespace

RsPipelineRunner::RsPipelineRunner(ProgressCallback progressCallback,
                                   LogCallback logCallback)
    : m_progressCallback(std::move(progressCallback))
    , m_logCallback(std::move(logCallback))
{
}

RsPipelineRunner::PipelineResult RsPipelineRunner::runFromJson(const Json::Value& pipelineJson) {
    PipelineResult result;
    result.success = false;

    std::string validationError;
    if (!validatePipelineJson(pipelineJson, &validationError)) {
        result.errorMessage = "Invalid pipeline JSON: " + validationError;
        return result;
    }

    const Json::Value steps = pipelineJson["steps"];
    const int totalSteps = static_cast<int>(steps.size());
    const std::string pipelineName =
        pipelineJson.isMember("name") ? pipelineJson["name"].asString() : "unnamed";

    reportLog("info", "Starting pipeline: " + pipelineName +
                          " (" + std::to_string(totalSteps) + " steps)");

    for (int i = 0; i < totalSteps; ++i) {
        const Json::Value step = steps[i];
        const std::string operatorName = step["operator"].asString();
        const Json::Value params = step.isMember("params")
                                       ? step["params"]
                                       : Json::Value(Json::objectValue);

        StepResult stepResult;
        stepResult.operatorName = operatorName;
        stepResult.params = params;

        reportProgress(i, totalSteps, 0.0, "Running " + operatorName);

        auto op = operators::RSOperatorRegistry::instance().create(operatorName);
        if (!op) {
            stepResult.errorMessage = "Operator not registered: " + operatorName;
            stepResult.errorCode = static_cast<int>(operators::ErrorCode::InvalidParameter);
            result.steps.push_back(stepResult);
            result.errorMessage = stepResult.errorMessage;
            reportLog("error", stepResult.errorMessage);
            return result;
        }

        operators::RSOperatorContext context;

        // Forward operator progress into pipeline progress
        context.setProgressCallback([this, i, totalSteps, operatorName](double progress,
                                                                        const std::string& message) {
            reportProgress(i, totalSteps, progress,
                           "[" + operatorName + "] " + message);
        });

        // Forward operator logs (LogFn is (message, level) — see rs_progress_callback.h).
        context.setLogCallback([this](const std::string& message, const std::string& level) {
            reportLog(level, message);
        });

        try {
            Json::Value opResult = op->execute(params, context);
            stepResult.result = opResult;
            stepResult.success = true;
            reportProgress(i, totalSteps, 1.0, "Finished " + operatorName);
        } catch (const operators::RSOperatorError& e) {
            stepResult.errorMessage = e.message();
            stepResult.errorCode = static_cast<int>(e.code());
            result.errorMessage = "Step " + std::to_string(i + 1) + " (" + operatorName +
                                  ") failed: " + stepResult.errorMessage;
            reportLog("error", result.errorMessage);
            result.steps.push_back(stepResult);
            return result;
        } catch (const std::exception& e) {
            stepResult.errorMessage = e.what();
            stepResult.errorCode = static_cast<int>(operators::ErrorCode::Unknown);
            result.errorMessage = "Step " + std::to_string(i + 1) + " (" + operatorName +
                                  ") failed: " + stepResult.errorMessage;
            reportLog("error", result.errorMessage);
            result.steps.push_back(stepResult);
            return result;
        }

        result.steps.push_back(stepResult);
    }

    result.success = true;
    reportLog("info", "Pipeline completed successfully: " + pipelineName);
    return result;
}

RsPipelineRunner::PipelineResult RsPipelineRunner::runFromFile(const std::string& filePath) {
    PipelineResult result;
    result.success = false;

    QFile file(QString::fromStdString(filePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = "Cannot open pipeline file: " + filePath;
        return result;
    }

    QTextStream in(&file);
    const QString jsonText = in.readAll();

    Json::Value root;
    Json::CharReaderBuilder readerBuilder;
    std::string parseError;
    std::istringstream jsonStream(jsonText.toStdString());

    if (!Json::parseFromStream(readerBuilder, jsonStream, &root, &parseError)) {
        result.errorMessage = "JSON parse error in " + filePath + ": " + parseError;
        return result;
    }

    return runFromJson(root);
}

bool RsPipelineRunner::validatePipelineJson(const Json::Value& pipelineJson,
                                            std::string* errorMessage) {
    if (!pipelineJson.isObject()) {
        if (errorMessage) *errorMessage = "Root must be a JSON object";
        return false;
    }

    if (!pipelineJson.isMember("steps") || !pipelineJson["steps"].isArray()) {
        if (errorMessage) *errorMessage = "Missing or invalid 'steps' array";
        return false;
    }

    const Json::Value steps = pipelineJson["steps"];
    if (static_cast<int>(steps.size()) > kMaxPipelineSteps) {
        if (errorMessage) {
            *errorMessage = "Pipeline exceeds maximum of " + std::to_string(kMaxPipelineSteps)
                            + " steps (got " + std::to_string(steps.size()) + ")";
        }
        return false;
    }

    const QString workspace = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("SICNU_PIPELINE_WORKSPACE"));

    for (Json::ArrayIndex i = 0; i < steps.size(); ++i) {
        const Json::Value step = steps[i];
        if (!step.isObject()) {
            if (errorMessage) *errorMessage = "Step " + std::to_string(i) + " is not an object";
            return false;
        }
        if (!step.isMember("operator") || !step["operator"].isString()) {
            if (errorMessage) *errorMessage = "Step " + std::to_string(i) + " missing 'operator' string";
            return false;
        }
        if (step.isMember("params") && !step["params"].isObject()) {
            if (errorMessage) *errorMessage = "Step " + std::to_string(i) + " 'params' is not an object";
            return false;
        }

        if (!workspace.isEmpty() && step.isMember("params")) {
            std::string detail;
            if (jsonValueOutsideWorkspace(step["params"], workspace, &detail)) {
                if (errorMessage)
                    *errorMessage = "Step " + std::to_string(i) + ": " + detail;
                return false;
            }
        }
    }
    return true;
}

void RsPipelineRunner::reportProgress(int stepIndex, int totalSteps, double stepProgress,
                                      const std::string& message) const {
    if (m_progressCallback) {
        m_progressCallback(stepIndex, totalSteps, stepProgress, message);
    }
}

void RsPipelineRunner::reportLog(const std::string& level, const std::string& message) const {
    if (m_logCallback) {
        m_logCallback(level, message);
    }
}

} // namespace sicnu::cli
