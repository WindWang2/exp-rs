// src/processing/tools/tool_path_manager.cpp
#include "tool_path_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QProcess>

ToolPathManager &ToolPathManager::instance()
{
    static ToolPathManager s_instance;
    return s_instance;
}

ToolPathManager::ToolPathManager() = default;

QString ToolPathManager::gdalToolPath(const QString &toolName) const
{
    // 1. Custom path
    if (!m_customGdalPath.isEmpty()) {
        QString p = QDir(m_customGdalPath).filePath(toolName);
        if (QFileInfo::exists(p)) return p;
    }

    // 2. App directory
    QString appPath = findInAppDir("tools/gdal", toolName);
    if (!appPath.isEmpty()) return appPath;

    // 3. Environment variable
    QString envPath = findInEnv("SICNU_GDAL_PATH", toolName);
    if (!envPath.isEmpty()) return envPath;

    // 4. System PATH
    return findInSystemPath(toolName);
}

bool ToolPathManager::isGdalAvailable() const
{
    return !gdalToolPath("gdal_translate").isEmpty();
}

QString ToolPathManager::otbToolPath(const QString &appName) const
{
    QString cliName = "otbcli_" + appName;

    // 1. Custom path
    if (!m_customOtbPath.isEmpty()) {
        QString p = QDir(m_customOtbPath).filePath(cliName);
        if (QFileInfo::exists(p)) return p;
    }

    // 2. App directory
    QString appPath = findInAppDir("tools/otb", cliName);
    if (!appPath.isEmpty()) return appPath;

    // 3. Environment variable
    QString envPath = findInEnv("SICNU_OTB_PATH", cliName);
    if (!envPath.isEmpty()) return envPath;

    // 4. System PATH
    return findInSystemPath(cliName);
}

bool ToolPathManager::isOtbAvailable() const
{
    return !otbToolPath("BandMath").isEmpty();
}

void ToolPathManager::setGdalPath(const QString &path)
{
    m_customGdalPath = path;
}

void ToolPathManager::setOtbPath(const QString &path)
{
    m_customOtbPath = path;
}

QString ToolPathManager::findInAppDir(const QString &subdir, const QString &toolName) const
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString p = QDir(appDir).filePath(subdir + "/" + toolName);
    return QFileInfo::exists(p) ? p : QString();
}

QString ToolPathManager::findInEnv(const QString &envVar, const QString &toolName) const
{
    QString envDir = QProcessEnvironment::systemEnvironment().value(envVar);
    if (envDir.isEmpty()) return QString();
    QString p = QDir(envDir).filePath(toolName);
    return QFileInfo::exists(p) ? p : QString();
}

QString ToolPathManager::findInSystemPath(const QString &toolName) const
{
    QProcess proc;
    proc.start("which", QStringList() << toolName);
    proc.waitForFinished(3000);
    QString result = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return result.isEmpty() ? QString() : result;
}
