// src/processing/providers/otb_tools/otb_tool_wrapper.cpp
#include "otb_tool_wrapper.h"
#include "tools/tool_path_manager.h"

#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <processing/qgsprocessingparameters.h>

QVariantMap OtbToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().otbToolPath(applicationName());
    if (program.isEmpty()) {
        feedback->reportError(QObject::tr("OTB application '%1' not found. Ensure OTB is installed.").arg(applicationName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

    if (!runOtbApplication(args, feedback)) {
        return {};
    }

    QVariantMap results;
    if (parameters.contains("OUTPUT")) {
        results["OUTPUT"] = parameters.value("OUTPUT");
    }
    return results;
}

bool OtbToolWrapper::runOtbApplication(const QStringList &args, QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().otbToolPath(applicationName());

    feedback->pushInfo(QObject::tr("Running: %1 %2").arg(program, args.join(" ")));

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        feedback->reportError(QObject::tr("Failed to start OTB application: %1").arg(proc.errorString()));
        return false;
    }

    while (proc.state() == QProcess::Running) {
        if (feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("OTB application canceled by user."));
            return false;
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            feedback->pushInfo(QString::fromUtf8(output));
        }
    }

    if (proc.exitCode() != 0) {
        feedback->reportError(QObject::tr("OTB application failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardError())));
        return false;
    }

    return true;
}
