// src/processing/providers/otb_tools/algorithms/otb_pixel_info.cpp
#include "otb_pixel_info.h"
#include "tools/tool_path_manager.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsexception.h>
#include <qgsrasterlayer.h>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>

void OtbPixelInfoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("X", "X coordinate (pixel column)",
                                                  Qgis::ProcessingNumberParameterType::Integer, 0, false, 0));
    addParameter(new QgsProcessingParameterNumber("Y", "Y coordinate (pixel row)",
                                                  Qgis::ProcessingNumberParameterType::Integer, 0, false, 0));
}

QStringList OtbPixelInfoAlgorithm::buildArgs(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-coordx" << QString::number(parameters.value("X").toInt());
    args << "-coordy" << QString::number(parameters.value("Y").toInt());

    return args;
}

QVariantMap OtbPixelInfoAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                     QgsProcessingContext &context,
                                                     QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().otbToolPath(applicationName());
    if (program.isEmpty()) {
        const QString err = QObject::tr("OTB application '%1' not found. Ensure OTB is installed.").arg(applicationName());
        if (feedback)
            feedback->reportError(err);
        // A missing tool is a failure, not an empty success (#1043).
        throw QgsProcessingException(err);
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) {
        const QString err = QObject::tr("Failed to build arguments for OTB application '%1'.").arg(applicationName());
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    if (feedback)
        feedback->pushInfo(QObject::tr("Running: %1 %2").arg(program, args.join(" ")));

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if ( const QString bundleDir = ToolPathManager::instance().otbBundleDir(); !bundleDir.isEmpty() )
    {
        const QString appPath = QDir( bundleDir ).filePath( QStringLiteral( "lib/otb/applications" ) );
        const QString binPath = QDir( bundleDir ).filePath( QStringLiteral( "bin" ) );
        env.insert( QStringLiteral( "OTB_APPLICATION_PATH" ), appPath );
        const QString path = env.value( QStringLiteral( "PATH" ) );
        const QString listSep = QString( QDir::listSeparator() );
        env.insert( QStringLiteral( "PATH" ), binPath + ( path.isEmpty() ? QString() : listSep + path ) );
        env.insert( QStringLiteral( "LC_NUMERIC" ), QStringLiteral( "C" ) );
    }
    proc.setProcessEnvironment( env );

    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        const QString err = QObject::tr("Failed to start OTB application: %1").arg(proc.errorString());
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    // Watchdog: a hung OTB application must not block its worker forever
    // (same 60-minute bound as the sibling otb_tool_wrapper).
    QElapsedTimer watchdog;
    watchdog.start();
    const qint64 timeoutMs = 60 * 60 * 1000;
    QByteArray allOutput;
    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            const QString err = QObject::tr("OTB application canceled by user.");
            feedback->reportError(err);
            throw QgsProcessingException(err);
        }
        if (watchdog.elapsed() > timeoutMs) {
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            const QString err = QObject::tr("OTB application timed out after %1 s and was terminated.")
                                    .arg(timeoutMs / 1000);
            if (feedback)
                feedback->reportError(err);
            throw QgsProcessingException(err);
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            allOutput.append(output);
            if (feedback)
                feedback->pushInfo(QString::fromUtf8(output));
        }
    }
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(5000);
    }
    QByteArray finalOutput = proc.readAllStandardOutput();
    if (!finalOutput.isEmpty()) {
        allOutput.append(finalOutput);
        if (feedback)
            feedback->pushInfo(QString::fromUtf8(finalOutput));
    }

    if (proc.exitCode() != 0) {
        const QString err = QObject::tr("OTB application failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(allOutput).trimmed());
        if (feedback) {
            feedback->reportError(err);
        }
        throw QgsProcessingException(err);
    }

    QVariantMap results;
    results["OUTPUT"] = QObject::tr("Pixel info retrieved successfully");
    return results;
}
