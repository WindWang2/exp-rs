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

    auto fallbackName = [](const QString &name) -> QString {
        if (name.endsWith(QStringLiteral(".py")))
            return name.chopped(3);
        return name + QStringLiteral(".py");
    };

    // 1. Custom path
    if (!customPath.isEmpty()) {
        QString p = QDir(customPath).filePath(toolName);
        if (QFileInfo::exists(p)) return p;
        QString alt = fallbackName(toolName);
        p = QDir(customPath).filePath(alt);
        if (QFileInfo::exists(p)) return p;
    }

    // 2. App directory
    QString appPath = findInAppDir("tools/gdal", toolName);
    if (!appPath.isEmpty()) return appPath;
    {
        QString alt = fallbackName(toolName);
        QString altPath = findInAppDir("tools/gdal", alt);
        if (!altPath.isEmpty()) return altPath;
    }

    // 3. Environment variable
    QString envPath = findInEnv("SICNU_GDAL_PATH", toolName);
    if (!envPath.isEmpty()) return envPath;
    {
        QString alt = fallbackName(toolName);
        QString altEnvPath = findInEnv("SICNU_GDAL_PATH", alt);
        if (!altEnvPath.isEmpty()) return altEnvPath;
    }

    // 4. System PATH
    QString sysPath = findInSystemPath(toolName);
    if (!sysPath.isEmpty()) return sysPath;
    return findInSystemPath(fallbackName(toolName));
}

bool ToolPathManager::isGdalAvailable() const
{
    return !gdalToolPath("gdal_translate").isEmpty();
}

namespace
{
QString findOtbCliInDirectory( const QString &dir, const QString &cliName )
{
    if ( dir.isEmpty() )
        return QString();

    const QStringList candidates = {
        QDir( dir ).filePath( cliName ),
        QDir( dir ).filePath( QStringLiteral( "bin/" ) + cliName ),
    };

    for ( const QString &candidate : candidates )
    {
        if ( QFileInfo::exists( candidate ) )
            return candidate;
    }

    return QString();
}
} // namespace

QString ToolPathManager::otbBundleDir() const
{
    QString appDir = QCoreApplication::applicationDirPath();
    for ( const QString &candidate : {
              // Dev build: binary in ${CMAKE_BINARY_DIR}/, bundle in ${CMAKE_BINARY_DIR}/tools/otb
              QDir( appDir ).filePath( QStringLiteral( "tools/otb" ) ),
              // Installed layout: binary in prefix/bin/, bundle in prefix/tools/otb
              QDir( appDir ).filePath( QStringLiteral( "../tools/otb" ) ),
              QDir( appDir ).filePath( QStringLiteral( "../../tools/otb" ) ),
          } )
    {
        const QString binDir = QDir( candidate ).filePath( QStringLiteral( "bin" ) );
        if ( QFileInfo::exists( QDir( binDir ).filePath( QStringLiteral( "otbcli" ) ) ) )
            return QDir::cleanPath( candidate );
    }
    return QString();
}

QString ToolPathManager::otbToolPath(const QString &appName) const
{
    QMutexLocker locker(&m_mutex);
    QString customPath = m_customOtbPath;
    locker.unlock();

    QString cliName = "otbcli_" + appName;

    // 1. Custom path (directory root or bin/)
    if ( const QString customTool = findOtbCliInDirectory( customPath, cliName ); !customTool.isEmpty() )
        return customTool;

    // 2. Vendored bundle staged by sicnu_otb_bundle (build/tools/otb)
    if ( const QString bundleDir = otbBundleDir(); !bundleDir.isEmpty() )
    {
        if ( const QString bundled = findOtbCliInDirectory( bundleDir, cliName ); !bundled.isEmpty() )
            return bundled;
    }

    // 3. App directory (bundled OTB — primary path for packaged builds)
    if ( const QString appPath = findInAppDir( "tools/otb", cliName ); !appPath.isEmpty() )
        return appPath;
    if ( const QString appBinPath = findInAppDir( "tools/otb/bin", cliName ); !appBinPath.isEmpty() )
        return appBinPath;

    // 5. App directory relative to binary (for development builds)
    QString appDir = QCoreApplication::applicationDirPath();
    for ( const QString &devDir : {
              QDir( appDir ).filePath( QStringLiteral( "tools/otb" ) ),
              QDir( appDir ).filePath( QStringLiteral( "tools/otb/bin" ) ),
              QDir( appDir ).filePath( QStringLiteral( "../tools/otb" ) ),
              QDir( appDir ).filePath( QStringLiteral( "../tools/otb/bin" ) ),
              QDir( appDir ).filePath( QStringLiteral( "../../tools/otb/bin" ) ),
          } )
    {
        if ( const QString devPath = findOtbCliInDirectory( devDir, cliName ); !devPath.isEmpty() )
            return QDir::cleanPath( devPath );
    }

    // 6. Environment variable
    if ( const QString envPath = findInEnv( "SICNU_OTB_PATH", cliName ); !envPath.isEmpty() )
        return envPath;
    const QString envDir = QProcessEnvironment::systemEnvironment().value( QStringLiteral( "SICNU_OTB_PATH" ) );
    if ( const QString envBinPath = findOtbCliInDirectory( envDir, cliName ); !envBinPath.isEmpty() )
        return envBinPath;

    // 7. Build-tree bin/ (otbcli_* emitted next to otbApplicationLauncherCommandLine)
    const QString buildBin = QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "../bin" ) );
    if ( const QString devTool = findOtbCliInDirectory( buildBin, cliName ); !devTool.isEmpty() )
        return QDir::cleanPath( devTool );

    // 8. System PATH (fallback)
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
