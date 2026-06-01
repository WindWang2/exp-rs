// src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp
#include "gdal_tool_wrapper.h"
#include "tools/tool_path_manager.h"

#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <processing/qgsprocessingparameters.h>

QVariantMap GdalToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().gdalToolPath(toolName());
    if (program.isEmpty()) {
        feedback->reportError(QObject::tr("GDAL tool '%1' not found. Ensure GDAL tools are installed.").arg(toolName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

    if (!runExternalTool(program, args, feedback)) {
        return {};
    }

    QVariantMap results;
    if (parameters.contains("OUTPUT")) {
        results["OUTPUT"] = parameters.value("OUTPUT");
    }
    return results;
}

bool GdalToolWrapper::runExternalTool(const QString &program, const QStringList &args,
                                       QgsProcessingFeedback *feedback)
{
    QString cmdLine = program + " " + args.join(" ");
    feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    QgsMessageLog::logMessage(cmdLine, "gdal", Qgis::MessageLevel::Info);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        QString err = QObject::tr("Failed to start tool: %1").arg(proc.errorString());
        feedback->reportError(err);
        QgsMessageLog::logMessage(err, "gdal", Qgis::MessageLevel::Critical);
        return false;
    }

    while (proc.state() == QProcess::Running) {
        if (feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("Tool execution canceled by user."));
            return false;
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            QString msg = QString::fromUtf8(output);
            feedback->pushInfo(msg);
            QgsMessageLog::logMessage(msg, "gdal", Qgis::MessageLevel::Info);
        }
    }

    if (proc.exitCode() != 0) {
        // MergedChannels merges stderr into stdout, so read from readAllStandardOutput()
        QString err = QObject::tr("Tool failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardOutput()));
        feedback->reportError(err);
        QgsMessageLog::logMessage(err, "gdal", Qgis::MessageLevel::Warning);
        return false;
    }

    return true;
}

void GdalToolWrapper::addInputRasterLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterRasterLayer(name, description));
}

void GdalToolWrapper::addOutputRasterLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterRasterDestination(name, description));
}

void GdalToolWrapper::addInputVectorLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterVectorLayer(name, description));
}

void GdalToolWrapper::addOutputVectorLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterVectorDestination(name, description));
}

void GdalToolWrapper::addExtentParameter(const QString &name)
{
    addParameter(new QgsProcessingParameterExtent(name, QObject::tr("Extent")));
}

void GdalToolWrapper::addCrsParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterCrs(name, description, QVariant(), true));
}
