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
#include <QMessageBox>

#include "app/app_paths.h"

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

    const QString shipped = AppPaths::resolveDataPath( "data/tools/custom" );
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
            QMessageBox::warning(nullptr, QObject::tr("Tool Configuration Error"),
                                 QObject::tr("Failed to parse %1: %2").arg(fileInfo.fileName(), error.errorString()));
            continue;
        }

        QJsonObject config = doc.object();
        QString toolId = config.value("id").toString();
        if (toolId.isEmpty()) {
            QMessageBox::warning(nullptr, QObject::tr("Tool Configuration Error"),
                                 QObject::tr("Tool in %1 has no 'id' field").arg(fileInfo.fileName()));
            continue;
        }

        // Create and register the algorithm
        auto *algorithm = new GenericCliAlgorithm(config, id());
        addAlgorithm(algorithm);
    }
}
