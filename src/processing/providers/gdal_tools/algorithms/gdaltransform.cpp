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
    Q_UNUSED(feedback);

    QStringList args;

    // A CRS parameter arrives either as a QgsCoordinateReferenceSystem QVariant
    // (the processing dialog's QgsProcessingCrsWidgetWrapper) or as a string
    // (MCP/CLI). QVariant::toString() on the former yields an empty string, so
    // -s_srs/-t_srs were silently dropped and the tool failed with a usage
    // error; valueAsString() is the contract-compliant conversion (#2221).
    bool sourceOk = false;
    const QString sourceCrs = parameterDefinition(QStringLiteral("SOURCE_CRS"))
                                  ->valueAsString(parameters.value(QStringLiteral("SOURCE_CRS")), context, sourceOk);
    // Gate on the VALUE, not the ok flag: QgsProcessingParameterCrs::
    // valueAsString returns the string for a plain-string value without
    // setting ok (it only sets ok in valueAsStringPrivate), so an ok-gated
    // call silently dropped every MCP/CLI string CRS (#2221 regression).
    if (!sourceCrs.isEmpty()) {
        args << "-s_srs" << sourceCrs;
    }

    bool targetOk = false;
    const QString targetCrs = parameterDefinition(QStringLiteral("TARGET_CRS"))
                                  ->valueAsString(parameters.value(QStringLiteral("TARGET_CRS")), context, targetOk);
    if (!targetCrs.isEmpty()) {
        args << "-t_srs" << targetCrs;
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

    // Watchdog (#1056): the sibling CLI implementations bound a hung tool at
    // 30 minutes; this poll loop lacked it and could block its worker forever.
    QElapsedTimer watchdog;
    watchdog.start();
    constexpr qint64 kWatchdogMs = 30 * 60 * 1000;

    // The result line is this algorithm's ONLY product: accumulate every chunk
    // read inside the poll loop (forwarding to the log is not enough — the
    // post-loop residue is empty on the normal timing, so OUTPUT was written
    // empty and the run still reported success, #2218).
    QByteArray stdoutData;
    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            // Grace ladder (#618): terminate first, escalate to kill — and
            // THROW: an empty result map reads as success with no output on
            // the direct QGIS path.
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            const QString err = QObject::tr("Tool execution canceled by user.");
            if (feedback) {
                feedback->reportError(err);
            }
            throw QgsProcessingException(err);
        }
        if (watchdog.elapsed() > kWatchdogMs) {
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            const QString err = QObject::tr("Tool timed out after 30 minutes and was terminated.");
            if (feedback) {
                feedback->reportError(err);
            }
            SICNU_LOG_ERROR(SicnuLogTags::GDAL, err);
            throw QgsProcessingException(err);
        }
        proc.waitForReadyRead(100);
        const QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            stdoutData.append(output);
            if (feedback) {
                feedback->pushInfo(QString::fromUtf8(output));
            }
        }
    }

    proc.waitForFinished();
    stdoutData.append(proc.readAllStandardOutput());
    const QByteArray stderrData = proc.readAllStandardError();

    // A signal-killed tool reports exitCode() == 0 with CrashExit — treating
    // it as success published whatever partial stdout had arrived.
    if (proc.exitStatus() == QProcess::CrashExit) {
        const QString err = QObject::tr("Tool crashed (killed by signal): %1")
                                .arg(QString::fromUtf8(stderrData.isEmpty() ? stdoutData : stderrData));
        if (feedback) {
            feedback->reportError(err);
        }
        SICNU_LOG_WARN(SicnuLogTags::GDAL, err);
        throw QgsProcessingException(err);
    }

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

    // An empty stdout is not a successful transform: the tool's only product is
    // the result line, so a run without one must fail instead of publishing an
    // empty OUTPUT file and reporting success (#2218).
    if (stdoutData.trimmed().isEmpty()) {
        const QString err = QObject::tr("Tool '%1' produced no output on stdout").arg(toolName());
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
        // A short write or failed close (disk full) must not pass for
        // success: remove the partial file and fail (#1043 contract).
        QTextStream stream(&file);
        stream << QString::fromUtf8(stdoutData);
        stream.flush();
        const bool writeOk = ( stream.status() == QTextStream::Ok )
                             && ( file.error() == QFileDevice::NoError );
        file.close();
        if (!writeOk) {
            file.remove();
            const QString err = QObject::tr("Failed to write output file (short write or flush failure): %1").arg(outputPath);
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