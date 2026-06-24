// src/processing/tools/tool_path_manager.cpp
#include "tool_path_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

ToolPathManager &ToolPathManager::instance()
{
    static ToolPathManager s_instance;
    return s_instance;
}

ToolPathManager::ToolPathManager() = default;

QString ToolPathManager::gdalToolPath(const QString &toolName) const
{
    QMutexLocker locker(&m_mutex);
    QString customPath = m_customGdalPath;
    locker.unlock();

    // 1. Custom path
    if (!customPath.isEmpty()) {
        QString p = QDir(customPath).filePath(toolName);
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
    QMutexLocker locker(&m_mutex);
    QString customPath = m_customOtbPath;
    locker.unlock();

    QString cliName = "otbcli_" + appName;

    // 1. Custom path
    if (!customPath.isEmpty()) {
        QString p = QDir(customPath).filePath(cliName);
        if (QFileInfo::exists(p)) return p;
    }

    // 2. App directory (bundled OTB — primary path for packaged builds)
    QString appPath = findInAppDir("tools/otb", cliName);
    if (!appPath.isEmpty()) return appPath;

    // 3. App directory relative to binary (for development builds)
    QString appDir = QCoreApplication::applicationDirPath();
    QString devPath = QDir(appDir).filePath("../tools/otb/" + cliName);
    if (QFileInfo::exists(devPath)) return QDir::cleanPath(devPath);

    // 4. Environment variable
    QString envPath = findInEnv("SICNU_OTB_PATH", cliName);
    if (!envPath.isEmpty()) return envPath;

    // 5. System PATH (fallback)
    return findInSystemPath(cliName);
}

bool ToolPathManager::isOtbAvailable() const
{
    // OTB is bundled — always available in packaged builds
    // Check for any OTB CLI tool as indicator
    return !otbToolPath("BandMath").isEmpty() || !otbToolPath("ExtractROI").isEmpty();
}

void ToolPathManager::setGdalPath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_customGdalPath = path;
}

void ToolPathManager::setOtbPath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
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
    // Cross-platform executable lookup (works on Linux, macOS, Windows)
    QString result = QStandardPaths::findExecutable(toolName);
    return result.isEmpty() ? QString() : result;
}
