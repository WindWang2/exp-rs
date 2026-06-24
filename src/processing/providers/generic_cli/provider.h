// generic_cli/provider.h — Provider for user-defined CLI tools
#pragma once

#include <processing/qgsprocessingprovider.h>

/**
 * A processing provider that loads generic CLI tool definitions
 * from JSON configuration files in a directory.
 *
 * Configuration directory: ~/.sicnu_geo_rs/tools/ or <app>/tools/
 * Each .json file defines one tool with:
 *   - id, name, command, group, tags
 *   - parameters array
 *   - args template with {PARAM} placeholders
 */
class GenericCliProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    GenericCliProvider(const QString &configDir = QString());

    QString id() const override { return QStringLiteral("custom_tools"); }
    QString name() const override { return QObject::tr("Custom Tools"); }
    QString longName() const override { return QObject::tr("User-defined CLI Tools"); }
    QIcon icon() const override;

    void setConfigDirectory(const QString &dir);
    QString configDirectory() const { return m_configDir; }

protected:
    void loadAlgorithms() override;

private:
    QString m_configDir;
    void loadToolsFromDirectory(const QString &dir);
};
