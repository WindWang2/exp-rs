// src/processing/providers/otb_tools/otb_tool_wrapper.cpp
#include "otb_tool_wrapper.h"
#include "tools/tool_path_manager.h"

#include "core/sicnu_logging.h"
#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <processing/qgsprocessingparameters.h>

QVariantMap OtbToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().otbToolPath(applicationName());
    if (program.isEmpty()) {
        SICNU_LOG_ERROR( SicnuLogTags::OTB, QString( "OTB application '%1' not found — set SICNU_OTB_PATH or configure in Preferences" ).arg( applicationName() ) );
        if (feedback)
            feedback->reportError(QObject::tr(
                "OTB application '%1' not found.\n"
                "Expected at: tools/otb/otbcli_%1\n"
                "Set SICNU_OTB_PATH environment variable or configure OTB path in Preferences.")
                .arg(applicationName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

    SICNU_LOG_INFO( SicnuLogTags::OTB, QString( "Executing OTB application: %1" ).arg( applicationName() ) );
    if (!runOtbApplication(program, args, feedback)) {
        SICNU_LOG_ERROR( SicnuLogTags::OTB, QString( "OTB application '%1' failed" ).arg( applicationName() ) );
        return {};
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::OTB, QString( "OTB application '%1' completed successfully" ).arg( applicationName() ) );
    QVariantMap results;
    if (parameters.contains("OUTPUT")) {
        results["OUTPUT"] = parameters.value("OUTPUT");
    }
    return results;
}

bool OtbToolWrapper::runOtbApplication(const QString &program, const QStringList &args, QgsProcessingFeedback *feedback)
{
    QString cmdLine = program + " " + args.join(" ");
    if (feedback) feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    SICNU_LOG_INFO( SicnuLogTags::OTB, cmdLine );

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        QString err = QObject::tr("Failed to start OTB application: %1").arg(proc.errorString());
        if (feedback) feedback->reportError(err);
        SICNU_LOG_ERROR( SicnuLogTags::OTB, err );
        return false;
    }

    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("OTB application canceled by user."));
            return false;
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            QString msg = QString::fromUtf8(output);
            if (feedback) feedback->pushInfo(msg);
            SICNU_LOG_INFO( SicnuLogTags::OTB, msg );
        }
    }

    if (proc.exitCode() != 0) {
        QString err = QObject::tr("OTB application failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardOutput()));
        if (feedback) feedback->reportError(err);
        SICNU_LOG_WARN( SicnuLogTags::OTB, err );
        return false;
    }

    return true;
}

QString OtbToolWrapper::rasterLayerSource(const QVariant &var)
{
    if (var.canConvert<QgsRasterLayer *>()) {
        QgsRasterLayer *layer = var.value<QgsRasterLayer *>();
        return layer ? layer->source() : QString();
    }
    return var.toString();
}

QString OtbToolWrapper::vectorLayerSource(const QVariant &var)
{
    if (var.canConvert<QgsVectorLayer *>()) {
        QgsVectorLayer *layer = var.value<QgsVectorLayer *>();
        return layer ? layer->source() : QString();
    }
    return var.toString();
}
