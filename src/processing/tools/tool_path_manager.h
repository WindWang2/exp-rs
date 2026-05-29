// src/processing/tools/tool_path_manager.h
#pragma once

#include <QString>

class ToolPathManager
{
public:
    static ToolPathManager &instance();

    // GDAL tools
    QString gdalToolPath(const QString &toolName) const;
    bool isGdalAvailable() const;

    // OTB tools
    QString otbToolPath(const QString &appName) const;
    bool isOtbAvailable() const;

    // Set custom paths (for user configuration)
    void setGdalPath(const QString &path);
    void setOtbPath(const QString &path);

private:
    ToolPathManager();
    QString findInAppDir(const QString &subdir, const QString &toolName) const;
    QString findInEnv(const QString &envVar, const QString &toolName) const;
    QString findInSystemPath(const QString &toolName) const;

    QString m_customGdalPath;
    QString m_customOtbPath;
};
