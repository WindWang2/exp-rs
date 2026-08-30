/***************************************************************************
 * satellite_products.cpp  —  Landsat / Sentinel-2 / MODIS product discovery
 ***************************************************************************/
#include "satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <stdexcept>
#include <QFileInfo>

#include "qgsdatasourceresolver.h"
#include <QRegularExpression>
#include <QTextStream>

#include <gdal.h>
#include <gdal_utils.h>
#include <cpl_conv.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using sicnu::data::BandRole;

namespace SatelliteProducts {
namespace {

QString normalizePath(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

bool isLandsatMtlName(const QString& name)
{
    return name.endsWith(QStringLiteral("_MTL.txt"), Qt::CaseInsensitive)
           || name.compare(QStringLiteral("MTL.txt"), Qt::CaseInsensitive) == 0;
}

bool isSentinelMtdName(const QString& name)
{
    return name.startsWith(QStringLiteral("MTD_MSI"), Qt::CaseInsensitive)
           && name.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive);
}

QString findFirstFile(const QDir& dir, const std::function<bool(const QString&)>& pred)
{
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo& fi : entries) {
        if (pred(fi.fileName()))
            return fi.absoluteFilePath();
    }
    return {};
}

QString findMtlInDir(const QDir& dir)
{
    return findFirstFile(dir, isLandsatMtlName);
}

QString findMtdInDir(const QDir& dir)
{
    // Prefer top-level MTD_MSIL*.xml
    QString top = findFirstFile(dir, isSentinelMtdName);
    if (!top.isEmpty())
        return top;
    QDirIterator it(dir.absolutePath(), QStringList{QStringLiteral("MTD_MSI*.xml")},
                    QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext())
        return it.next();
    return {};
}

} // namespace

QMap<QString, QString> parseMtlKeyValues(const QString& mtlPath, QString* error)
{
    QMap<QString, QString> kv;
    QFile f(mtlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Cannot open MTL: %1").arg(mtlPath);
        return kv;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        // GROUP / END_GROUP lines
        if (line.startsWith(QStringLiteral("GROUP")) || line.startsWith(QStringLiteral("END")))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')))
            val = val.mid(1, val.size() - 2);
        kv.insert(key, val);
    }
    return kv;
}

namespace {

QString landsatBandNameFromKey(const QString& key)
{
    // FILE_NAME_BAND_1, FILE_NAME_BAND_QA_PIXEL, FILE_NAME_BAND_ST_B10, ...
    static const QRegularExpression re(
        QStringLiteral(R"(^FILE_NAME_BAND_(.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(key);
    if (!m.hasMatch())
        return {};
    QString suffix = m.captured(1).toUpper();
    if (suffix.startsWith(QStringLiteral("QA")))
        return suffix; // QA_PIXEL, QA_RADSAT
    if (suffix.startsWith(QStringLiteral("ST_")))
        return suffix;
    if (suffix.startsWith(QStringLiteral("SR_")))
        return suffix;
    // numeric
    bool ok = false;
    const int n = suffix.toInt(&ok);
    if (ok)
        return QStringLiteral("B%1").arg(n);
    return QStringLiteral("B%1").arg(suffix);
}

int landsatWavelength(const QString& bandName, bool oli)
{
    // Collection-2 L2 stacks carry SR_Bn/ST_B10 names (#613): strip the
    // prefix so wavelength/FWHM metadata resolves in the primary (MTL-key)
    // discovery path exactly like the directory-scan fallback does.
    QString name = bandName.toUpper();
    if (name.startsWith(QStringLiteral("SR_")) || name.startsWith(QStringLiteral("ST_")))
        name = name.mid(3);
    if (oli) {
        static const QMap<QString, int> wl{
            {QStringLiteral("B1"), 443},  {QStringLiteral("B2"), 482},
            {QStringLiteral("B3"), 561},  {QStringLiteral("B4"), 655},
            {QStringLiteral("B5"), 865},  {QStringLiteral("B6"), 1609},
            {QStringLiteral("B7"), 2201}, {QStringLiteral("B8"), 590},
            {QStringLiteral("B9"), 1373}, {QStringLiteral("B10"), 10895},
            {QStringLiteral("B11"), 12005},
        };
        return wl.value(name, 0);
    }
    // TM/ETM+ centre wavelengths (nm), for Landsat 4-7 (#673).
    static const QMap<QString, int> wl{
        {QStringLiteral("B1"), 485}, {QStringLiteral("B2"), 560},
        {QStringLiteral("B3"), 660}, {QStringLiteral("B4"), 830},
        {QStringLiteral("B5"), 1650}, {QStringLiteral("B6"), 11350},
        {QStringLiteral("B7"), 2220}, {QStringLiteral("B8"), 555},
        {QStringLiteral("B9"), 0}, {QStringLiteral("B10"), 0},
        {QStringLiteral("B11"), 0},
    };
    return wl.value(name, 0);
}

int sentinelWavelength(const QString& bandName)
{
    static const QMap<QString, int> wl{
        {QStringLiteral("B1"), 443},  {QStringLiteral("B2"), 490},
        {QStringLiteral("B3"), 560},  {QStringLiteral("B4"), 665},
        {QStringLiteral("B5"), 705},  {QStringLiteral("B6"), 740},
        {QStringLiteral("B7"), 783},  {QStringLiteral("B8"), 842},
        {QStringLiteral("B8A"), 865}, {QStringLiteral("B9"), 945},
        {QStringLiteral("B10"), 1375},{QStringLiteral("B11"), 1610},
        {QStringLiteral("B12"), 2190},
    };
    return wl.value(bandName.toUpper(), 0);
}

bool bandIsQa(const QString& name)
{
    const QString u = name.toUpper();
    return u.contains(QStringLiteral("QA")) || u.contains(QStringLiteral("SCL"))
           || u.contains(QStringLiteral("AOT")) || u.contains(QStringLiteral("WVP"))
           || u.contains(QStringLiteral("TCI")) || u.contains(QStringLiteral("MSK"));
}

BandRole landsatOliRole(const QString& bandName)
{
    // OLI (Landsat 8/9): B1 Coastal, B2 Blue, B3 Green, B4 Red, B5 NIR,
    // B6 SWIR1, B7 SWIR2, B8 Pan, B9 Cirrus, B10/B11 Thermal
    static const QMap<QString, BandRole> roles{
        {QStringLiteral("B1"), BandRole::Coastal},
        {QStringLiteral("B2"), BandRole::Blue},
        {QStringLiteral("B3"), BandRole::Green},
        {QStringLiteral("B4"), BandRole::Red},
        {QStringLiteral("B5"), BandRole::NIR},
        {QStringLiteral("B6"), BandRole::SWIR1},
        {QStringLiteral("B7"), BandRole::SWIR2},
        {QStringLiteral("B8"), BandRole::Panchromatic},
        {QStringLiteral("B9"), BandRole::Cirrus},
        {QStringLiteral("B10"), BandRole::Thermal},
        {QStringLiteral("B11"), BandRole::Thermal},
    };
    return roles.value(bandName.toUpper(), BandRole::Unknown);
}

BandRole legacyLandsatRole(const QString& bandName)
{
    // TM/ETM (Landsat 4-7): B1 Blue, B2 Green, B3 Red, B4 NIR, B5 SWIR1,
    // B6 Thermal, B7 SWIR2, B8 Pan (ETM+)
    static const QMap<QString, BandRole> roles{
        {QStringLiteral("B1"), BandRole::Blue},
        {QStringLiteral("B2"), BandRole::Green},
        {QStringLiteral("B3"), BandRole::Red},
        {QStringLiteral("B4"), BandRole::NIR},
        {QStringLiteral("B5"), BandRole::SWIR1},
        {QStringLiteral("B6"), BandRole::Thermal},
        {QStringLiteral("B7"), BandRole::SWIR2},
        {QStringLiteral("B8"), BandRole::Panchromatic},
    };
    return roles.value(bandName.toUpper(), BandRole::Unknown);
}

int landsatWavelength(const QString& bandName)
{ return landsatWavelength(bandName, true); }

int landsatFwhmNm(const QString& bandName, bool oli)
{
    QString name = bandName.toUpper();
    if (name.startsWith(QStringLiteral("SR_")) || name.startsWith(QStringLiteral("ST_")))
        name = name.mid(3);
    if (oli) {
        // Approximate OLI band widths (nm)
        static const QMap<QString, int> fwhm{
            {QStringLiteral("B1"), 20},  {QStringLiteral("B2"), 65},
            {QStringLiteral("B3"), 60},  {QStringLiteral("B4"), 30},
            {QStringLiteral("B5"), 77},  {QStringLiteral("B6"), 90},
            {QStringLiteral("B7"), 180}, {QStringLiteral("B8"), 172},
            {QStringLiteral("B9"), 30},  {QStringLiteral("B10"), 570},
            {QStringLiteral("B11"), 690},
        };
        return fwhm.value(name, 0);
    }
    // Approximate TM/ETM band widths (nm)
    static const QMap<QString, int> fwhm{
        {QStringLiteral("B1"), 70},  {QStringLiteral("B2"), 80},
        {QStringLiteral("B3"), 60},  {QStringLiteral("B4"), 140},
        {QStringLiteral("B5"), 200}, {QStringLiteral("B6"), 2100},
        {QStringLiteral("B7"), 260}, {QStringLiteral("B8"), 310},
    };
    return fwhm.value(name, 0);
}

int sentinelFwhmNm(const QString& bandName)
{
    // Approximate MSI band widths (nm)
    static const QMap<QString, int> fwhm{
        {QStringLiteral("B1"), 21},  {QStringLiteral("B2"), 66},
        {QStringLiteral("B3"), 36},  {QStringLiteral("B4"), 31},
        {QStringLiteral("B5"), 15},  {QStringLiteral("B6"), 15},
        {QStringLiteral("B7"), 20},  {QStringLiteral("B8"), 106},
        {QStringLiteral("B8A"), 21}, {QStringLiteral("B9"), 20},
        {QStringLiteral("B10"), 30}, {QStringLiteral("B11"), 91},
        {QStringLiteral("B12"), 175},
    };
    return fwhm.value(bandName.toUpper(), 0);
}

const BandFile* findBand(const QVector<BandFile>& bands, const QString& want)
{
    const QString w = want.trimmed().toUpper();
    for (const BandFile& b : bands) {
        const QString n = b.name.toUpper();
        if (n == w || n == QStringLiteral("B") + w)
            return &b;
        // B02 vs B2
        if (w.size() >= 2 && w[0] == QLatin1Char('B') && n.size() >= 2 && n[0] == QLatin1Char('B')) {
            QString nw = w.mid(1);
            QString nn = n.mid(1);
            while (nw.startsWith(QLatin1Char('0')))
                nw.remove(0, 1);
            while (nn.startsWith(QLatin1Char('0')))
                nn.remove(0, 1);
            if (nw == nn && !nw.isEmpty())
                return &b;
        }
    }
    bool ok = false;
    const int num = w.toInt(&ok);
    if (ok) {
        const QString bn = QStringLiteral("B%1").arg(num);
        for (const BandFile& b : bands) {
            if (b.name.toUpper() == bn)
                return &b;
        }
    }
    // MODIS / long subdataset names: substring match, both directions gated
    // at >= 4 chars — an ungated `w.contains(n)` let a short band name match
    // anywhere inside a long requested label.
    if (w.size() >= 4) {
        for (const BandFile& b : bands) {
            const QString n = b.name.toUpper();
            if (n.size() < 4)
                continue;
            if (n.endsWith(w) || n.contains(w) || w.contains(n))
                return &b;
        }
    }
    return nullptr;
}

bool isIdentityOrMissingGeo(const std::array<double, 6>& gt, const QString& projection)
{
    if (projection.trimmed().isEmpty())
        return true;
    // Default open without geotransform often yields 0,1,0,0,0,1 or 0,1,0,0,0,-1
    const bool nearUnit =
        std::abs(gt[0]) < 1e-9 && std::abs(gt[1] - 1.0) < 1e-9 && std::abs(gt[2]) < 1e-9
        && std::abs(gt[3]) < 1e-9 && std::abs(gt[4]) < 1e-9
        && (std::abs(gt[5] - 1.0) < 1e-9 || std::abs(gt[5] + 1.0) < 1e-9);
    return nearUnit;
}

bool copyRasterPixels(GDALDatasetH src, GDALDatasetH dst, QString* errorMessage)
{
    const int width = GDALGetRasterXSize(src);
    const int height = GDALGetRasterYSize(src);
    const int bandCount = GDALGetRasterCount(src);
    if (GDALGetRasterXSize(dst) != width || GDALGetRasterYSize(dst) != height
        || GDALGetRasterCount(dst) != bandCount) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Source/destination raster size or band count mismatch");
        return false;
    }
    // Windowed copy (#634): the whole-band float buffer (~10 GB at 50k x 50k)
    // became an uncaught bad_alloc on first-class scenes; row-block windows
    // keep memory O(width * kBlockRows) per band.
    constexpr int kBlockRows = 256;
    std::vector<float> buffer;
    for (int b = 1; b <= bandCount; ++b) {
        GDALRasterBandH sb = GDALGetRasterBand(src, b);
        GDALRasterBandH db = GDALGetRasterBand(dst, b);
        if (!sb || !db) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Missing band %1 during pixel copy").arg(b);
            return false;
        }
        for (int y = 0; y < height; y += kBlockRows) {
            const int rows = std::min(kBlockRows, height - y);
            buffer.assign(static_cast<size_t>(width) * rows, 0.0f);
            if (GDALRasterIO(sb, GF_Read, 0, y, width, rows, buffer.data(), width, rows,
                             GDT_Float32, 0, 0)
                != CE_None) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Failed to read band %1").arg(b);
                return false;
            }
            if (GDALRasterIO(db, GF_Write, 0, y, width, rows, buffer.data(), width, rows,
                             GDT_Float32, 0, 0)
                != CE_None) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Failed to write band %1").arg(b);
                return false;
            }
        }
        const char* desc = GDALGetDescription(sb);
        if (desc && desc[0])
            GDALSetDescription(db, desc);
        int hasNodata = 0;
        const double nd = GDALGetRasterNoDataValue(sb, &hasNodata);
        if (hasNodata)
            GDALSetRasterNoDataValue(db, nd);
    }
    return true;
}

bool warpToCrs(const QString& inputPath, const QString& outputPath, const QString& dstCrs,
               const QString& resampling, QString* errorMessage,
               const std::function<void(double, const QString&)>& progress)
{
    ensureGdalInit();
    GDALDatasetH src = GDALOpen(inputPath.toUtf8().constData(), GA_ReadOnly);
    if (!src) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Cannot open for warp: %1").arg(inputPath);
        return false;
    }

    std::vector<char*> argv;
    auto push = [&](const char* s) { argv.push_back(CPLStrdup(s)); };
    push("-t_srs");
    push(dstCrs.toUtf8().constData());
    push("-r");
    push(resampling.isEmpty() ? "bilinear" : resampling.toUtf8().constData());
    push("-of");
    push("GTiff");
    push("-co");
    push("COMPRESS=LZW");
    push("-co");
    push("TILED=YES");
    argv.push_back(nullptr);

    GDALWarpAppOptions* opts = GDALWarpAppOptionsNew(argv.data(), nullptr);
    for (char* a : argv) {
        if (a)
            CPLFree(a);
    }
    if (!opts) {
        GDALClose(src);
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to build GDALWarp options");
        return false;
    }

    struct ProgressBridge {
        std::function<void(double, const QString&)> fn;
    } bridge{progress};

    if (progress) {
        GDALWarpAppOptionsSetProgress(
            opts,
            [](double complete, const char* msg, void* p) -> int {
                auto* b = static_cast<ProgressBridge*>(p);
                if (b && b->fn) {
                    try {
                        b->fn(complete, msg ? QString::fromUtf8(msg) : QString());
                    } catch (...) {
                        return FALSE; // Cleanly tell GDAL to abort warp without leaking exception
                    }
                }
                return TRUE;
            },
            &bridge);
    }

    int usageError = FALSE;
    GDALDatasetH dst =
        GDALWarp(outputPath.toUtf8().constData(), nullptr, 1, &src, opts, &usageError);
    GDALWarpAppOptionsFree(opts);
    GDALClose(src);

    if (!dst || usageError) {
        if (errorMessage) {
            const char* last = CPLGetLastErrorMsg();
            *errorMessage =
                QStringLiteral("MODIS warp to %1 failed%2")
                    .arg(dstCrs)
                    .arg(last && last[0] ? QStringLiteral(": %1").arg(QString::fromUtf8(last))
                                         : QString());
        }
        if (dst)
            GDALClose(dst);
        return false;
    }
    GDALClose(dst);
    return true;
}

QVector<BandFile> selectBands(const ProductInfo& product, const QStringList& bandNames)
{
    QVector<BandFile> selected;
    if (bandNames.isEmpty()) {
        for (const BandFile& b : product.bands) {
            if (!bandIsQa(b.name))
                selected.append(b);
        }
        return selected;
    }
    QStringList missing;
    for (const QString& want : bandNames) {
        if (const BandFile* hit = findBand(product.bands, want))
            selected.append(*hit);
        else
            missing.append(want);
    }
    if (!missing.isEmpty()) {
        throw std::runtime_error(QStringLiteral("Requested bands not found: %1").arg(missing.join(QStringLiteral(", "))).toStdString());
    }
    return selected;
}

} // namespace

sicnu::data::BandRole landsatBandRole(const QString& bandName, const QString& spacecraft)
{
    QString core = bandName.toUpper();
    if (core.contains(QStringLiteral("QA")))
        return sicnu::data::BandRole::QA;
    // Discovery strips SR_/ST_ prefixes; tolerate them defensively here.
    if (core.startsWith(QStringLiteral("SR_")) || core.startsWith(QStringLiteral("ST_")))
        core = core.mid(3);
    const QString sc = spacecraft.toUpper();
    const bool oli = sc.contains(QStringLiteral("LANDSAT_8"))
                     || sc.contains(QStringLiteral("LANDSAT_9"));
    return oli ? landsatOliRole(core) : legacyLandsatRole(core);
}

sicnu::data::BandRole sentinel2BandRole(const QString& bandName)
{
    const QString u = bandName.toUpper();
    if (u == QStringLiteral("SCL"))
        return sicnu::data::BandRole::SceneClassification;
    if (u.contains(QStringLiteral("MSK")) || u.contains(QStringLiteral("AOT"))
        || u.contains(QStringLiteral("WVP")) || u.contains(QStringLiteral("CLDPRB"))
        || u.contains(QStringLiteral("TCI")))
        return sicnu::data::BandRole::QA;
    static const QMap<QString, BandRole> roles{
        {QStringLiteral("B1"), BandRole::Coastal},
        {QStringLiteral("B2"), BandRole::Blue},
        {QStringLiteral("B3"), BandRole::Green},
        {QStringLiteral("B4"), BandRole::Red},
        {QStringLiteral("B5"), BandRole::RedEdge},
        {QStringLiteral("B6"), BandRole::RedEdge},
        {QStringLiteral("B7"), BandRole::RedEdge},
        {QStringLiteral("B8"), BandRole::NIR},
        {QStringLiteral("B8A"), BandRole::NarrowNIR},
        {QStringLiteral("B9"), BandRole::Unknown}, // water vapour, no spectral role
        {QStringLiteral("B10"), BandRole::Cirrus},
        {QStringLiteral("B11"), BandRole::SWIR1},
        {QStringLiteral("B12"), BandRole::SWIR2},
    };
    return roles.value(u, BandRole::Unknown);
}

sicnu::data::BandRole modisBandRole(const QString& bandName)
{
    static const QRegularExpression bRe(
        QStringLiteral(R"(b0?([1-7])$)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = bRe.match(bandName.trimmed());
    if (!m.hasMatch())
        return sicnu::data::BandRole::Unknown;
    static const BandRole roles[] = {
        BandRole::Unknown, BandRole::Red, BandRole::NIR, BandRole::Blue,
        BandRole::Green, BandRole::SWIR1, BandRole::SWIR2, BandRole::SWIR2,
    };
    const int idx = m.captured(1).toInt();
    return (idx >= 1 && idx <= 7) ? roles[idx] : BandRole::Unknown;
}

QString productTypeName(ProductType type)
{
    switch (type) {
    case ProductType::Landsat:
        return QStringLiteral("Landsat");
    case ProductType::Sentinel2:
        return QStringLiteral("Sentinel-2");
    case ProductType::Modis:
        return QStringLiteral("MODIS");
    default:
        return QStringLiteral("Unknown");
    }
}

QStringList defaultModisReflectanceBands()
{
    // Common surface reflectance short names across MOD09/MYD09/MCD43 products
    return {QStringLiteral("sur_refl_b01"), QStringLiteral("sur_refl_b02"),
            QStringLiteral("sur_refl_b03"), QStringLiteral("sur_refl_b04"),
            QStringLiteral("sur_refl_b05"), QStringLiteral("sur_refl_b06"),
            QStringLiteral("sur_refl_b07")};
}

// NASA MODIS sinusoidal tile grid constants (metres)
constexpr double kModisTileSizeM = 1111950.5196666666;
constexpr double kModisXMin = -20015109.354;
constexpr double kModisYMax = 10007554.677;

QString modisSinusoidalWkt()
{
    // Sphere R=6371007.181 used by MODIS land products
    return QStringLiteral(
        "PROJCS[\"MODIS Sinusoidal\","
        "GEOGCS[\"Unknown datum based upon the custom spheroid\","
        "DATUM[\"Not specified (based on custom spheroid)\","
        "SPHEROID[\"Custom spheroid\",6371007.181,0]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]],"
        "PROJECTION[\"Sinusoidal\"],"
        "PARAMETER[\"longitude_of_center\",0],"
        "PARAMETER[\"false_easting\",0],"
        "PARAMETER[\"false_northing\",0],"
        "UNIT[\"metre\",1]]");
}

bool parseModisTileIndices(const QString& fileName, int* tileH, int* tileV)
{
    static const QRegularExpression re(
        QStringLiteral(R"(h(\d{2})v(\d{2}))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(fileName);
    if (!m.hasMatch())
        return false;
    const int h = m.captured(1).toInt();
    const int v = m.captured(2).toInt();
    if (h < 0 || h > 35 || v < 0 || v > 17)
        return false;
    if (tileH)
        *tileH = h;
    if (tileV)
        *tileV = v;
    return true;
}

bool modisTileGeoTransform(int tileH, int tileV, int width, int height,
                           std::array<double, 6>* gt, QString* errorMessage)
{
    if (!gt) {
        if (errorMessage)
            *errorMessage = QStringLiteral("geotransform output is null");
        return false;
    }
    if (tileH < 0 || tileH > 35 || tileV < 0 || tileV > 17) {
        if (errorMessage)
            *errorMessage = QStringLiteral("MODIS tile indices out of range (h0-35, v0-17)");
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Invalid raster dimensions for MODIS georef");
        return false;
    }
    const double ulx = kModisXMin + tileH * kModisTileSizeM;
    const double uly = kModisYMax - tileV * kModisTileSizeM;
    const double resX = kModisTileSizeM / static_cast<double>(width);
    const double resY = kModisTileSizeM / static_cast<double>(height);
    (*gt)[0] = ulx;
    (*gt)[1] = resX;
    (*gt)[2] = 0.0;
    (*gt)[3] = uly;
    (*gt)[4] = 0.0;
    (*gt)[5] = -resY;
    return true;
}

bool assignModisSinusoidalGeoref(const QString& inputPath,
                                 const QString& outputPath,
                                 int tileH,
                                 int tileV,
                                 QString* errorMessage)
{
    ensureGdalInit();
    const QString absIn = normalizePath(inputPath);
    GDALDatasetH src = GDALOpen(absIn.toUtf8().constData(), GA_ReadOnly);
    if (!src) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Cannot open input raster: %1").arg(inputPath);
        return false;
    }

    int h = tileH;
    int v = tileV;
    if (h < 0 || v < 0) {
        int ph = -1, pv = -1;
        if (parseModisTileIndices(QFileInfo(absIn).fileName(), &ph, &pv)) {
            if (h < 0)
                h = ph;
            if (v < 0)
                v = pv;
        }
    }
    if (h < 0 || v < 0) {
        GDALClose(src);
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "MODIS tile indices required (hXXvYY in filename or tileH/tileV params)");
        return false;
    }

    const int width = GDALGetRasterXSize(src);
    const int height = GDALGetRasterYSize(src);
    const int bandCount = GDALGetRasterCount(src);
    if (width <= 0 || height <= 0 || bandCount <= 0) {
        GDALClose(src);
        if (errorMessage)
            *errorMessage = QStringLiteral("Invalid raster dimensions in %1").arg(inputPath);
        return false;
    }

    std::array<double, 6> gt{};
    if (!modisTileGeoTransform(h, v, width, height, &gt, errorMessage)) {
        GDALClose(src);
        return false;
    }

    const QString wkt = modisSinusoidalWkt();
    QString err;
    GDALDatasetH dst = createOutputTiff(outputPath, width, height, bandCount,
                                        static_cast<int>(GDT_Float32), gt, wkt, &err);
    if (!dst) {
        GDALClose(src);
        if (errorMessage)
            *errorMessage = err.isEmpty() ? QStringLiteral("Failed to create georeferenced GeoTIFF")
                                          : err;
        return false;
    }

    if (!copyRasterPixels(src, dst, errorMessage)) {
        GDALClose(src);
        GDALClose(dst);
        return false;
    }

    GDALSetMetadataItem(dst, "SICNU_PRODUCT_TYPE", "MODIS", nullptr);
    GDALSetMetadataItem(dst, "SICNU_MODIS_TILE_H", QByteArray::number(h).constData(), nullptr);
    GDALSetMetadataItem(dst, "SICNU_MODIS_TILE_V", QByteArray::number(v).constData(), nullptr);
    GDALSetMetadataItem(dst, "SICNU_MODIS_CRS", "sinusoidal", nullptr);

    GDALClose(src);
    GDALClose(dst);
    return true;
}

bool georeferenceModis(const QString& inputPath,
                       const QString& outputPath,
                       const QString& dstCrs,
                       int tileH,
                       int tileV,
                       const QString& resampling,
                       QString* errorMessage,
                       const std::function<void(double, const QString&)>& progress)
{
    ensureGdalInit();
    const QString absIn = normalizePath(inputPath);
    const QString absOut = normalizePath(outputPath);

    // If no destination CRS, only assign sinusoidal GT/CRS.
    if (dstCrs.trimmed().isEmpty()) {
        if (progress)
            progress(0.1, QStringLiteral("Assigning MODIS sinusoidal georeference"));
        const bool ok = assignModisSinusoidalGeoref(absIn, absOut, tileH, tileV, errorMessage);
        if (ok && progress)
            progress(1.0, QStringLiteral("MODIS georeference complete (sinusoidal)"));
        return ok;
    }

    // Need sinusoidal source first when input lacks CRS or tile override requested.
    GDALDatasetH probe = GDALOpen(absIn.toUtf8().constData(), GA_ReadOnly);
    if (!probe) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Cannot open input raster: %1").arg(inputPath);
        return false;
    }
    std::array<double, 6> srcGt{};
    if (GDALGetGeoTransform(probe, srcGt.data()) != CE_None)
        srcGt = {0, 1, 0, 0, 0, -1};
    const char* srcProj = GDALGetProjectionRef(probe);
    const QString srcProjection = srcProj ? QString::fromUtf8(srcProj) : QString();
    GDALClose(probe);

    QString sinuPath = absIn;
    QString tempSinu;
    const bool needsAssign = isIdentityOrMissingGeo(srcGt, srcProjection) || tileH >= 0 || tileV >= 0;
    if (needsAssign) {
        if (progress)
            progress(0.05, QStringLiteral("Assigning MODIS sinusoidal tile georeference"));
        tempSinu = absOut + QStringLiteral(".sinu_tmp.tif");
        if (!assignModisSinusoidalGeoref(absIn, tempSinu, tileH, tileV, errorMessage)) {
            // assign may leave a partial .sinu_tmp.tif behind; don't leak it.
            QFile::remove(tempSinu);
            return false;
        }
        sinuPath = tempSinu;
    }

    if (progress)
        progress(0.2, QStringLiteral("Warping to %1").arg(dstCrs));

    const bool ok = warpToCrs(sinuPath, absOut, dstCrs,
                              resampling.isEmpty() ? QStringLiteral("bilinear") : resampling,
                              errorMessage, [&](double f, const QString& msg) {
                                  if (progress)
                                      progress(0.2 + 0.75 * f, msg);
                              });

    if (!tempSinu.isEmpty())
        QFile::remove(tempSinu);

    if (ok && progress)
        progress(1.0, QStringLiteral("MODIS georeference complete"));
    return ok;
}

QStringList defaultLandsatOpticalBands()
{
    return {QStringLiteral("B1"), QStringLiteral("B2"), QStringLiteral("B3"),
            QStringLiteral("B4"), QStringLiteral("B5"), QStringLiteral("B6"),
            QStringLiteral("B7")};
}

QStringList defaultSentinel2Bands10m()
{
    return {QStringLiteral("B2"), QStringLiteral("B3"), QStringLiteral("B4"),
            QStringLiteral("B8")};
}

QStringList defaultSentinel2Bands20m()
{
    return {QStringLiteral("B5"),  QStringLiteral("B6"),  QStringLiteral("B7"),
            QStringLiteral("B8A"), QStringLiteral("B11"), QStringLiteral("B12")};
}

bool discoverLandsat(const QString& path, ProductInfo* out, QString* errorMessage)
{
    if (!out)
        return false;
    *out = ProductInfo{};

    const QString abs = normalizePath(path);
    QFileInfo fi(abs);
    QString mtlPath;
    QDir rootDir;

    if (fi.isFile() && isLandsatMtlName(fi.fileName())) {
        mtlPath = abs;
        rootDir = fi.absoluteDir();
    } else if (fi.isDir()) {
        rootDir = QDir(abs);
        mtlPath = findMtlInDir(rootDir);
    } else {
        if (errorMessage)
            *errorMessage = QStringLiteral("Not a Landsat MTL file or scene directory: %1").arg(path);
        return false;
    }

    if (mtlPath.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No *_MTL.txt found under: %1").arg(rootDir.absolutePath());
        return false;
    }

    QString err;
    const QMap<QString, QString> kv = parseMtlKeyValues(mtlPath, &err);
    if (kv.isEmpty()) {
        if (errorMessage)
            *errorMessage = err.isEmpty() ? QStringLiteral("Empty MTL") : err;
        return false;
    }

    out->type = ProductType::Landsat;
    out->metadataPath = mtlPath;
    out->rootDir = rootDir.absolutePath();
    out->spacecraft = kv.value(QStringLiteral("SPACECRAFT_ID"),
                               kv.value(QStringLiteral("SENSOR_ID")));
    out->processingLevel = kv.value(QStringLiteral("PROCESSING_LEVEL"),
                                    kv.value(QStringLiteral("DATA_TYPE")));
    out->acquisitionDate = kv.value(QStringLiteral("DATE_ACQUIRED"),
                                    kv.value(QStringLiteral("ACQUISITION_DATE")));
    out->productId = kv.value(QStringLiteral("LANDSAT_PRODUCT_ID"),
                              kv.value(QStringLiteral("LANDSAT_SCENE_ID"),
                                       rootDir.dirName()));
    out->attributes = kv;

    // Collect band files from FILE_NAME_BAND_* keys
    QList<QPair<QString, QString>> ordered; // name, filename
    for (auto it = kv.constBegin(); it != kv.constEnd(); ++it) {
        if (!it.key().startsWith(QStringLiteral("FILE_NAME_BAND_"), Qt::CaseInsensitive))
            continue;
        const QString bname = landsatBandNameFromKey(it.key());
        if (bname.isEmpty())
            continue;
        ordered.append({bname, it.value()});
    }

    // Sort: numeric B1..B11 then others
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        auto rank = [](const QString& n) -> int {
            static const QRegularExpression re(QStringLiteral(R"(^B(\d+)$)"),
                                               QRegularExpression::CaseInsensitiveOption);
            const auto m = re.match(n);
            if (m.hasMatch())
                return m.captured(1).toInt();
            return 1000;
        };
        const int ra = rank(a.first);
        const int rb = rank(b.first);
        if (ra != rb)
            return ra < rb;
        return a.first < b.first;
    });

    // OLI vs legacy TM/ETM decide band roles/FWHM (B1: Coastal vs Blue, ...).
    const QString spacecraftU = out->spacecraft.toUpper();
    const bool oliLandsat = spacecraftU.contains(QStringLiteral("LANDSAT_8"))
                            || spacecraftU.contains(QStringLiteral("LANDSAT_9"));

    for (const auto& p : ordered) {
        const QString bandPath = rootDir.absoluteFilePath(p.second);
        if ( QgsDataSourceResolver::requiresLocalExistenceCheck( bandPath ) && !QFileInfo::exists( bandPath ) )
            continue;
        BandFile bf;
        bf.path = bandPath;
        bf.name = p.first;
        bf.wavelengthNm = landsatWavelength(p.first, oliLandsat);
        bf.fwhmNm = landsatFwhmNm(p.first, oliLandsat);
        bf.role = landsatBandRole(p.first, out->spacecraft);
        out->bands.append(bf);
    }

    if (out->bands.isEmpty()) {
        // Fallback: scan directory for *_B*.TIF
        const QFileInfoList tifs = rootDir.entryInfoList(
            {QStringLiteral("*_B*.TIF"), QStringLiteral("*_B*.tif"),
             QStringLiteral("*_SR_B*.TIF"), QStringLiteral("*_SR_B*.tif")},
            QDir::Files);
        static const QRegularExpression bandRe(
            QStringLiteral(R"(_((?:SR_)?B\d{1,2}|QA_PIXEL|QA_RADSAT)\.)"),
            QRegularExpression::CaseInsensitiveOption);
        for (const QFileInfo& tif : tifs) {
            const auto m = bandRe.match(tif.fileName());
            if (!m.hasMatch())
                continue;
            BandFile bf;
            bf.path = tif.absoluteFilePath();
            bf.name = m.captured(1).toUpper();
            if (bf.name.startsWith(QStringLiteral("SR_")))
                bf.name = bf.name.mid(3);
            bf.wavelengthNm = landsatWavelength(bf.name, oliLandsat);
            bf.fwhmNm = landsatFwhmNm(bf.name, oliLandsat);
            bf.role = landsatBandRole(bf.name, out->spacecraft);
            out->bands.append(bf);
        }
    }

    if (out->bands.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No Landsat band GeoTIFFs found next to MTL");
        return false;
    }
    return true;
}

bool discoverSentinel2(const QString& path, ProductInfo* out,
                       const QString& preferredResolution, QString* errorMessage)
{
    if (!out)
        return false;
    *out = ProductInfo{};

    const QString abs = normalizePath(path);
    QFileInfo fi(abs);
    QString mtdPath;
    QDir rootDir;

    if (fi.isFile() && isSentinelMtdName(fi.fileName())) {
        mtdPath = abs;
        // SAFE root is usually parent of MTD or parent of GRANULE sibling
        rootDir = fi.absoluteDir();
        if (rootDir.dirName().endsWith(QStringLiteral(".SAFE"), Qt::CaseInsensitive)
            || rootDir.exists(QStringLiteral("GRANULE"))) {
            // already SAFE root or close
        } else if (QDir(rootDir.absoluteFilePath(QStringLiteral(".."))).exists(QStringLiteral("GRANULE"))) {
            rootDir.cdUp();
        }
    } else if (fi.isDir()) {
        rootDir = QDir(abs);
        // If user pointed at GRANULE child, go up
        if (rootDir.dirName() == QStringLiteral("GRANULE") && rootDir.cdUp()) {
            // ok
        }
        mtdPath = findMtdInDir(rootDir);
    } else {
        if (errorMessage)
            *errorMessage = QStringLiteral("Not a Sentinel-2 SAFE directory or MTD xml: %1").arg(path);
        return false;
    }

    // Allow discovery without MTD by scanning IMG_DATA
    out->type = ProductType::Sentinel2;
    out->metadataPath = mtdPath;
    out->rootDir = rootDir.absolutePath();
    out->productId = rootDir.dirName();
    if (out->productId.endsWith(QStringLiteral(".SAFE"), Qt::CaseInsensitive))
        out->productId.chop(5);

    if (out->productId.startsWith(QStringLiteral("S2A"), Qt::CaseInsensitive))
        out->spacecraft = QStringLiteral("Sentinel-2A");
    else if (out->productId.startsWith(QStringLiteral("S2B"), Qt::CaseInsensitive))
        out->spacecraft = QStringLiteral("Sentinel-2B");
    else if (out->productId.startsWith(QStringLiteral("S2C"), Qt::CaseInsensitive))
        out->spacecraft = QStringLiteral("Sentinel-2C");
    else
        out->spacecraft = QStringLiteral("Sentinel-2");

    if (out->productId.contains(QStringLiteral("MSIL2A"), Qt::CaseInsensitive))
        out->processingLevel = QStringLiteral("L2A");
    else if (out->productId.contains(QStringLiteral("MSIL1C"), Qt::CaseInsensitive))
        out->processingLevel = QStringLiteral("L1C");
    else
        out->processingLevel = QStringLiteral("unknown");

    // Parse acquisition date from product id: S2A_MSIL2A_20170725T...
    static const QRegularExpression dateRe(QStringLiteral(R"(_(\d{8})T\d{6})"));
    const auto dm = dateRe.match(out->productId);
    if (dm.hasMatch()) {
        const QString d = dm.captured(1);
        out->acquisitionDate = QStringLiteral("%1-%2-%3")
                                   .arg(d.left(4), d.mid(4, 2), d.mid(6, 2));
    }

    QString res = preferredResolution.trimmed().toLower();
    if (res != QStringLiteral("10m") && res != QStringLiteral("20m") && res != QStringLiteral("60m"))
        res = QStringLiteral("10m");
    out->attributes.insert(QStringLiteral("resolution"), res);

    // Collect band files under GRANULE/**/IMG_DATA
    QStringList nameFilters;
    nameFilters << QStringLiteral("*.jp2") << QStringLiteral("*.JP2")
                << QStringLiteral("*.tif") << QStringLiteral("*.TIF")
                << QStringLiteral("*.tiff") << QStringLiteral("*.TIFF");

    // Band id pattern: _B02_10m.jp2 or _B8A_20m.jp2 or _B02.jp2 (L1C),
    // plus the L2A auxiliary layers SCL and MSK_CLDPRB.
    static const QRegularExpression bandRe(
        QStringLiteral(R"(_(B(?:0?[1-9]|1[0-2]|8A)|SCL|MSK_CLDPRB)(?:_(\d{2}m))?\.(?:jp2|tif|tiff)$)"),
        QRegularExpression::CaseInsensitiveOption);

    QDirIterator it(rootDir.absolutePath(), nameFilters, QDir::Files, QDirIterator::Subdirectories);
    QMap<QString, BandFile> byName; // keep one per band name matching resolution preference

    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString fileName = QFileInfo(filePath).fileName();
        const QString pathLower = filePath.toLower();
        // L2A: keep bands under the preferred R10m/R20m/R60m folder (or matching suffix).
        // Auxiliary layers (SCL, MSK_CLDPRB) are resolution-independent and are
        // always discovered regardless of the preferred optical resolution.
        if (out->processingLevel == QStringLiteral("L2A")) {
            const QString folderTag = QStringLiteral("/r") + res; // e.g. /r10m
            const bool inPreferredFolder = pathLower.contains(folderTag);
            const bool nameHasRes = fileName.contains(QStringLiteral("_") + res, Qt::CaseInsensitive);
            const bool inAnyResFolder = pathLower.contains(QStringLiteral("/r10m"))
                                        || pathLower.contains(QStringLiteral("/r20m"))
                                        || pathLower.contains(QStringLiteral("/r60m"));
            const QString uName = fileName.toUpper();
            const bool isAux = uName.contains(QStringLiteral("_SCL_"))
                               || uName.contains(QStringLiteral("_MSK_CLDPRB_"));
            if (inAnyResFolder && !inPreferredFolder && !nameHasRes && !isAux)
                continue;
        }

        const auto m = bandRe.match(fileName);
        if (!m.hasMatch())
            continue;

        QString bname = m.captured(1).toUpper();
        // Normalize B02 -> B2 for consistency with defaults, keep B8A
        if (bname.size() == 3 && bname[0] == QLatin1Char('B') && bname[1] == QLatin1Char('0'))
            bname = QStringLiteral("B") + bname.mid(2);

        const QString fileRes = m.captured(2).toLower();
        const bool isAux = bname == QStringLiteral("SCL")
                           || bname == QStringLiteral("MSK_CLDPRB");
        if (!fileRes.isEmpty() && fileRes != res && out->processingLevel == QStringLiteral("L2A")
            && !isAux)
            continue;

        BandFile bf;
        bf.path = filePath;
        bf.name = bname;
        bf.wavelengthNm = sentinelWavelength(bname);
        bf.fwhmNm = sentinelFwhmNm(bname);
        bf.role = sentinel2BandRole(bname);
        // Prefer first match; overwrite only if current empty
        if (!byName.contains(bname))
            byName.insert(bname, bf);
    }

    // Order B1..B12, B8A after B8, then the L2A auxiliary layers.
    QStringList order = {QStringLiteral("B1"),  QStringLiteral("B2"),  QStringLiteral("B3"),
                         QStringLiteral("B4"),  QStringLiteral("B5"),  QStringLiteral("B6"),
                         QStringLiteral("B7"),  QStringLiteral("B8"),  QStringLiteral("B8A"),
                         QStringLiteral("B9"),  QStringLiteral("B10"), QStringLiteral("B11"),
                         QStringLiteral("B12"), QStringLiteral("SCL"), QStringLiteral("MSK_CLDPRB")};
    for (const QString& n : order) {
        if (byName.contains(n))
            out->bands.append(byName.value(n));
    }
    // any extras
    for (auto itb = byName.constBegin(); itb != byName.constEnd(); ++itb) {
        bool already = false;
        for (const BandFile& b : out->bands) {
            if (b.name == itb.key()) {
                already = true;
                break;
            }
        }
        if (!already)
            out->bands.append(itb.value());
    }

    if (out->bands.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "No Sentinel-2 band rasters found under %1 (resolution=%2)")
                                .arg(rootDir.absolutePath(), res);
        return false;
    }
    return true;
}

bool discoverModis(const QString& path, ProductInfo* out, QString* errorMessage)
{
    if (!out)
        return false;
    *out = ProductInfo{};

    const QString abs = normalizePath(path);
    QFileInfo fi(abs);
    QStringList candidateFiles;

    auto isModisFile = [](const QString& name) -> bool {
        const QString u = name.toUpper();
        if (u.endsWith(QStringLiteral(".HDF")) || u.endsWith(QStringLiteral(".H5"))
            || u.endsWith(QStringLiteral(".HE5")) || u.endsWith(QStringLiteral(".HDF5")))
            return true;
        // GeoTIFF exports often keep MOD/MYD/MCD product prefix
        if ((u.startsWith(QStringLiteral("MOD")) || u.startsWith(QStringLiteral("MYD"))
             || u.startsWith(QStringLiteral("MCD")))
            && (u.endsWith(QStringLiteral(".TIF")) || u.endsWith(QStringLiteral(".TIFF"))))
            return true;
        return false;
    };

    if (fi.isFile()) {
        candidateFiles << abs;
    } else if (fi.isDir()) {
        const QFileInfoList entries = QDir(abs).entryInfoList(QDir::Files);
        for (const QFileInfo& e : entries) {
            if (isModisFile(e.fileName()))
                candidateFiles << e.absoluteFilePath();
        }
        if (candidateFiles.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("No MODIS HDF/GeoTIFF files in directory: %1").arg(abs);
            return false;
        }
    } else {
        if (errorMessage)
            *errorMessage = QStringLiteral("MODIS path not found: %1").arg(path);
        return false;
    }

    // Prefer HDF containers first
    std::sort(candidateFiles.begin(), candidateFiles.end(), [](const QString& a, const QString& b) {
        auto rank = [](const QString& p) {
            const QString u = p.toUpper();
            if (u.endsWith(QStringLiteral(".HDF")))
                return 0;
            if (u.endsWith(QStringLiteral(".H5")) || u.endsWith(QStringLiteral(".HE5")))
                return 1;
            return 2;
        };
        return rank(a) < rank(b);
    });

    ensureGdalInit();

    QString openPath = candidateFiles.first();
    out->type = ProductType::Modis;
    out->metadataPath = openPath;
    out->rootDir = QFileInfo(openPath).absolutePath();
    out->productId = QFileInfo(openPath).completeBaseName();

    // Spacecraft / product from filename prefix
    const QString base = QFileInfo(openPath).fileName().toUpper();
    if (base.startsWith(QStringLiteral("MOD")))
        out->spacecraft = QStringLiteral("Terra");
    else if (base.startsWith(QStringLiteral("MYD")))
        out->spacecraft = QStringLiteral("Aqua");
    else if (base.startsWith(QStringLiteral("MCD")))
        out->spacecraft = QStringLiteral("Combined");
    else
        out->spacecraft = QStringLiteral("MODIS");

    // Collection / product code e.g. MOD09GQ
    static const QRegularExpression prodRe(
        QStringLiteral(R"(^((?:MOD|MYD|MCD)\w+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto pm = prodRe.match(base);
    if (pm.hasMatch())
        out->processingLevel = pm.captured(1);

    // AYYYYDDD date code
    static const QRegularExpression dateRe(QStringLiteral(R"(\.A(\d{4})(\d{3})\.)"));
    const auto dm = dateRe.match(base);
    if (dm.hasMatch()) {
        // Keep year + DOY as compact string; full conversion optional
        out->acquisitionDate = QStringLiteral("%1-DOY%2").arg(dm.captured(1), dm.captured(2));
    }

    int th = -1, tv = -1;
    if (parseModisTileIndices(base, &th, &tv)) {
        out->modisTileH = th;
        out->modisTileV = tv;
        out->attributes.insert(QStringLiteral("tileH"), QString::number(th));
        out->attributes.insert(QStringLiteral("tileV"), QString::number(tv));
    }

    GDALDatasetH ds = GDALOpen(openPath.toUtf8().constData(), GA_ReadOnly);
    if (!ds) {
        // HDF4 missing is common; still allow georef-only workflows if path is a bare raster
        // by treating as single-band product with tile indices only.
        const char* last = CPLGetLastErrorMsg();
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                                "Cannot open MODIS file (need HDF4/HDF5 GDAL driver for NASA HDF): %1%2")
                                .arg(openPath)
                                .arg(last && last[0] ? QStringLiteral(" — %1").arg(QString::fromUtf8(last))
                                                     : QString());
        }
        return false;
    }

    char** subdatasets = const_cast<char**>(GDALGetMetadata(ds, "SUBDATASETS"));
    if (subdatasets && CSLCount(subdatasets) > 0) {
        // Collect NAME->DESC pairs first: the DESC entry is needed to recover
        // a readable name when the NAME's trailing component is a bare index.
        QMap<QString, QString> descByName;
        for (int i = 0; subdatasets[i] != nullptr; ++i) {
            const QString dEntry = QString::fromUtf8(subdatasets[i]);
            if (!dEntry.startsWith(QStringLiteral("SUBDATASET_")) || !dEntry.contains(QStringLiteral("_DESC=")))
                continue;
            const int dEq = dEntry.indexOf(QLatin1Char('='));
            if (dEq < 0)
                continue;
            const QString dKey = dEntry.left(dEq); // SUBDATASET_n_DESC
            descByName.insert(dKey.left(dKey.size() - 5) + QStringLiteral("_NAME"),
                              dEntry.mid(dEq + 1));
        }
        // Pairs: SUBDATASET_n_NAME / SUBDATASET_n_DESC
        for (int i = 0; subdatasets[i] != nullptr; ++i) {
            const QString entry = QString::fromUtf8(subdatasets[i]);
            if (!entry.startsWith(QStringLiteral("SUBDATASET_")) || !entry.contains(QStringLiteral("_NAME=")))
                continue;
            const int eq = entry.indexOf(QLatin1Char('='));
            if (eq < 0)
                continue;
            const QString subPath = entry.mid(eq + 1);
            // Derive short name from last component after ':'
            QString shortName = subPath;
            const int colon = shortName.lastIndexOf(QLatin1Char(':'));
            if (colon >= 0)
                shortName = shortName.mid(colon + 1);
            shortName = shortName.trimmed();
            // Plain HDF4_SDS names end in the SDS index ("...:0", "...:1"),
            // collapsing every band to a bare digit. Fall back to the paired
            // DESC text, which carries the real SDS name for these drivers.
            static const QRegularExpression digitsOnly(QStringLiteral(R"(^\d+$)"));
            if (digitsOnly.match(shortName).hasMatch()) {
                const QString desc = descByName.value(entry.left(eq));
                // DESC shape for these drivers: "[w x h] SDS Name (type)"
                const int closeBracket = desc.indexOf(QLatin1Char(']'));
                const int openParen = desc.lastIndexOf(QLatin1Char('('));
                const QString descName = (closeBracket >= 0 && openParen > closeBracket)
                                             ? desc.mid(closeBracket + 1, openParen - closeBracket - 1).trimmed()
                                             : desc.trimmed();
                if (!descName.isEmpty())
                    shortName = descName;
            }

            BandFile bf;
            bf.path = subPath;
            bf.name = shortName;
            bf.role = modisBandRole(shortName);
            // Rough MODIS land wavelengths for sur_refl_b0N
            static const QRegularExpression bRe(
                QStringLiteral(R"(b0?([1-7])$)"), QRegularExpression::CaseInsensitiveOption);
            const auto bm = bRe.match(shortName);
            if (bm.hasMatch()) {
                static const int wl[] = {0, 645, 858, 469, 555, 1240, 1640, 2130};
                const int idx = bm.captured(1).toInt();
                if (idx >= 1 && idx <= 7)
                    bf.wavelengthNm = wl[idx];
            }
            out->bands.append(bf);
        }
    } else {
        // Flat multi-band or single-band dataset
        const int bc = GDALGetRasterCount(ds);
        for (int b = 1; b <= bc; ++b) {
            BandFile bf;
            bf.path = openPath;
            bf.sourceBand = b;
            GDALRasterBandH band = GDALGetRasterBand(ds, b);
            const char* desc = band ? GDALGetDescription(band) : nullptr;
            bf.name = (desc && desc[0]) ? QString::fromUtf8(desc)
                                        : QStringLiteral("Band%1").arg(b);
            bf.role = modisBandRole(bf.name);
            out->bands.append(bf);
        }
    }

    GDALClose(ds);

    if (out->bands.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No MODIS bands/subdatasets found in %1").arg(openPath);
        return false;
    }
    return true;
}

bool discoverProduct(const QString& path, ProductInfo* out,
                     const QString& sentinelResolution, QString* errorMessage)
{
    const QString abs = normalizePath(path);
    QFileInfo fi(abs);
    if (fi.isFile() && isLandsatMtlName(fi.fileName()))
        return discoverLandsat(abs, out, errorMessage);
    if (fi.isFile() && isSentinelMtdName(fi.fileName()))
        return discoverSentinel2(abs, out, sentinelResolution, errorMessage);

    const QString nameU = fi.fileName().toUpper();
    if (fi.isFile()
        && (nameU.endsWith(QStringLiteral(".HDF")) || nameU.endsWith(QStringLiteral(".H5"))
            || nameU.endsWith(QStringLiteral(".HE5")) || nameU.endsWith(QStringLiteral(".HDF5"))
            || ((nameU.startsWith(QStringLiteral("MOD")) || nameU.startsWith(QStringLiteral("MYD"))
                 || nameU.startsWith(QStringLiteral("MCD")))
                && (nameU.endsWith(QStringLiteral(".TIF")) || nameU.endsWith(QStringLiteral(".TIFF"))))))
        return discoverModis(abs, out, errorMessage);

    if (fi.isDir()) {
        QDir d(abs);
        if (!findMtlInDir(d).isEmpty()
            || !d.entryList({QStringLiteral("*_B*.TIF"), QStringLiteral("*_MTL.txt")}, QDir::Files).isEmpty())
            return discoverLandsat(abs, out, errorMessage);
        if (d.dirName().endsWith(QStringLiteral(".SAFE"), Qt::CaseInsensitive)
            || d.exists(QStringLiteral("GRANULE"))
            || !findMtdInDir(d).isEmpty())
            return discoverSentinel2(abs, out, sentinelResolution, errorMessage);
        if (!d.entryList({QStringLiteral("*.hdf"), QStringLiteral("*.HDF"),
                          QStringLiteral("*.h5"), QStringLiteral("MOD*"), QStringLiteral("MYD*"),
                          QStringLiteral("MCD*")},
                         QDir::Files)
                 .isEmpty())
            return discoverModis(abs, out, errorMessage);
    }
    // Try Landsat then Sentinel then MODIS
    QString e1, e2, e3;
    if (discoverLandsat(abs, out, &e1))
        return true;
    if (discoverSentinel2(abs, out, sentinelResolution, &e2))
        return true;
    if (discoverModis(abs, out, &e3))
        return true;
    if (errorMessage)
        *errorMessage = QStringLiteral("Unrecognized satellite product (%1; %2; %3)")
                            .arg(e1, e2, e3);
    return false;
}

bool stackToGeoTiff(const ProductInfo& product,
                    const QStringList& bandNames,
                    const QString& outputPath,
                    QString* errorMessage,
                    const std::function<void(double, const QString&)>& progress)
{
    if (product.bands.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Product has no bands to stack");
        return false;
    }

    QVector<BandFile> selected;
    try {
        selected = selectBands(product, bandNames);
    } catch (const std::exception &e) {
        if (errorMessage)
            *errorMessage = QString::fromStdString(e.what());
        return false;
    }
    if (selected.isEmpty()) {
        if (errorMessage)
            *errorMessage = bandNames.isEmpty()
                                ? QStringLiteral("No stackable optical bands in product")
                                : QStringLiteral("Requested bands not found in product");
        return false;
    }

    ensureGdalInit();

    // Open first band for dimensions / geo
    GDALDatasetH first = GDALOpen(selected.first().path.toUtf8().constData(), GA_ReadOnly);
    if (!first) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to open band: %1").arg(selected.first().path);
        return false;
    }

    const int width = GDALGetRasterXSize(first);
    const int height = GDALGetRasterYSize(first);
    std::array<double, 6> gt{};
    if (GDALGetGeoTransform(first, gt.data()) != CE_None) {
        gt = {0, 1, 0, 0, 0, -1};
    }
    const char* proj = GDALGetProjectionRef(first);
    QString projection = proj ? QString::fromUtf8(proj) : QString();
    GDALClose(first);

    // MODIS HDF/exported tiles often lack CRS; assign sinusoidal tile GT when known.
    if (product.type == ProductType::Modis
        && isIdentityOrMissingGeo(gt, projection)
        && product.modisTileH >= 0 && product.modisTileV >= 0) {
        std::array<double, 6> modisGt{};
        QString geoErr;
        if (modisTileGeoTransform(product.modisTileH, product.modisTileV, width, height,
                                  &modisGt, &geoErr)) {
            gt = modisGt;
            projection = modisSinusoidalWkt();
        }
    }

    // Always write Float32 so mixed source types (UInt16 reflectance / DN) stack cleanly.
    QString err;
    GDALDatasetH outDs = createOutputTiff(outputPath, width, height,
                                          selected.size(), static_cast<int>(GDT_Float32),
                                          gt, projection, &err);
    if (!outDs) {
        if (errorMessage)
            *errorMessage = err.isEmpty() ? QStringLiteral("Failed to create output GeoTIFF") : err;
        return false;
    }

    // Windowed stacking (#634): the whole-band float buffer (~10 GB at
    // 50k x 50k) became an uncaught bad_alloc; row-block windows keep memory
    // O(width * kBlockRows).
    constexpr int kBlockRows = 256;
    std::vector<float> buffer;

    for (int i = 0; i < selected.size(); ++i) {
        if (progress)
            progress(static_cast<double>(i) / selected.size(),
                     QStringLiteral("Stacking %1").arg(selected[i].name));

        GDALDatasetH src = GDALOpen(selected[i].path.toUtf8().constData(), GA_ReadOnly);
        if (!src) {
            GDALClose(outDs);
            if (errorMessage)
                *errorMessage = QStringLiteral("Failed to open band: %1").arg(selected[i].path);
            return false;
        }

        if (GDALGetRasterXSize(src) != width || GDALGetRasterYSize(src) != height) {
            GDALClose(src);
            GDALClose(outDs);
            if (errorMessage)
                *errorMessage = QStringLiteral(
                                    "Band size mismatch for %1 (expected %2x%3)")
                                    .arg(selected[i].name)
                                    .arg(width)
                                    .arg(height);
            return false;
        }

        const int srcBandIndex = selected[i].sourceBand > 0 ? selected[i].sourceBand : 1;
        GDALRasterBandH srcBand = GDALGetRasterBand(src, srcBandIndex);
        if (!srcBand) {
            GDALClose(src);
            GDALClose(outDs);
            if (errorMessage)
                *errorMessage = QStringLiteral("Missing band %1 in %2")
                                    .arg(srcBandIndex)
                                    .arg(selected[i].path);
            return false;
        }
        int hasNoData = 0;
        double ndVal = GDALGetRasterNoDataValue(srcBand, &hasNoData);

        GDALRasterBandH dstBand = GDALGetRasterBand(outDs, i + 1);
        for (int y = 0; y < height; y += kBlockRows) {
            const int rows = std::min(kBlockRows, height - y);
            buffer.assign(static_cast<size_t>(width) * rows, 0.0f);
            CPLErr cerr = GDALRasterIO(srcBand, GF_Read, 0, y, width, rows,
                                       buffer.data(), width, rows, GDT_Float32, 0, 0);
            if (cerr != CE_None) {
                GDALClose(src);
                GDALClose(outDs);
                if (errorMessage)
                    *errorMessage = QStringLiteral("Failed to read band %1").arg(selected[i].name);
                return false;
            }
            cerr = GDALRasterIO(dstBand, GF_Write, 0, y, width, rows,
                                buffer.data(), width, rows, GDT_Float32, 0, 0);
            if (cerr != CE_None) {
                GDALClose(src);
                GDALClose(outDs);
                if (errorMessage)
                    *errorMessage = QStringLiteral("Failed to write band %1").arg(selected[i].name);
                return false;
            }
        }
        GDALClose(src);

        if (hasNoData) {
            GDALSetRasterNoDataValue(dstBand, ndVal);
        }

        GDALSetDescription(dstBand, selected[i].name.toUtf8().constData());
        if (selected[i].wavelengthNm > 0) {
            char meta[64];
            std::snprintf(meta, sizeof(meta), "%d", selected[i].wavelengthNm);
            GDALSetMetadataItem(dstBand, "WAVELENGTH", meta, nullptr);
            GDALSetMetadataItem(dstBand, "WAVELENGTH_UNITS", "nm", nullptr);
        }
        if (selected[i].fwhmNm > 0) {
            char fwhmMeta[64];
            std::snprintf(fwhmMeta, sizeof(fwhmMeta), "%d", selected[i].fwhmNm);
            GDALSetMetadataItem(dstBand, "FWHM", fwhmMeta, nullptr);
        }
        if (selected[i].role != BandRole::Unknown) {
            const QByteArray roleId =
                sicnu::data::bandRoleToString(selected[i].role).toUtf8();
            GDALSetMetadataItem(dstBand, "SICNU_BAND_ROLE", roleId.constData(), nullptr);
        }
    }

    // Product-level metadata
    GDALSetMetadataItem(outDs, "SICNU_PRODUCT_TYPE",
                        productTypeName(product.type).toUtf8().constData(), nullptr);
    if (!product.productId.isEmpty())
        GDALSetMetadataItem(outDs, "SICNU_PRODUCT_ID",
                            product.productId.toUtf8().constData(), nullptr);
    if (!product.spacecraft.isEmpty())
        GDALSetMetadataItem(outDs, "SICNU_SPACECRAFT",
                            product.spacecraft.toUtf8().constData(), nullptr);
    if (!product.processingLevel.isEmpty())
        GDALSetMetadataItem(outDs, "SICNU_PROCESSING_LEVEL",
                            product.processingLevel.toUtf8().constData(), nullptr);
    if (!product.acquisitionDate.isEmpty())
        GDALSetMetadataItem(outDs, "SICNU_ACQUISITION_DATE",
                            product.acquisitionDate.toUtf8().constData(), nullptr);
    if (product.type == ProductType::Modis && product.modisTileH >= 0) {
        GDALSetMetadataItem(outDs, "SICNU_MODIS_TILE_H",
                            QByteArray::number(product.modisTileH).constData(), nullptr);
        GDALSetMetadataItem(outDs, "SICNU_MODIS_TILE_V",
                            QByteArray::number(product.modisTileV).constData(), nullptr);
    }

    // Radiometric state of the stacked product (P0): change detection and
    // radiometric pipelines refuse to compare rasters in incompatible physical
    // states. Recording the state at import time closes the gap where imported
    // products silently bypassed that protection. Only written when confidently
    // known from the product type/level; unknown products stay unlabelled.
    const char *importedState = nullptr;
    if ( product.type == ProductType::Landsat )
    {
        // stackToGeoTiff copies band pixels verbatim (no gain/bias applied):
        // Collection 1/2 Level-1 (L1TP/L1GT/L1GS) products deliver DN; only
        // Level-2 (surface reflectance) products are pre-calibrated.
        importedState = product.processingLevel.startsWith( QLatin1String( "L2" ), Qt::CaseInsensitive )
                            ? kRadiometricStateSurfaceReflectance
                            : kRadiometricStateDigitalNumber;
    }
    else if ( product.type == ProductType::Sentinel2 )
    {
        importedState = ( product.processingLevel.compare( QLatin1String( "L2A" ), Qt::CaseInsensitive ) == 0 )
                            ? kRadiometricStateSurfaceReflectance
                            : kRadiometricStateDigitalNumber;
    }
    else if ( product.type == ProductType::Modis )
    {
        const QString pid = product.productId;
        if ( pid.contains( QLatin1String( "MOD11" ), Qt::CaseInsensitive )
             || pid.contains( QLatin1String( "MYD11" ), Qt::CaseInsensitive ) )
        {
            importedState = kRadiometricStateBrightnessTemperature;
        }
        else if ( pid.contains( QLatin1String( "MOD09" ), Qt::CaseInsensitive )
                  || pid.contains( QLatin1String( "MYD09" ), Qt::CaseInsensitive ) )
        {
            importedState = kRadiometricStateDigitalNumber;
        }
        else if ( pid.contains( QLatin1String( "MOD13" ), Qt::CaseInsensitive )
                  || pid.contains( QLatin1String( "MYD13" ), Qt::CaseInsensitive ) )
        {
            // MOD13/MYD13 are vegetation-index products (NDVI/EVI values),
            // not reflectance — mark them as non-reflectance data so they are
            // never compared against reflectance states.
            importedState = kRadiometricStateDigitalNumber;
        }
        else if ( pid.contains( QLatin1String( "MOD02" ), Qt::CaseInsensitive )
                  || pid.contains( QLatin1String( "MYD02" ), Qt::CaseInsensitive ) )
        {
            // MOD02/MYD02 calibrated radiance products.
            importedState = kRadiometricStateRadiance;
        }
        else
        {
            // Non-optical / uncertain MODIS products: leave unlabelled rather
            // than assert a physical state we cannot verify.
            importedState = nullptr;
        }
    }
    if ( importedState )
        GDALSetMetadataItem( outDs, kRadiometricStateKey, importedState, nullptr );

    GDALClose(outDs);
    if (progress)
        progress(1.0, QStringLiteral("Stack complete"));
    return true;
}

bool setRadiometricState( const QString &path, const char *state,
                          QString *errorMessage )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    if ( !ds )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open '%1' to record the radiometric state" )
                              .arg( path );
        return false;
    }
    const int result = GDALSetMetadataItem( ds, kRadiometricStateKey, state, nullptr );
    GDALClose( ds );
    if ( result != CE_None )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Failed to write the radiometric state to '%1'" )
                              .arg( path );
        return false;
    }
    return true;
}

QString readRadiometricState( const QString &path )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
        return QString();
    const char *value = GDALGetMetadataItem( ds, kRadiometricStateKey, nullptr );
    const QString state = value ? QString::fromUtf8( value ) : QString();
    GDALClose( ds );
    return state;
}

} // namespace SatelliteProducts
