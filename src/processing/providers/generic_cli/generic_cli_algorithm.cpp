// generic_cli_algorithm.cpp — Generic CLI tool wrapper for user-defined tools
#include "generic_cli_algorithm.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QFileInfo>
#include <QDir>

#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

GenericCliAlgorithm::GenericCliAlgorithm(const QJsonObject &config, const QString &providerId)
    : m_config(config)
    , m_providerId(providerId)
{
}

QString GenericCliAlgorithm::name() const
{
    return m_config.value("id").toString();
}

QString GenericCliAlgorithm::displayName() const
{
    return m_config.value("name").toString();
}

QString GenericCliAlgorithm::group() const
{
    return m_config.value("group").toString("Custom Tools");
}

QString GenericCliAlgorithm::groupId() const
{
    return m_config.value("group_id").toString("custom");
}

QStringList GenericCliAlgorithm::tags() const
{
    QStringList tags;
    QJsonArray tagArray = m_config.value("tags").toArray();
    for (const QJsonValue &tag : tagArray)
        tags.append(tag.toString());
    return tags;
}

QgsProcessingAlgorithm *GenericCliAlgorithm::createInstance() const
{
    return new GenericCliAlgorithm(m_config, m_providerId);
}

void GenericCliAlgorithm::initAlgorithm(const QVariantMap &)
{
    QJsonArray params = m_config.value("parameters").toArray();
    for (const QJsonValue &paramVal : params) {
        QJsonObject param = paramVal.toObject();
        QString name = param.value("name").toString();
        QString type = param.value("type").toString();
        QString desc = param.value("description").toString();
        QJsonValue defaultVal = param.value("default");

        if (type == "raster") {
            addParameter(new QgsProcessingParameterRasterLayer(name, desc));
        } else if (type == "vector") {
            addParameter(new QgsProcessingParameterVectorLayer(name, desc));
        } else if (type == "number") {
            double def = defaultVal.toDouble(0.0);
            addParameter(new QgsProcessingParameterNumber(name, desc, Qgis::ProcessingNumberParameterType::Double, def));
        } else if (type == "string") {
            addParameter(new QgsProcessingParameterString(name, desc, defaultVal.toString()));
        } else if (type == "boolean") {
            addParameter(new QgsProcessingParameterBoolean(name, desc, defaultVal.toBool()));
        } else if (type == "output_raster") {
            addParameter(new QgsProcessingParameterRasterDestination(name, desc));
        } else if (type == "output_vector") {
            addParameter(new QgsProcessingParameterFeatureSink(name, desc));
        }
    }
}

QStringList GenericCliAlgorithm::buildArgs(const QVariantMap &parameters, QgsProcessingFeedback *feedback) const
{
    QStringList args;
    QJsonArray argTemplate = m_config.value("args").toArray();

    for (const QJsonValue &argVal : argTemplate) {
        QString arg = argVal.toString();

        // Replace parameter placeholders {PARAM_NAME}
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            QString placeholder = "{" + it.key() + "}";
            if (arg.contains(placeholder)) {
                arg.replace(placeholder, it.value().toString());
            }
        }

        // Skip arguments with unresolved placeholders
        if (arg.contains("{") && arg.contains("}")) {
            if (feedback)
                feedback->pushWarning(QObject::tr("Skipping argument with unresolved placeholder: %1").arg(arg));
            continue;
        }

        args.append(arg);
    }

    return args;
}

QVariantMap GenericCliAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                   QgsProcessingContext &context,
                                                   QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);

    QString command = m_config.value("command").toString();
    if (command.isEmpty()) {
        feedback->reportError(QObject::tr("No command specified in tool configuration"));
        return {};
    }

    QStringList args = buildArgs(parameters, feedback);
    QString cmdLine = command + " " + args.join(" ");
    feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    QgsMessageLog::logMessage(cmdLine, "generic_cli", Qgis::MessageLevel::Info);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(command, args);

    if (!proc.waitForStarted(5000)) {
        QString err = QObject::tr("Failed to start tool: %1").arg(proc.errorString());
        feedback->reportError(err);
        QgsMessageLog::logMessage(err, "generic_cli", Qgis::MessageLevel::Critical);
        return {};
    }

    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("Tool execution canceled by user."));
            return {};
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            QString msg = QString::fromUtf8(output);
            if (feedback) feedback->pushInfo(msg);
            QgsMessageLog::logMessage(msg, "generic_cli", Qgis::MessageLevel::Info);
        }
    }

    if (proc.exitCode() != 0) {
        QString err = QObject::tr("Tool failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardOutput()));
        if (feedback) feedback->reportError(err);
        QgsMessageLog::logMessage(err, "generic_cli", Qgis::MessageLevel::Warning);
        return {};
    }

    QVariantMap results;
    // Try to find output parameters
    QJsonArray params = m_config.value("parameters").toArray();
    for (const QJsonValue &paramVal : params) {
        QJsonObject param = paramVal.toObject();
        QString name = param.value("name").toString();
        QString type = param.value("type").toString();
        if (type.startsWith("output_") && parameters.contains(name)) {
            results[name] = parameters.value(name);
        }
    }

    return results;
}
