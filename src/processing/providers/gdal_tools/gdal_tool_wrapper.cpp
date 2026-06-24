// src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp
#include "gdal_tool_wrapper.h"
#include "tools/tool_path_manager.h"

#include "core/sicnu_logging.h"
#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <processing/qgsprocessingparameters.h>

QVariantMap GdalToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().gdalToolPath(toolName());
    if (program.isEmpty()) {
        SICNU_LOG_ERROR( SicnuLogTags::GDAL, QString( "GDAL tool '%1' not found — ensure GDAL tools are installed" ).arg( toolName() ) );
        if (feedback)
            feedback->reportError(QObject::tr("GDAL tool '%1' not found. Ensure GDAL tools are installed.").arg(toolName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

    SICNU_LOG_INFO( SicnuLogTags::GDAL, QString( "Executing GDAL tool: %1" ).arg( toolName() ) );
    if (!runExternalTool(program, args, feedback)) {
        SICNU_LOG_ERROR( SicnuLogTags::GDAL, QString( "GDAL tool '%1' failed" ).arg( toolName() ) );
        return {};
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::GDAL, QString( "GDAL tool '%1' completed successfully" ).arg( toolName() ) );
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
    if (feedback) feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    SICNU_LOG_INFO( SicnuLogTags::GDAL, cmdLine );

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        QString err = QObject::tr("Failed to start tool: %1").arg(proc.errorString());
        if (feedback) feedback->reportError(err);
        SICNU_LOG_ERROR( SicnuLogTags::GDAL, err );
        return false;
    }

    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("Tool execution canceled by user."));
            return false;
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            QString msg = QString::fromUtf8(output);
            if (feedback) feedback->pushInfo(msg);
            SICNU_LOG_INFO( SicnuLogTags::GDAL, msg );
        }
    }

    if (proc.exitCode() != 0) {
        QString err = QObject::tr("Tool failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardOutput()));
        if (feedback) feedback->reportError(err);
        SICNU_LOG_WARN( SicnuLogTags::GDAL, err );
        return false;
    }

    return true;
}

QString GdalToolWrapper::rasterLayerSource(const QVariant &var)
{
    if (var.canConvert<QgsRasterLayer *>()) {
        QgsRasterLayer *layer = var.value<QgsRasterLayer *>();
        return layer ? layer->source() : QString();
    }
    return var.toString();
}

QString GdalToolWrapper::vectorLayerSource(const QVariant &var)
{
    if (var.canConvert<QgsVectorLayer *>()) {
        QgsVectorLayer *layer = var.value<QgsVectorLayer *>();
        return layer ? layer->source() : QString();
    }
    return var.toString();
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
