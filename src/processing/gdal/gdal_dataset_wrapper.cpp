// src/processing/gdal/gdal_dataset_wrapper.cpp — GDAL C API wrapper
#include "gdal_dataset_wrapper.h"

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <QFile>
#include <mutex>

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

    if (!QFile::exists(path)) {
        m_lastError = QStringLiteral("File not found: %1").arg(path);
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
        GDALClose(static_cast<GDALDatasetH>(m_dataset));
        m_dataset = nullptr;
    }
}

bool GdalDatasetWrapper::isValid() const
{
    return m_dataset != nullptr;
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
                     QString *errorMessage)
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

    GDALClose(ds);
    return true;
}
