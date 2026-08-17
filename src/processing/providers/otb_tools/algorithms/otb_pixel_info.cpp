// src/processing/providers/otb_tools/algorithms/otb_pixel_info.cpp
#include "otb_pixel_info.h"
#include "tools/tool_path_manager.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>
#include <QDir>
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
        if (feedback)
            feedback->reportError(QObject::tr("OTB application '%1' not found. Ensure OTB is installed.").arg(applicationName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

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
        if (feedback)
            feedback->reportError(QObject::tr("Failed to start OTB application: %1").arg(proc.errorString()));
        return {};
    }

    QByteArray allOutput;
    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("OTB application canceled by user."));
            return {};
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

    if (proc.exitCode() != 0) {
        if (feedback) {
            feedback->reportError(QObject::tr("OTB application failed with exit code %1: %2")
                .arg(proc.exitCode())
                .arg(QString::fromUtf8(allOutput).trimmed()));
        }
        return {};
    }

    QVariantMap results;
    results["OUTPUT"] = QObject::tr("Pixel info retrieved successfully");
    return results;
}
