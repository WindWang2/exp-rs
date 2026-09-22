// src/processing/providers/otb_tools/algorithms/otb_read_image_info.cpp
#include "otb_read_image_info.h"
#include "tools/tool_path_manager.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsexception.h>
#include <qgsrasterlayer.h>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>

void OtbReadImageInfoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
}

QStringList OtbReadImageInfoAlgorithm::buildArgs(const QVariantMap &parameters,
                                                  QgsProcessingContext &context,
                                                  QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));

    return args;
}

QVariantMap OtbReadImageInfoAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                         QgsProcessingContext &context,
                                                         QgsProcessingFeedback *feedback)
{
    // Fail closed (#1043 contract): every failure below throws — an empty
    // result map reads as success with no output, so "OTB not installed" or
    // a tool crash used to complete the task as a successful no-op.
    QString program = ToolPathManager::instance().otbToolPath(applicationName());
    if (program.isEmpty())
        throw QgsProcessingException(
            QObject::tr("OTB application '%1' not found. Ensure OTB is installed.").arg(applicationName()));

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty())
        throw QgsProcessingException(
            QObject::tr("OTB application '%1' produced no command line.").arg(applicationName()));

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

    if (!proc.waitForStarted(5000))
        throw QgsProcessingException(
            QObject::tr("Failed to start OTB application: %1").arg(proc.errorString()));

    // Watchdog + graceful cancel ladder (#618): terminate first so multi-GB
    // OTB writes can flush, escalate to kill after a grace period, and
    // classify a signal death as a crash (a killed tool reports exitCode 0).
    QElapsedTimer watchdog;
    watchdog.start();
    const qint64 timeoutMs = 60 * 60 * 1000; // OTB composites can be long
    QByteArray allOutput;
    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            throw QgsProcessingException(QObject::tr("OTB application canceled by user."));
        }
        if (watchdog.elapsed() > timeoutMs) {
            proc.terminate();
            if (!proc.waitForFinished(5000))
                proc.kill();
            throw QgsProcessingException(
                QObject::tr("OTB application timed out after %1 s and was terminated.")
                    .arg(timeoutMs / 1000));
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            allOutput.append(output);
            if (feedback)
                feedback->pushInfo(QString::fromUtf8(output));
        }
    }
    QByteArray finalOutput = proc.readAllStandardOutput();
    if (!finalOutput.isEmpty()) {
        allOutput.append(finalOutput);
        if (feedback)
            feedback->pushInfo(QString::fromUtf8(finalOutput));
    }

    if (proc.exitStatus() == QProcess::CrashExit)
        throw QgsProcessingException(QObject::tr("OTB application crashed (killed by signal)."));

    if (proc.exitCode() != 0)
        throw QgsProcessingException(
            QObject::tr("OTB application failed with exit code %1: %2")
                .arg(proc.exitCode())
                .arg(QString::fromUtf8(allOutput).trimmed()));

    QVariantMap results;
    results["OUTPUT"] = QObject::tr("Image info retrieved successfully");
    return results;
}
