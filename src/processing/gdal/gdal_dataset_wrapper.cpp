// src/processing/gdal/gdal_dataset_wrapper.cpp — GDAL C API wrapper
#include "gdal_dataset_wrapper.h"

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <QFile>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>

#include "qgsdatasourceresolver.h"

// Ensure GDAL drivers are registered (once per process, thread-safe)
void ensureGdalInit()
{
    static std::once_flag flag;
    std::call_once(flag, []() { GDALAllRegister(); });
}

GdalDatasetWrapper::GdalDatasetWrapper() = default;

GdalDatasetWrapper::~GdalDatasetWrapper() { close(); }

GdalDatasetWrapper::GdalDatasetWrapper(GdalDatasetWrapper &&other) noexcept
    : m_dataset(other.m_dataset)
{
    other.m_dataset = nullptr;
}

GdalDatasetWrapper &GdalDatasetWrapper::operator=(GdalDatasetWrapper &&other) noexcept
{
    if (this != &other) {
        close();
        m_dataset = other.m_dataset;
        other.m_dataset = nullptr;
    }
    return *this;
}

bool GdalDatasetWrapper::open(const QString &path)
{
    ensureGdalInit();

    close();
    m_lastError.clear();

    if ( QgsDataSourceResolver::requiresLocalExistenceCheck( path ) && !QFile::exists( path ) )
    {
        m_lastError = QStringLiteral( "File not found: %1" ).arg( path );
        return false;
    }

    m_dataset = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
    if (!m_dataset) {
        const char *msg = CPLGetLastErrorMsg();
        m_lastError = msg ? QString::fromUtf8(msg) : QStringLiteral("Unknown GDAL error");
        return false;
    }
    return true;
}

void GdalDatasetWrapper::close()
{
    if (m_dataset) {
        CPLErrorReset();
        GDALClose(static_cast<GDALDatasetH>(m_dataset));
        if ( CPLGetLastErrorType() == CE_Failure )
        {
            qWarning() << "GdalDatasetWrapper::close flush error:" << CPLGetLastErrorMsg();
            CPLErrorReset();
        }
        m_dataset = nullptr;
    }
}

bool GdalDatasetWrapper::isValid() const
{
    return m_dataset != nullptr;
}

void *GdalDatasetWrapper::dataset() const
{
    return m_dataset;
}

bool GdalDatasetWrapper::create(const QString &path, int width, int height, int bandCount,
                                int dtype, const std::array<double, 6> &geoTransform,
                                const QString &projection, QString *errorMessage)
{
    close();
    m_lastError.clear();

    if (width <= 0 || height <= 0 || bandCount <= 0) {
        m_lastError = QStringLiteral("Invalid raster dimensions or band count");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        m_lastError = QStringLiteral("GeoTIFF driver not available");
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    char **opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    if (width >= 256 && height >= 256) {
        opts = CSLSetNameValue(opts, "TILED", "YES");
        opts = CSLSetNameValue(opts, "BLOCKXSIZE", "256");
        opts = CSLSetNameValue(opts, "BLOCKYSIZE", "256");
    }
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(),
                                 width, height, bandCount,
                                 static_cast<GDALDataType>(dtype), opts);
    CSLDestroy(opts);
    if (!ds) {
        const char *msg = CPLGetLastErrorMsg();
        m_lastError = msg ? QString::fromUtf8(msg)
                          : QStringLiteral("Failed to create output file: %1").arg(path);
        if (errorMessage) *errorMessage = m_lastError;
        return false;
    }

    if (GDALSetGeoTransform(ds, const_cast<double*>(geoTransform.data())) != CE_None) {
        m_lastError = QStringLiteral("Failed to write geotransform");
        if (errorMessage) *errorMessage = m_lastError;
        GDALClose(ds);
        return false;
    }
    if (!projection.isEmpty() && GDALSetProjection(ds, projection.toUtf8().constData()) != CE_None) {
        m_lastError = QStringLiteral("Failed to write projection");
        if (errorMessage) *errorMessage = m_lastError;
        GDALClose(ds);
        return false;
    }

    m_dataset = ds;
    return true;
}

int GdalDatasetWrapper::width() const
{
    if (!m_dataset) return 0;
    return GDALGetRasterXSize(static_cast<GDALDatasetH>(m_dataset));
}

int GdalDatasetWrapper::height() const
{
    if (!m_dataset) return 0;
    return GDALGetRasterYSize(static_cast<GDALDatasetH>(m_dataset));
}

int GdalDatasetWrapper::bandCount() const
{
    if (!m_dataset) return 0;
    return GDALGetRasterCount(static_cast<GDALDatasetH>(m_dataset));
}

QString GdalDatasetWrapper::driverName() const
{
    if (!m_dataset) return {};
    GDALDriverH driver = GDALGetDatasetDriver(static_cast<GDALDatasetH>(m_dataset));
    if (!driver) return {};
    return QString::fromUtf8(GDALGetDriverShortName(driver));
}

QString GdalDatasetWrapper::projection() const
{
    if (!m_dataset) return {};
    const char *ref = GDALGetProjectionRef(static_cast<GDALDatasetH>(m_dataset));
    return ref ? QString::fromUtf8(ref) : QString();
}

std::array<double, 6> GdalDatasetWrapper::geoTransform() const
{
    std::array<double, 6> gt = {};
    if (m_dataset) {
        GDALGetGeoTransform(static_cast<GDALDatasetH>(m_dataset), gt.data());
    }
    return gt;
}

bool GdalDatasetWrapper::hasGeoTransform() const
{
    if (!m_dataset)
        return false;
    std::array<double, 6> gt = {};
    return GDALGetGeoTransform(static_cast<GDALDatasetH>(m_dataset), gt.data()) == CE_None;
}

bool GdalDatasetWrapper::readBandData(int bandNum, float *buffer, int dstWidth, int dstHeight) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !buffer)
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    CPLErr err = GDALRasterIO(band, GF_Read,
                              0, 0, GDALGetRasterBandXSize(band), GDALGetRasterBandYSize(band),
                              buffer, dstWidth, dstHeight, GDT_Float32,
                              0, 0);
    return err == CE_None;
}

bool GdalDatasetWrapper::readBandWindow(int bandNum, int xOff, int yOff,
                                        int srcWidth, int srcHeight, float *buffer) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !buffer)
        return false;
    if (srcWidth <= 0 || srcHeight <= 0 || xOff < 0 || yOff < 0)
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    const int bw = GDALGetRasterBandXSize(band);
    const int bh = GDALGetRasterBandYSize(band);
    if (xOff >= bw || yOff >= bh)
        return false;

    // Pad out-of-raster regions with the band's NoData (NaN when unset) instead
    // of leaving them uninitialized / zero-filled: zero is a real pixel value
    // for DN imagery and silently corrupts downstream statistics.
    bool hasNodata = false;
    const double nd = bandNoDataValue( bandNum, &hasNodata );
    const float pad = hasNodata ? static_cast<float>( nd )
                                : std::numeric_limits<float>::quiet_NaN();
    std::fill( buffer, buffer + static_cast<size_t>( srcWidth ) * srcHeight, pad );

    const int clampedWidth = (std::min)(srcWidth, bw - xOff);
    const int clampedHeight = (std::min)(srcHeight, bh - yOff);

    CPLErr err = GDALRasterIO(band, GF_Read,
                              xOff, yOff, clampedWidth, clampedHeight,
                              buffer, clampedWidth, clampedHeight, GDT_Float32,
                              sizeof(float), static_cast<GSpacing>(srcWidth) * sizeof(float));
    return err == CE_None;
}

bool GdalDatasetWrapper::readWindowBip(const std::vector<int> &bands, int xOff, int yOff,
                                       int srcWidth, int srcHeight, float *bipBuffer) const
{
    if (!m_dataset || bands.empty() || !bipBuffer)
        return false;
    if (srcWidth <= 0 || srcHeight <= 0 || xOff < 0 || yOff < 0)
        return false;

    const int totalBands = bandCount();
    for (int b : bands) {
        if (b < 1 || b > totalBands)
            return false;
    }

    const int bw = width();
    const int bh = height();
    if (xOff >= bw || yOff >= bh)
        return false;

    const int nBands = static_cast<int>(bands.size());
    const size_t totalFloats = static_cast<size_t>(srcWidth) * srcHeight * nBands;

    std::fill(bipBuffer, bipBuffer + totalFloats, std::numeric_limits<float>::quiet_NaN());

    const int clampedWidth = (std::min)(srcWidth, bw - xOff);
    const int clampedHeight = (std::min)(srcHeight, bh - yOff);

    CPLErr err = GDALDatasetRasterIO(
        static_cast<GDALDatasetH>(m_dataset), GF_Read,
        xOff, yOff, clampedWidth, clampedHeight,
        bipBuffer, clampedWidth, clampedHeight, GDT_Float32,
        nBands, const_cast<int *>(bands.data()),
        /* nPixelSpace */ sizeof(float) * nBands,
        /* nLineSpace  */ static_cast<GSpacing>(srcWidth) * sizeof(float) * nBands,
        /* nBandSpace  */ sizeof(float)
    );

    return err == CE_None;
}

bool GdalDatasetWrapper::readBandWindowScaled(int bandNum, int xOff, int yOff,
                                              int srcWidth, int srcHeight, float *buffer,
                                              int bufWidth, int bufHeight, float nodata) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !buffer)
        return false;
    if (srcWidth <= 0 || srcHeight <= 0 || bufWidth <= 0 || bufHeight <= 0)
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    const int bw = GDALGetRasterBandXSize(band);
    const int bh = GDALGetRasterBandYSize(band);

    // Pre-fill so out-of-raster regions (edge tiles) read as NoData.
    std::fill( buffer, buffer + static_cast<size_t>( bufWidth ) * bufHeight, nodata );

    const int srcX0 = std::max( 0, xOff );
    const int srcX1 = std::min( bw, xOff + srcWidth );
    const int srcY0 = std::max( 0, yOff );
    const int srcY1 = std::min( bh, yOff + srcHeight );

    if ( srcX1 <= srcX0 || srcY1 <= srcY0 )
        return true;

    const int validSrcW = srcX1 - srcX0;
    const int validSrcH = srcY1 - srcY0;

    if ( srcX0 == xOff && srcY0 == yOff && validSrcW == srcWidth && validSrcH == srcHeight )
    {
        GDALRasterIOExtraArg extra;
        INIT_RASTERIO_EXTRA_ARG( extra );
        extra.eResampleAlg = GRIORA_Bilinear;
        CPLErr err = GDALRasterIOEx( band, GF_Read,
                                     xOff, yOff, srcWidth, srcHeight,
                                     buffer, bufWidth, bufHeight, GDT_Float32,
                                     0, 0, &extra );
        return err == CE_None;
    }

    const double scaleX = static_cast<double>( bufWidth ) / srcWidth;
    const double scaleY = static_cast<double>( bufHeight ) / srcHeight;

    int dstX0 = static_cast<int>( std::round( ( srcX0 - xOff ) * scaleX ) );
    int dstY0 = static_cast<int>( std::round( ( srcY0 - yOff ) * scaleY ) );
    int dstX1 = static_cast<int>( std::round( ( srcX1 - xOff ) * scaleX ) );
    int dstY1 = static_cast<int>( std::round( ( srcY1 - yOff ) * scaleY ) );

    dstX0 = std::clamp( dstX0, 0, bufWidth );
    dstX1 = std::clamp( dstX1, dstX0, bufWidth );
    dstY0 = std::clamp( dstY0, 0, bufHeight );
    dstY1 = std::clamp( dstY1, dstY0, bufHeight );

    const int validBufW = dstX1 - dstX0;
    const int validBufH = dstY1 - dstY0;

    if ( validBufW <= 0 || validBufH <= 0 )
        return true;

    float *dstPtr = buffer + static_cast<size_t>( dstY0 ) * bufWidth + dstX0;
    GDALRasterIOExtraArg extra2;
    INIT_RASTERIO_EXTRA_ARG( extra2 );
    extra2.eResampleAlg = GRIORA_Bilinear;
    CPLErr err = GDALRasterIOEx( band, GF_Read,
                                 srcX0, srcY0, validSrcW, validSrcH,
                                 dstPtr, validBufW, validBufH, GDT_Float32,
                                 sizeof( float ), static_cast<GSpacing>( sizeof( float ) * bufWidth ), &extra2 );
    return err == CE_None;
}

bool GdalDatasetWrapper::writeBandWindow(int bandNum, int xOff, int yOff,
                                         int srcWidth, int srcHeight,
                                         const float *buffer) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !buffer)
        return false;
    if (srcWidth <= 0 || srcHeight <= 0 || xOff < 0 || yOff < 0)
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    const int bw = GDALGetRasterBandXSize(band);
    const int bh = GDALGetRasterBandYSize(band);
    if (xOff >= bw || yOff >= bh)
        return false;

    const int originalWidth = srcWidth;
    const int clampedWidth = (std::min)(srcWidth, bw - xOff);
    const int clampedHeight = (std::min)(srcHeight, bh - yOff);

    CPLErr err = GDALRasterIO(band, GF_Write,
                              xOff, yOff, clampedWidth, clampedHeight,
                              const_cast<float *>(buffer), clampedWidth, clampedHeight, GDT_Float32,
                              sizeof(float), static_cast<GSpacing>(originalWidth) * sizeof(float));
    return err == CE_None;
}

int GdalDatasetWrapper::bandDataType(int bandNum) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount())
        return 0; // GDT_Unknown
    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return 0;
    return static_cast<int>(GDALGetRasterDataType(band));
}

bool GdalDatasetWrapper::readBandDataNative(int bandNum, void *buffer, int dstWidth, int dstHeight) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !buffer)
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    // Native type: no conversion, so the payload matches the on-disk dtype
    // and the Python side can mount it as the same numpy dtype.
    const GDALDataType eType = GDALGetRasterDataType(band);
    CPLErr err = GDALRasterIO(band, GF_Read,
                              0, 0, GDALGetRasterBandXSize(band), GDALGetRasterBandYSize(band),
                              buffer, dstWidth, dstHeight, eType,
                              0, 0);
    return err == CE_None;
}

bool GdalDatasetWrapper::readBandWindowNative(int bandNum, int xOff, int yOff,
                                              int srcWidth, int srcHeight, void *buffer) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !buffer)
        return false;
    if (srcWidth <= 0 || srcHeight <= 0 || xOff < 0 || yOff < 0)
        return false;
    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;
    const GDALDataType eType = GDALGetRasterDataType(band);
    const int bw = GDALGetRasterBandXSize(band);
    const int bh = GDALGetRasterBandYSize(band);
    if (xOff >= bw || yOff >= bh)
        return false;
    const int clampedW = std::min(srcWidth, bw - xOff);
    const int clampedH = std::min(srcHeight, bh - yOff);
    const size_t elemSize = static_cast<size_t>(GDALGetDataTypeSizeBytes(eType));
    if (elemSize == 0)
        return false;
    // Pad out-of-raster with zeros (native type) — caller will handle nodata
    std::memset(buffer, 0, static_cast<size_t>(srcWidth) * srcHeight * elemSize);
    CPLErr err = GDALRasterIO(band, GF_Read,
                              xOff, yOff, clampedW, clampedH,
                              buffer, clampedW, clampedH, eType,
                              elemSize, static_cast<GSpacing>(srcWidth) * elemSize);
    return err == CE_None;
}

bool GdalDatasetWrapper::readPixel(int bandNum, int x, int y, float *value) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount() || !value)
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    CPLErr err = GDALRasterIO(band, GF_Read,
                              x, y, 1, 1,
                              value, 1, 1, GDT_Float32,
                              0, 0);
    return err == CE_None;
}

double GdalDatasetWrapper::bandNoDataValue(int bandNum, bool *hasNodata) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount()) {
        if (hasNodata) *hasNodata = false;
        return 0.0;
    }

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band) {
        if (hasNodata) *hasNodata = false;
        return 0.0;
    }

    int hasNodataInt = 0;
    double nodata = GDALGetRasterNoDataValue(band, &hasNodataInt);
    if (hasNodata) *hasNodata = (hasNodataInt != 0);
    return nodata;
}

bool GdalDatasetWrapper::setBandNoDataValue(int bandNum, double nodata) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount())
        return false;

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return false;

    return GDALSetRasterNoDataValue(band, nodata) == CE_None;
}

QString GdalDatasetWrapper::bandDescription(int bandNum) const
{
    if (!m_dataset || bandNum < 1 || bandNum > bandCount())
        return {};

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return {};

    const char *desc = GDALGetDescription(band);
    return (desc && desc[0]) ? QString::fromUtf8(desc) : QString();
}

QString GdalDatasetWrapper::bandMetadataItem(int bandNum, const char *item) const
{
    if (!m_dataset || !item || !item[0] || bandNum < 1 || bandNum > bandCount())
        return {};

    GDALRasterBandH band = GDALGetRasterBand(static_cast<GDALDatasetH>(m_dataset), bandNum);
    if (!band)
        return {};

    const char *value = GDALGetMetadataItem(band, item, nullptr);
    return (value && value[0]) ? QString::fromUtf8(value) : QString();
}

QString GdalDatasetWrapper::lastError() const
{
    return m_lastError;
}

// --- Free function: createOutputTiff ---

GDALDatasetH createOutputTiff(const QString &path,
                               int width, int height, int bandCount,
                               int dtype,
                               const std::array<double, 6> &geoTransform,
                               const QString &projection,
                               QString *errorMessage)
{
    ensureGdalInit();

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        if (errorMessage) *errorMessage = QStringLiteral("GeoTIFF driver not available");
        return nullptr;
    }

    char **opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    if (width >= 256 && height >= 256) {
        opts = CSLSetNameValue(opts, "TILED", "YES");
        opts = CSLSetNameValue(opts, "BLOCKXSIZE", "256");
        opts = CSLSetNameValue(opts, "BLOCKYSIZE", "256");
    }

    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(),
                                  width, height, bandCount, static_cast<GDALDataType>(dtype), opts);
    CSLDestroy(opts);

    if (!ds) {
        const char *msg = CPLGetLastErrorMsg();
        if (errorMessage) {
            *errorMessage = msg ? QString::fromUtf8(msg)
                                : QStringLiteral("Failed to create output file: %1").arg(path);
        }
        return nullptr;
    }

    GDALSetGeoTransform(ds, const_cast<double *>(geoTransform.data()));
    GDALSetProjection(ds, projection.toUtf8().constData());

    return ds;
}

// --- Free function: extractGeoInfo ---

GeoInfo extractGeoInfo(GDALDatasetH ds)
{
    GeoInfo info;
    if (!ds) return info;

    GDALGetGeoTransform(ds, info.geoTransform.data());
    const char *proj = GDALGetProjectionRef(ds);
    info.projection = proj ? QString::fromUtf8(proj) : QString();

    return info;
}

// --- Free function: writeGdalOutput ---

bool writeGdalOutput(const QString &outputPath, int width, int height,
                     const std::vector<std::vector<float>> &bands,
                     const std::array<double, 6> &geoTransform,
                     const QString &projection,
                     QString *errorMessage,
                     std::optional<double> nodata)
{
    if (bands.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No band data to write");
        return false;
    }

    int bandCount = static_cast<int>(bands.size());

    GDALDatasetH ds = createOutputTiff(outputPath, width, height, bandCount,
                                        GDT_Float32, geoTransform, projection, errorMessage);
    if (!ds) return false;

    for (int b = 0; b < bandCount; ++b) {
        GDALRasterBandH dstBand = GDALGetRasterBand(ds, b + 1);
        if (!dstBand) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to get output band %1").arg(b + 1);
            GDALClose(ds);
            return false;
        }
        if (nodata.has_value()) {
            GDALSetRasterNoDataValue(dstBand, *nodata);
        }
        CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                                   const_cast<float *>(bands[b].data()),
                                   width, height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            const char *msg = CPLGetLastErrorMsg();
            if (errorMessage) {
                *errorMessage = msg ? QString::fromUtf8(msg)
                                    : QStringLiteral("Failed to write band %1").arg(b + 1);
            }
            GDALClose(ds);
            return false;
        }
    }

    CPLErrorReset();
    if ( GDALFlushCache(ds) != CE_None || CPLGetLastErrorType() == CE_Failure )
    {
        const char *msg = CPLGetLastErrorMsg();
        if (errorMessage) *errorMessage = msg ? QString::fromUtf8(msg) : QStringLiteral("Flush failed");
        GDALClose(ds);
        if ( CPLGetLastErrorType() == CE_Failure )
            CPLErrorReset();
        return false;
    }
    CPLErrorReset();
    GDALClose(ds);
    if ( CPLGetLastErrorType() == CE_Failure )
    {
        const char *msg = CPLGetLastErrorMsg();
        if (errorMessage) *errorMessage = msg ? QString::fromUtf8(msg) : QStringLiteral("Close failed");
        CPLErrorReset();
        return false;
    }
    return true;
}
