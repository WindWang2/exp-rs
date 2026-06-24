// sicnu_logging.h — Unified logging macros for SICNU GEO RS
//
// Usage:
//   SICNU_LOG_INFO("BandMath", "Expression parsed successfully");
//   SICNU_LOG_WARN("Classification", "KMeans remap failed, using defaults");
//   SICNU_LOG_ERROR("Georeferencer", "Failed to open source raster");
//   SICNU_LOG_DEBUG("Segmentation", "Processing tile %1 of %2", i, total);
//
// All messages go through QgsMessageLog → LogPanel with timestamps.
#pragma once

#include <qgsmessagelog.h>
#include <QString>

// Module tags — use these consistently across the codebase
namespace SicnuLogTags {
    // Processing
    constexpr const char* Algorithms = "Algorithms";
    constexpr const char* Framework  = "Framework";
    constexpr const char* GDAL       = "GDAL";
    constexpr const char* OTB        = "OTB";
    constexpr const char* Providers  = "Providers";

    // Analysis
    constexpr const char* Classification = "Classification";
    constexpr const char* Georeferencing = "Georeferencing";
    constexpr const char* Segmentation   = "Segmentation";

    // App
    constexpr const char* Dialogs        = "Dialogs";
    constexpr const char* Widgets        = "Widgets";
    constexpr const char* MapTools       = "MapTools";
    constexpr const char* OBIA           = "OBIA";
    constexpr const char* Layout         = "Layout";
    constexpr const char* Main           = "Main";

    // Agent
    constexpr const char* MCP            = "MCP";
    constexpr const char* STAC           = "STAC";
}

// Logging macros — single-line, zero overhead when disabled
#define SICNU_LOG_INFO(tag, ...) \
    QgsMessageLog::logMessage(QString(__VA_ARGS__), tag, Qgis::MessageLevel::Info)

#define SICNU_LOG_WARN(tag, ...) \
    QgsMessageLog::logMessage(QString(__VA_ARGS__), tag, Qgis::MessageLevel::Warning)

#define SICNU_LOG_ERROR(tag, ...) \
    QgsMessageLog::logMessage(QString(__VA_ARGS__), tag, Qgis::MessageLevel::Critical)

#define SICNU_LOG_SUCCESS(tag, ...) \
    QgsMessageLog::logMessage(QString(__VA_ARGS__), tag, Qgis::MessageLevel::Success)

// Debug logging — only in debug builds
#ifdef QT_NO_DEBUG
#define SICNU_LOG_DEBUG(tag, ...) ((void)0)
#else
#define SICNU_LOG_DEBUG(tag, ...) \
    QgsMessageLog::logMessage(QString(__VA_ARGS__), tag, Qgis::MessageLevel::Info)
#endif
