// src/processing/providers/gdal_tools/algorithms/gdaltransform.cpp
#include "gdaltransform.h"
#include "tools/tool_path_manager.h"

#include "core/sicnu_logging.h"
#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

void GdalTransformAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addCrsParameter("SOURCE_CRS", QObject::tr("Source CRS"));
    addCrsParameter("TARGET_CRS", QObject::tr("Target CRS"));

    addParameter(new QgsProcessingParameterNumber(
        "X", QObject::tr("X coordinate"), Qgis::ProcessingNumberParameterType::Double, 0.0));
    addParameter(new QgsProcessingParameterNumber(
        "Y", QObject::tr("Y coordinate"), Qgis::ProcessingNumberParameterType::Double, 0.0));
    addParameter(new QgsProcessingParameterNumber(
        "Z", QObject::tr("Z coordinate (optional)"), Qgis::ProcessingNumberParameterType::Double, 0.0, true));

    addParameter(new QgsProcessingParameterRasterLayer(
        "INPUT", QObject::tr("Input raster (optional, for geolocation-based transform)"), QVariant(), true));

    addParameter(new QgsProcessingParameterBoolean(
        "OUTPUT_XY", QObject::tr("Output X Y only"), false));

    addParameter(new QgsProcessingParameterFileDestination(
        "OUTPUT", QObject::tr("Transform output (text file)"), "Text files (*.txt)"));
}

QStringList GdalTransformAlgorithm::buildArgs(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    if (parameters.contains("SOURCE_CRS") && !parameters.value("SOURCE_CRS").toString().isEmpty()) {
        args << "-s_srs" << parameters.value("SOURCE_CRS").toString();
    }

    if (parameters.contains("TARGET_CRS") && !parameters.value("TARGET_CRS").toString().isEmpty()) {
        args << "-t_srs" << parameters.value("TARGET_CRS").toString();
    }

    if (parameters.value("OUTPUT_XY", false).toBool()) {
        args << "-output_xy";
    }

    const QString input = rasterLayerSource(parameters.value("INPUT"));
    if (!input.isEmpty()) {
        args << input;
    }

    return args;
}

QVariantMap GdalTransformAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                     QgsProcessingContext &context,
                                                     QgsProcessingFeedback *feedback)
{
    // Resolve FileDestination like GdalToolWrapper does (creates parent dirs)
    QVariantMap resolvedParams = parameters;
    for (const QgsProcessingParameterDefinition *param : parameterDefinitions()) {
        if (!param || !resolvedParams.contains(param->name()))
            continue;
        const QString type = param->type();
        if (type == QgsProcessingParameterFileDestination::typeName()
            || type == QgsProcessingParameterRasterDestination::typeName()
            || type == QgsProcessingParameterVectorDestination::typeName()
            || type == QgsProcessingParameterFeatureSink::typeName()
            || type == QgsProcessingParameterFolderDestination::typeName()) {
            resolvedParams.insert(
                param->name(),
                QgsProcessingParameters::parameterAsOutputLayer(param, resolvedParams.value(param->name()), context, true));
        }
    }
    const QVariantMap &effectiveParams = resolvedParams;

    const QString program = ToolPathManager::instance().gdalToolPath(toolName());
    if (program.isEmpty()) {
        const QString err = QObject::tr("GDAL tool '%1' not found. Ensure GDAL tools are installed.").arg(toolName());
        SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
        if (feedback) {
            feedback->reportError(err);
        }
        throw QgsProcessingException(err);
    }

    const QStringList args = buildArgs(effectiveParams, context, feedback);
    if (args.isEmpty()) {
        const QString err = QObject::tr("Failed to build arguments for GDAL tool '%1'.").arg(toolName());
        SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
        if (feedback) {
            feedback->reportError(err);
        }
        throw QgsProcessingException(err);
    }

    const QString cmdLine = program + " " + args.join(" ");
    if (feedback) {
        feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    }
    SICNU_LOG_INFO(SicnuLogTags::GDAL, cmdLine);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        const QString err = QObject::tr("Failed to start tool: %1").arg(proc.errorString());
        if (feedback) {
            feedback->reportError(err);
        }
        SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
        throw QgsProcessingException(err);
    }

    QString stdinLine = QString::number(effectiveParams.value("X").toDouble(), 'g', 15) + " "
                        + QString::number(effectiveParams.value("Y").toDouble(), 'g', 15);
    if (effectiveParams.contains("Z") && !effectiveParams.value("Z").isNull()) {
        stdinLine += " " + QString::number(effectiveParams.value("Z").toDouble(), 'g', 15);
    }
    stdinLine += "\n";
    proc.write(stdinLine.toUtf8());
    proc.closeWriteChannel();

    // Watchdog (#618/#1056): gdaltransform answers per coordinate and should
    // finish in seconds; a hung process must not block its worker forever.
    // Same 30-minute bound as the sibling gdal_tool_wrapper.
    QElapsedTimer watchdog;
    watchdog.start();
    const qint64 timeoutMs = 30 * 60 * 1000;
    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            if (!proc.waitForFinished(5000))
                SICNU_LOG_WARN(SicnuLogTags::GDAL, QStringLiteral("gdaltransform did not exit after kill"));
            const QString err = QObject::tr("Tool execution canceled by user.");
            feedback->reportError(err);
            // A canceled run must not masquerade as a successful empty result (#1043).
            throw QgsProcessingException(err);
        }
        if (watchdog.elapsed() > timeoutMs) {
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            const QString err = QObject::tr("Tool timed out after %1 s and was terminated.")
                                    .arg(timeoutMs / 1000);
            if (feedback) {
                feedback->reportError(err);
            }
            SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
            throw QgsProcessingException(err);
        }
        proc.waitForReadyRead(100);
        const QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty() && feedback) {
            feedback->pushInfo(QString::fromUtf8(output));
        }
    }

    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(5000);
    }
    const QByteArray stdoutData = proc.readAllStandardOutput();
    const QByteArray stderrData = proc.readAllStandardError();

    if (proc.exitCode() != 0) {
        const QString err = QObject::tr("Tool failed with exit code %1: %2")
                                .arg(proc.exitCode())
                                .arg(QString::fromUtf8(stderrData.isEmpty() ? stdoutData : stderrData));
        if (feedback) {
            feedback->reportError(err);
        }
        SICNU_LOG_WARN(SicnuLogTags::GDAL, err);
        throw QgsProcessingException(err);
    }

    const QString outputPath = effectiveParams.value("OUTPUT").toString();
    if (!outputPath.isEmpty()) {
        // Ensure parent directory exists (parameterAsOutputLayer already does, but guard anyway)
        QFileInfo fi(outputPath);
        QDir().mkpath(fi.absolutePath());
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            const QString err = QObject::tr("Failed to write output file: %1").arg(outputPath);
            if (feedback) {
                feedback->reportError(err);
            }
            SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
            throw QgsProcessingException(err);
        }
        QTextStream stream(&file);
        stream << QString::fromUtf8(stdoutData);
        stream.flush();
        file.close();
        // A short write or failed close must fail the run, not report success
        // with a truncated OUTPUT file (#1043).
        if (stream.status() == QTextStream::WriteFailed || file.error() != QFileDevice::NoError) {
            const QString err = QObject::tr("Failed to write transform result to output file: %1").arg(outputPath);
            file.remove();
            if (feedback) {
                feedback->reportError(err);
            }
            SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
            throw QgsProcessingException(err);
        }
    }

    SICNU_LOG_SUCCESS(SicnuLogTags::GDAL,
                      QString("GDAL tool '%1' completed successfully").arg(toolName()));

    QVariantMap results;
    if (!outputPath.isEmpty()) {
        results["OUTPUT"] = outputPath;
    }
    return results;
}