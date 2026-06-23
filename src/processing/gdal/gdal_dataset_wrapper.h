// src/processing/gdal/gdal_dataset_wrapper.h
#pragma once

#include <QString>
#include <array>
#include <cstdint>

// Forward declarations to avoid exposing gdal.h in header
typedef void *GDALDatasetH;
typedef int GDALDataType;

/// Ensure GDAL drivers are registered (once per process, thread-safe).
/// Prefer this over calling GDALAllRegister() directly.
void ensureGdalInit();

/**
 * Create a standard GeoTIFF output file with LZW compression.
 *
 * Copies the geotransform and projection from a source dataset.
 * The caller is responsible for closing the returned dataset handle.
 *
 * @param path          Output file path
 * @param width         Raster width in pixels
 * @param height        Raster height in pixels
 * @param bandCount     Number of output bands
 * @param dtype         GDAL data type (e.g. GDT_Float32)
 * @param geoTransform  6-element affine geotransform from source
 * @param projection    WKT projection string from source
 * @param errorMessage  If non-null, receives error description on failure
 * @return GDALDatasetH on success, nullptr on failure
 */
GDALDatasetH createOutputTiff(const QString &path,
                               int width, int height, int bandCount,
                               GDALDataType dtype,
                               const std::array<double, 6> &geoTransform,
                               const QString &projection,
                               QString *errorMessage = nullptr);

/**
 * RAII wrapper around GDAL C API for raster dataset access.
 *
 * Provides safe open/close, metadata queries, and band-level pixel reading.
 * Move-only (no copy) to prevent double-close.
 */
class GdalDatasetWrapper
{
public:
    GdalDatasetWrapper();
    ~GdalDatasetWrapper();

    // Move-only
    GdalDatasetWrapper(GdalDatasetWrapper &&other) noexcept;
    GdalDatasetWrapper &operator=(GdalDatasetWrapper &&other) noexcept;
    GdalDatasetWrapper(const GdalDatasetWrapper &) = delete;
    GdalDatasetWrapper &operator=(const GdalDatasetWrapper &) = delete;

    /// Open a raster file. Returns true on success.
    bool open(const QString &path);

    /// Close the dataset (also called by destructor).
    void close();

    /// True if a dataset is currently open.
    bool isValid() const;

    // --- Metadata ---

    int width() const;
    int height() const;
    int bandCount() const;
    QString driverName() const;
    QString projection() const;

    /// Returns the 6-element affine geotransform [originX, pixelW, rotX, originY, rotY, pixelH].
    std::array<double, 6> geoTransform() const;

    // --- Band reading ---

    /**
     * Read an entire band into a float buffer.
     * @param bandNum 1-based band number
     * @param buffer  pre-allocated buffer of size dstWidth * dstHeight
     * @param dstWidth  destination width in pixels
     * @param dstHeight destination height in pixels
     * @return true on success
     */
    bool readBandData(int bandNum, float *buffer, int dstWidth, int dstHeight) const;

    /**
     * Read a single pixel value.
     * @param bandNum 1-based band number
     * @param x pixel column (0-based)
     * @param y pixel row (0-based)
     * @param value output value
     * @return true on success
     */
    bool readPixel(int bandNum, int x, int y, float *value) const;

    /**
     * Get the no-data value for a band.
     * @param bandNum 1-based band number
     * @param hasNodata set to true if a no-data value is defined
     * @return the no-data value (meaningful only when hasNodata is true)
     */
    double bandNoDataValue(int bandNum, bool *hasNodata = nullptr) const;

    /// Get the last error message (empty if no error).
    QString lastError() const;

private:
    void *m_dataset = nullptr; // GDALDatasetH (void* to avoid exposing gdal.h in header)
    mutable QString m_lastError;
};
