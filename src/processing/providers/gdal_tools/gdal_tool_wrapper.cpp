// src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp
#include "gdal_tool_wrapper.h"
#include "tools/tool_path_manager.h"

#include "core/sicnu_logging.h"
#include <qgsapplication.h>
#include <qgsexception.h>
#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <processing/qgsprocessingparameters.h>

#include <QFileInfo>

namespace
{
QVariantMap resolveDestinationParameters( const QgsProcessingAlgorithm *algorithm,
                                          const QVariantMap &parameters,
                                          QgsProcessingContext &context )
{
    if ( !algorithm )
        return parameters;

    QVariantMap resolved = parameters;
    for ( const QgsProcessingParameterDefinition *param : algorithm->parameterDefinitions() )
    {
        if ( !param || !resolved.contains( param->name() ) )
            continue;

        const QString type = param->type();
        if ( type != QgsProcessingParameterRasterDestination::typeName()
             && type != QgsProcessingParameterVectorDestination::typeName()
             && type != QgsProcessingParameterFeatureSink::typeName() )
            continue;

        resolved.insert(
            param->name(),
            QgsProcessingParameters::parameterAsOutputLayer( param, resolved.value( param->name() ), context, true ) );
    }
    return resolved;
}
} // namespace

QVariantMap GdalToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    const QVariantMap resolvedParameters = resolveDestinationParameters( this, parameters, context );

    QString program = ToolPathManager::instance().gdalToolPath(toolName());
    if (program.isEmpty()) {
        const QString err = QObject::tr("GDAL tool '%1' not found. Ensure GDAL tools are installed.").arg(toolName());
        SICNU_LOG_ERROR( SicnuLogTags::GDAL, err );
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    QStringList args = buildArgs(resolvedParameters, context, feedback);
    if (args.isEmpty()) {
        const QString err = QObject::tr("Failed to build arguments for GDAL tool '%1'.").arg(toolName());
        SICNU_LOG_ERROR( SicnuLogTags::GDAL, err );
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    SICNU_LOG_INFO( SicnuLogTags::GDAL, QString( "Executing GDAL tool: %1" ).arg( toolName() ) );
    if (!runExternalTool(program, args, feedback)) {
        const QString err = QObject::tr("GDAL tool '%1' failed").arg(toolName());
        SICNU_LOG_ERROR( SicnuLogTags::GDAL, err );
        throw QgsProcessingException(err);
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::GDAL, QString( "GDAL tool '%1' completed successfully" ).arg( toolName() ) );
    QVariantMap results;
    for ( const QgsProcessingParameterDefinition *param : parameterDefinitions() )
    {
        if ( !param || !resolvedParameters.contains( param->name() ) )
            continue;

        const QString type = param->type();
        if ( type == QgsProcessingParameterRasterDestination::typeName()
             || type == QgsProcessingParameterVectorDestination::typeName()
             || type == QgsProcessingParameterFeatureSink::typeName() )
        {
            const QString outPath = resolvedParameters.value( param->name() ).toString();
            if ( !outPath.isEmpty() && !QFileInfo::exists( outPath ) )
            {
                const QString err = QObject::tr(
                                      "Tool reported success but output file is missing: %1" )
                                      .arg( outPath );
                if ( feedback )
                    feedback->reportError( err );
                SICNU_LOG_ERROR( SicnuLogTags::GDAL, err );
                throw QgsProcessingException(err);
            }
            results.insert( param->name(), outPath );
        }
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
