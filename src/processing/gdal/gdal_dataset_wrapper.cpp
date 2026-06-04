// src/processing/gdal/gdal_dataset_wrapper.cpp — GDAL C API wrapper
#include "gdal_dataset_wrapper.h"

#include <gdal.h>
#include <cpl_error.h>
#include <QFile>
#include <mutex>

// Ensure GDAL drivers are registered (once per process, thread-safe)
static void ensureGdalInit()
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
