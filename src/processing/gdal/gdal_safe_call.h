// gdal_safe_call.h — GDAL safe call wrapper for error handling
#pragma once

#include <stdexcept>
#include <string>
#include <gdal.h>
#include <cpl_error.h>

/**
 * Macro to safely call GDAL functions with automatic error checking.
 * Throws std::runtime_error on failure with the error message.
 *
 * Usage:
 *   GDAL_SAFE_CALL(GDALRasterIO(band, GF_Read, ...), "Failed to read raster");
 */
#define GDAL_SAFE_CALL(expr, errorMsg) \
    do { \
        CPLErr _err = (expr); \
        if (_err != CE_None) { \
            const char *_gdalMsg = CPLGetLastErrorMsg(); \
            std::string _fullMsg = std::string(errorMsg); \
            if (_gdalMsg && _gdalMsg[0]) { \
                _fullMsg += ": "; \
                _fullMsg += _gdalMsg; \
            } \
            throw std::runtime_error(_fullMsg); \
        } \
    } while(0)

/**
 * Safely open a GDAL dataset. Returns nullptr on failure.
 * Logs error via CPLGetLastErrorMsg().
 */
inline GDALDatasetH gdalSafeOpen(const char *path, GDALAccess access = GA_ReadOnly)
{
    GDALDatasetH ds = GDALOpen(path, access);
    if (!ds) {
        const char *msg = CPLGetLastErrorMsg();
        if (msg && msg[0]) {
            // Error already logged by GDAL
        }
    }
    return ds;
}

/**
 * Safely close a GDAL dataset. Handles nullptr.
 */
inline void gdalSafeClose(GDALDatasetH &ds)
{
    if (ds) {
        GDALClose(ds);
        ds = nullptr;
    }
}

/**
 * RAII wrapper for GDAL dataset handles.
 * Automatically closes the dataset on destruction.
 */
class GdalDatasetGuard
{
public:
    GdalDatasetGuard(GDALDatasetH ds = nullptr) : m_ds(ds) {}
    ~GdalDatasetGuard() { close(); }

    // Non-copyable
    GdalDatasetGuard(const GdalDatasetGuard &) = delete;
    GdalDatasetGuard &operator=(const GdalDatasetGuard &) = delete;

    // Move support
    GdalDatasetGuard(GdalDatasetGuard &&other) noexcept : m_ds(other.m_ds) { other.m_ds = nullptr; }
    GdalDatasetGuard &operator=(GdalDatasetGuard &&other) noexcept {
        if (this != &other) { close(); m_ds = other.m_ds; other.m_ds = nullptr; }
        return *this;
    }

    GDALDatasetH get() const { return m_ds; }
    GDALDatasetH release() { GDALDatasetH ds = m_ds; m_ds = nullptr; return ds; }
    void reset(GDALDatasetH ds = nullptr) { close(); m_ds = ds; }
    explicit operator bool() const { return m_ds != nullptr; }

private:
    void close() { if (m_ds) { GDALClose(m_ds); m_ds = nullptr; } }
    GDALDatasetH m_ds;
};
