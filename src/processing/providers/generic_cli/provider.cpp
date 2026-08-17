// generic_cli/provider.cpp — Provider for user-defined CLI tools
#include "provider.h"
#include "generic_cli_algorithm.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QIcon>
#include <qgsmessagelog.h>

#include "framework/runtime_paths.h"

GenericCliProvider::GenericCliProvider(const QString &configDir)
    : m_configDir(configDir)
{
    if (m_configDir.isEmpty()) {
        // Default: ~/.sicnu_geo_rs/tools/
        m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tools";
    }
}

QIcon GenericCliProvider::icon() const
{
    return QIcon::fromTheme("applications-utilities");
}

void GenericCliProvider::setConfigDirectory(const QString &dir)
{
    m_configDir = dir;
}

void GenericCliProvider::loadAlgorithms()
{
    loadToolsFromDirectory(m_configDir);

    const QString shipped = sicnu::processing::resolveRuntimeDataPath( "data/tools/custom" );
    if ( QDir( shipped ).exists() )
        loadToolsFromDirectory( shipped );

    const QString appCustom = QCoreApplication::applicationDirPath() + "/../tools/custom";
    if ( QDir( appCustom ).exists() )
        loadToolsFromDirectory( appCustom );
}

void GenericCliProvider::loadToolsFromDirectory(const QString &dir)
{
    QDir toolDir(dir);
    if (!toolDir.exists())
        return;

    QStringList filters;
    filters << "*.json";
    QFileInfoList files = toolDir.entryInfoList(filters, QDir::Files);

    for (const QFileInfo &fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QByteArray data = file.readAll();
        file.close();

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError) {
            QgsMessageLog::logMessage(
                QObject::tr("Failed to parse tool %1: %2").arg(fileInfo.fileName(), error.errorString()),
                QStringLiteral("generic_cli"), Qgis::MessageLevel::Warning);
            continue;
        }

        QJsonObject config = doc.object();
        QString toolId = config.value("id").toString();
        if (toolId.isEmpty()) {
            QgsMessageLog::logMessage(
                QObject::tr("Tool in %1 has no 'id' field").arg(fileInfo.fileName()),
                QStringLiteral("generic_cli"), Qgis::MessageLevel::Warning);
            continue;
        }

        // Create and register the algorithm
        auto *algorithm = new GenericCliAlgorithm(config, id());
        addAlgorithm(algorithm);
    }
}
