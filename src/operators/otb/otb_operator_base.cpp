/***************************************************************************
 * otb_operator_base.cpp  —  Common OTB CLI execution logic
 ***************************************************************************/
#include <QFile>
#include "otb_operator_base.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/tools/tool_path_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

#include <algorithm>
#include <cctype>

namespace sicnu::operators::otb {

Json::Value OtbOperatorBase::run(const Json::Value& params, RSOperatorContext& context) {
    validateCommonParams(params);

    const QString appName = otbApplicationName();

    // Build and validate arguments before locating the OTB binary so that
    // parameter errors (missing files, invalid enums, type mismatches) are
    // reported with the correct ErrorCode instead of a generic OtbError.
    const QStringList args = buildOtbArgs(params, context);
    if (args.isEmpty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "No arguments generated for OTB application: " + appName.toStdString());
    }

    const QString program = ToolPathManager::instance().otbToolPath(appName);

    if (program.isEmpty()) {
        const std::string appNameStd = appName.toStdString();
        throw RSOperatorError(ErrorCode::OtbError,
                              QStringLiteral("OTB application '%1' not found. "
                                             "Set SICNU_OTB_PATH or configure OTB path.")
                                  .arg(appName)
                                  .toStdString(),
                              [appNameStd] {
                                  Json::Value details(Json::objectValue);
                                  details["applicationName"] = "otbcli_" + appNameStd;
                                  details["hint"] = "OTB CLI is not installed or not on PATH";
                                  return details;
                              }());
    }

    context.logInfo("Executing OTB application: otbcli_" + appName.toStdString());
    context.logInfo("Command: " + (program + " " + args.join(' ')).toStdString());

    if (!runOtbProcess(program, args, context)) {
        throw RSOperatorError(ErrorCode::OtbError,
                              "OTB application '" + appName.toStdString() + "' failed. "
                              "Check the execution log for details.",
                              [appName] {
                                  Json::Value details(Json::objectValue);
                                  details["applicationName"] = "otbcli_" + appName.toStdString();
                                  return details;
                              }());
    }

    return buildResult(params, context);
}

Json::Value OtbOperatorBase::buildResult(const Json::Value& params,
                                         RSOperatorContext& /*context*/) const {
    Json::Value result(Json::objectValue);
    if (params.isMember("output") && params["output"].isString()) {
        result["output"] = params["output"].asString();
    }
    return result;
}

void OtbOperatorBase::validateCommonParams(const Json::Value& params) const {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }
    requireString(params, "output");
}

Json::Value OtbOperatorBase::buildSchema(const std::string& title,
                                         const std::string& description,
                                         const Json::Value& params,
                                         const Json::Value& outputs,
                                         const std::vector<std::string>& required) const {
    using namespace schema;
    Json::Value root = makeRootSchema(title, description, params, outputs);

    std::vector<std::string> req = required;
    if (std::find(req.begin(), req.end(), "output") == req.end()) {
        req.push_back("output");
    }
    root["required"] = makeRequired(req);
    return root;
}

bool OtbOperatorBase::runOtbProcess(const QString& program, const QStringList& args,
                                    RSOperatorContext& context) {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setProcessEnvironment(buildProcessEnvironment(program));

    proc.start(program, args);

    if (!proc.waitForStarted(10000)) {
        throw RSOperatorError(ErrorCode::OtbError,
                              "Failed to start OTB application: " + proc.errorString().toStdString());
    }

    QString accumulatedOutput;

    // Best-effort output cleanup (#647): OTB applications write their product
    // at the "-out" argument. On cancellation or a non-zero exit the truncated
    // file must be removed instead of left looking like a result.
    QString outputPath;
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == QLatin1String("-out")) {
            outputPath = args.at(i + 1);
            break;
        }
    }
    struct OutputCleanup {
        QString path;
        bool committed = false;
        ~OutputCleanup() { if (!path.isEmpty() && !committed) QFile::remove(path); }
    };
    OutputCleanup cleanup{ outputPath };

    try {
    while (proc.state() == QProcess::Running) {
        context.throwIfCancelled();

        proc.waitForReadyRead(100);
        const QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            const QString text = QString::fromUtf8(output);
            accumulatedOutput += text;
            context.logInfo(text.toStdString());

            // OTB may emit progress on the same stream; try to parse it.
            const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                double progress = -1.0;
                if (parseProgress(line, progress)) {
                    context.reportProgress(progress, line.toStdString());
                }
            }
        }
    }

    context.throwIfCancelled();

    // Drain any remaining output.
    const QByteArray remaining = proc.readAllStandardOutput();
    if (!remaining.isEmpty()) {
        const QString text = QString::fromUtf8(remaining);
        accumulatedOutput += text;
        context.logInfo(text.toStdString());
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        // Under MergedChannels readAllStandardError() is always empty - the
        // diagnostic text already landed in accumulatedOutput.
        Json::Value details(Json::objectValue);
        details["exitCode"] = proc.exitCode();
        if (!accumulatedOutput.isEmpty()) {
            details["output"] = accumulatedOutput.toStdString();
        }

        std::string message = "OTB application failed with exit code " +
                              std::to_string(proc.exitCode());
        if (!accumulatedOutput.isEmpty()) {
            message += ": " + accumulatedOutput.toStdString();
        }

        throw RSOperatorError(ErrorCode::OtbError, message, details);
    }
    } catch (...) {
        throw; // the OutputCleanup destructor removes the partial product
    }

    cleanup.committed = true;
    return true;
}

QProcessEnvironment OtbOperatorBase::buildProcessEnvironment(const QString& toolPath) const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // OTB requires C locale numeric formatting; without this, arguments such as
    // floating-point thresholds may be parsed incorrectly.
    env.insert(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));

    // Locate the OTB bundle. ToolPathManager knows the canonical bundle root
    // (e.g. ${CMAKE_BINARY_DIR}/tools/otb) even when the otbcli_* launcher is
    // symlinked or staged in a different directory (e.g. build/bin).
    QString bundleAbs = ToolPathManager::instance().otbBundleDir();
    if (bundleAbs.isEmpty()) {
        const QFileInfo toolInfo(toolPath);
        const QString toolDir = toolInfo.absolutePath();
        bundleAbs = QDir(toolDir).filePath(QStringLiteral(".."));
    }

    const QString appPath = QDir(bundleAbs).filePath(QStringLiteral("lib/otb/applications"));
    if (QFileInfo::exists(appPath)) {
        env.insert(QStringLiteral("OTB_APPLICATION_PATH"), appPath);
    }

    const QString bundleBin = QDir(bundleAbs).filePath(QStringLiteral("bin"));
    if (QFileInfo::exists(bundleBin)) {
        const QString path = env.value(QStringLiteral("PATH"));
        env.insert(QStringLiteral("PATH"),
                   bundleBin + (path.isEmpty() ? QString() : QStringLiteral(":") + path));
    }

    return env;
}

bool OtbOperatorBase::parseProgress(const QString& line, double& progress) const {
    // OTB progress lines commonly look like:
    //   0% [                                                        ]
    //   50% [#########################                               ]
    // or simply contain a trailing " 45%".
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return false;

    // Look for a number immediately followed by '%' at the start of the line.
    int i = 0;
    while (i < trimmed.size() && std::isspace(trimmed[i].toLatin1())) ++i;

    int start = i;
    while (i < trimmed.size() && (trimmed[i].isDigit() || trimmed[i] == '.')) ++i;

    if (i > start && i < trimmed.size() && trimmed[i] == '%') {
        bool ok = false;
        const double value = trimmed.mid(start, i - start).toDouble(&ok);
        if (ok && value >= 0.0 && value <= 100.0) {
            progress = value / 100.0;
            return true;
        }
    }

    return false;
}

} // namespace sicnu::operators::otb
