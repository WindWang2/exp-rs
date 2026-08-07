// src/processing/gdal/gdal_dataset_wrapper.h
#pragma once

#include <QString>
#include <array>
#include <cstdint>
#include <vector>

// Forward declaration to avoid exposing gdal.h in header
typedef void *GDALDatasetH;

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
 * @param dtype         GDAL data type (e.g. GDT_Float32) - cast to int to avoid including gdal.h
 * @param geoTransform  6-element affine geotransform from source
 * @param projection    WKT projection string from source
 * @param errorMessage  If non-null, receives error description on failure
 * @return GDALDatasetH on success, nullptr on failure
 */
GDALDatasetH createOutputTiff(const QString &path,
                               int width, int height, int bandCount,
                               int dtype,
                               const std::array<double, 6> &geoTransform,
                               const QString &projection,
                               QString *errorMessage = nullptr);

/**
 * Geotransform and projection info extracted from a GDAL dataset.
 */
struct GeoInfo {
    std::array<double, 6> geoTransform = {0, 1, 0, 0, 0, 1};
    QString projection;
};

/**
 * Extract geotransform and projection from a raw GDAL dataset handle.
 * Useful when working with GDALDatasetH directly instead of GdalDatasetWrapper.
 */
GeoInfo extractGeoInfo(GDALDatasetH ds);

/**
 * Write multi-band float data to a new GeoTIFF file.
 *
 * Creates a GeoTIFF with LZW compression and writes all bands in one call.
 * Replaces the common pattern: createOutputTiff + per-band GDALRasterIO loop.
 *
 * @param outputPath    Output file path
 * @param width         Raster width in pixels
 * @param height        Raster height in pixels
 * @param bands         Vector of band data (each band = width*height floats)
 * @param geoTransform  6-element affine geotransform
 * @param projection    WKT projection string
 * @param errorMessage  If non-null, receives error description on failure
 * @return true on success
 */
bool writeGdalOutput(const QString &outputPath, int width, int height,
                     const std::vector<std::vector<float>> &bands,
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

    /**
     * Create a new single-dataset raster file (write access, e.g. a GeoTIFF)
     * with the given dimensions and band count, then keep it open for windowed
     * writes via writeBandWindow(). Use close() (or the destructor) to flush.
     *
     * @param path          Output file path
     * @param width         Raster width in pixels
     * @param height        Raster height in pixels
     * @param bandCount     Number of output bands
     * @param dtype         GDAL data type (e.g. GDT_Float32) as an int
     * @param geoTransform  6-element affine geotransform
     * @param projection    WKT projection string (may be empty)
     * @param errorMessage  optional error sink
     * @return true on success
     */
    bool create(const QString &path, int width, int height, int bandCount,
                int dtype, const std::array<double, 6> &geoTransform,
                const QString &projection, QString *errorMessage = nullptr);

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

    /// True when the dataset carries a real geotransform (GDAL reports one).
    bool hasGeoTransform() const;

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
     * Read a rectangular window of a band into a float buffer (out-of-core
     * streaming support). The window is read at native resolution (no
     * resampling): buffer must hold srcWidth*srcHeight floats.
     * @param bandNum 1-based band number
     * @param xOff    pixel column of the window's left edge (0-based)
     * @param yOff    pixel row of the window's top edge (0-based)
     * @param srcWidth  window width in source pixels
     * @param srcHeight window height in source pixels
     * @param buffer  pre-allocated float buffer of size srcWidth*srcHeight
     * @return true on success
     */
    bool readBandWindow(int bandNum, int xOff, int yOff,
                        int srcWidth, int srcHeight, float *buffer) const;

    /**
     * Write a rectangular window of a band from a float buffer (out-of-core
     * streaming support). The window is written at native resolution: buffer
     * must hold srcWidth*srcHeight floats. The window is clamped to the raster
     * extent.
     * @param bandNum 1-based band number
     * @param xOff    pixel column of the window's left edge (0-based)
     * @param yOff    pixel row of the window's top edge (0-based)
     * @param srcWidth  window width in pixels
     * @param srcHeight window height in pixels
     * @param buffer  float buffer of size srcWidth*srcHeight
     * @return true on success
     */
    bool writeBandWindow(int bandNum, int xOff, int yOff,
                         int srcWidth, int srcHeight, const float *buffer) const;

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
     * Native GDAL data type of a band, returned as an int (GDALDataType)
     * to avoid exposing gdal.h in this header. Use to choose a matching
     * buffer type before readBandDataNative().
     * @param bandNum 1-based band number
     * @return GDALDataType value, or GDT_Unknown (0) on error
     */
    int bandDataType(int bandNum) const;

    /**
     * Read an entire band into a buffer using the band's NATIVE data type
     * (no GDAL conversion). Use bandDataType() to size the buffer.
     * @param bandNum 1-based band number
     * @param buffer  pre-allocated buffer of size
     *                dstWidth * dstHeight * GDALGetDataTypeSize(nativeType)/8
     * @param dstWidth  destination width in pixels
     * @param dstHeight destination height in pixels
     * @return true on success
     */
    bool readBandDataNative(int bandNum, void *buffer, int dstWidth, int dstHeight) const;

    /**
     * Get the no-data value for a band.
     * @param bandNum 1-based band number
     * @param hasNodata set to true if a no-data value is defined
     * @return the no-data value (meaningful only when hasNodata is true)
     */
    double bandNoDataValue(int bandNum, bool *hasNodata = nullptr) const;

    /**
     * Get the GDAL description string for a band (e.g. "B4", "sur_refl_b01").
     * Returns an empty string if no description is set.
     * @param bandNum 1-based band number
     */
    QString bandDescription(int bandNum) const;

    /**
     * Get a metadata item of a band (e.g. "WAVELENGTH", "FWHM",
     * "SICNU_BAND_ROLE"). Returns an empty string if the item is absent, the
     * band is invalid, or the dataset is not open.
     * @param bandNum 1-based band number
     * @param item    metadata item name
     */
    QString bandMetadataItem(int bandNum, const char *item) const;

    /// Get the last error message (empty if no error).
    QString lastError() const;

private:
    void *m_dataset = nullptr; // GDALDatasetH (void* to avoid exposing gdal.h in header)
    mutable QString m_lastError;
};
