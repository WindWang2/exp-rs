// src/processing/algorithms/radiometric_calibration.cpp — DN to physical units
#include "radiometric_calibration.h"
#include "math_utils.h"
#include "satellite_products.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "core/sicnu_logging.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace RadiometricCalibration
{
namespace {

// ---------------------------------------------------------------------------
// Number helpers
// ---------------------------------------------------------------------------

double toDouble(const QString &s, bool *ok = nullptr)
{
    return s.trimmed().toDouble(ok);
}

/// Extract the trailing integer from a Landsat band name like "B4", "ST_B10".
/// Matches the LAST digit group so names like "LC08_B4" resolve to 4, not 08.
int bandNumberFromName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("(\\d+)[^\\d]*$"));
    const auto m = re.match(name);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toInt();
}

// ---------------------------------------------------------------------------
// Landsat MTL parsing
// ---------------------------------------------------------------------------

bool loadLandsatMtl(const QString &mtlPath, const QMap<int, QString> &bandNames,
                    CalibrationMetadata *out, QString *errorMessage)
{
    QString err;
    const QMap<QString, QString> kv = SatelliteProducts::parseMtlKeyValues(mtlPath, &err);
    if (kv.isEmpty()) {
        if (errorMessage)
            *errorMessage = err.isEmpty() ? QStringLiteral("Empty MTL: %1").arg(mtlPath) : err;
        return false;
    }

    out->sensor = SensorType::Landsat;
    out->spacecraft = kv.value(QStringLiteral("SPACECRAFT_ID"));
    out->processingLevel = kv.value(QStringLiteral("PROCESSING_LEVEL"));
    out->acquisitionDate = kv.value(QStringLiteral("DATE_ACQUIRED"));
    bool sunOk = false;
    const double sunEl = toDouble(kv.value(QStringLiteral("SUN_ELEVATION")), &sunOk);
    if (sunOk && sunEl > 0.0 && sunEl < 90.0)
        out->sunElevationDeg = sunEl;

    // Map raster band index (1-based) -> Landsat band number.
    // Prefer the band-name mapping; fall back to identity (band i == MTL band i).
    QMap<int, int> rasterToLandsatBand;
    for (auto it = bandNames.constBegin(); it != bandNames.constEnd(); ++it) {
        const int n = bandNumberFromName(it.value());
        if (n > 0)
            rasterToLandsatBand.insert(it.key(), n);
    }

    const auto landsatBandFor = [&](int rasterBand) {
        auto it = rasterToLandsatBand.constFind(rasterBand);
        return it != rasterToLandsatBand.constEnd() ? it.value() : rasterBand;
    };

    bool any = false;
    for (auto it = bandNames.constBegin(); it != bandNames.constEnd(); ++it) {
        const int rasterBand = it.key();
        const int lb = landsatBandFor(rasterBand);
        BandCoefficients c;  // defaults: gain=1, bias=0, reflMult=1, reflAdd=0, scale=1
        bool gOk = false, bOk = false, rmOk = false, raOk = false, k1Ok = false, k2Ok = false;
        double v;
        v = toDouble(kv.value(QStringLiteral("RADIANCE_MULT_BAND_%1").arg(lb)), &gOk);
        if (gOk) c.radianceGain = v;
        v = toDouble(kv.value(QStringLiteral("RADIANCE_ADD_BAND_%1").arg(lb)), &bOk);
        if (bOk) c.radianceBias = v;
        v = toDouble(kv.value(QStringLiteral("REFLECTANCE_MULT_BAND_%1").arg(lb)), &rmOk);
        if (rmOk) c.reflMult = v;
        v = toDouble(kv.value(QStringLiteral("REFLECTANCE_ADD_BAND_%1").arg(lb)), &raOk);
        if (raOk) c.reflAdd = v;
        v = toDouble(kv.value(QStringLiteral("K1_CONSTANT_BAND_%1").arg(lb)), &k1Ok);
        if (k1Ok) c.k1 = v;
        v = toDouble(kv.value(QStringLiteral("K2_CONSTANT_BAND_%1").arg(lb)), &k2Ok);
        if (k2Ok) c.k2 = v;
        if (!gOk && !bOk) {
            // Collection 1 stores rescaling as RADIANCE_MULT / RADIANCE_ADD (no band suffix).
            v = toDouble(kv.value(QStringLiteral("RADIANCE_MULT")), &gOk);
            if (gOk) c.radianceGain = v;
            v = toDouble(kv.value(QStringLiteral("RADIANCE_ADD")), &bOk);
            if (bOk) c.radianceBias = v;
        }
        if (gOk || bOk || rmOk || raOk || k1Ok || k2Ok) {
            out->bands.insert(rasterBand, c);
            any = true;
        }
    }
    if (!any && errorMessage)
        *errorMessage = QStringLiteral("No Landsat calibration coefficients found in MTL");
    return any;
}

// ---------------------------------------------------------------------------
// Sentinel-2 MTD XML parsing
// ---------------------------------------------------------------------------

/// Read a single text value from //Element_Tag/Tag within @p parent.
QString mtdValue(const QDomElement &parent, const QString &tag)
{
    if (parent.isNull())
        return {};
    const QDomNodeList nodes = parent.elementsByTagName(tag);
    if (nodes.isEmpty())
        return {};
    return nodes.item(0).toElement().text().trimmed();
}

/// Build a map of BAND_ID -> quantification/offset values from a list element.
/// @param root       the element to search within
/// @param listTag    the outer list container tag (e.g. BOA_ADD_OFFSET / RADIO_ADD_OFFSET)
/// @param toValuesTag  the intermediate container tag (e.g. BOA_LIST_TO_VALUES / RADIO_LIST_TO_VALUES)
/// @param valueTag     the per-band value tag (e.g. BOA_LIST_VALUE / RADIO_LIST_VALUE)
QMap<QString, double> parseQuantificationList(const QDomElement &root,
                                              const QString &listTag,
                                              const QString &toValuesTag,
                                              const QString &valueTag)
{
    QMap<QString, double> result;
    const QDomNodeList lists = root.elementsByTagName(listTag);
    for (int i = 0; i < lists.size(); ++i) {
        const QDomElement listEl = lists.item(i).toElement();
        const QDomNodeList values = listEl.elementsByTagName(toValuesTag);
        for (int j = 0; j < values.size(); ++j) {
            const QDomElement lv = values.item(j).toElement();
            const QDomNodeList items = lv.elementsByTagName(valueTag);
            for (int k = 0; k < items.size(); ++k) {
                const QDomElement it = items.item(k).toElement();
                const QString id = it.attribute(QStringLiteral("band_id"));
                bool ok = false;
                const double v = it.text().trimmed().toDouble(&ok);
                if (ok && !id.isEmpty())
                    result.insert(id, v);
            }
        }
    }
    return result;
}

bool loadSentinel2Mtd(const QString &mtdPath, const QMap<int, QString> &bandNames,
                      CalibrationMetadata *out, QString *errorMessage)
{
    QFile f(mtdPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Cannot open MTD: %1").arg(mtdPath);
        return false;
    }
    QDomDocument doc;
    const QDomDocument::ParseResult parseResult = doc.setContent(&f);
    if (!parseResult) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to parse MTD XML: %1").arg(parseResult.errorMessage);
        return false;
    }

    out->sensor = SensorType::Sentinel2;
    const QDomElement root = doc.documentElement();

    // Sun zenith angle from Mean_Sun_Zenith_Angle (L2A) or Zenith_Angle (L1C).
    const QDomElement sunEl = root.firstChildElement(QStringLiteral("Geometric_Info"))
                                  .firstChildElement(QStringLiteral("Sun_Angles"));
    const QString zenithText = !mtdValue(sunEl, QStringLiteral("ZENITH_ANGLE")).isEmpty()
                                   ? mtdValue(sunEl, QStringLiteral("ZENITH_ANGLE"))
                                   : mtdValue(sunEl, QStringLiteral("Mean_Sun_Zenith_Angle"));
    bool zenOk = false;
    const double zenith = toDouble(zenithText, &zenOk);
    if (zenOk && zenith >= 0.0 && zenith <= 90.0)
        out->sunElevationDeg = 90.0 - zenith;

    // Processing level: detect from the XML root tag name.
    // L2A products use <n1:Level-2A_User_Product>, L1C use <n1:Level-1C_User_Product>.
    const QString productTag = root.tagName();
    const bool isL2A = productTag.contains(QStringLiteral("Level-2A"), Qt::CaseInsensitive)
                       || productTag.contains(QStringLiteral("MSIL2A"), Qt::CaseInsensitive);
    const bool isL1C = productTag.contains(QStringLiteral("Level-1C"), Qt::CaseInsensitive)
                       || productTag.contains(QStringLiteral("MSIL1C"), Qt::CaseInsensitive);
    if (isL2A)
        out->processingLevel = QStringLiteral("L2A");
    else if (isL1C)
        out->processingLevel = QStringLiteral("L1C");

    const QDomElement rad = root.firstChildElement(QStringLiteral("General_Info"))
                                .firstChildElement(QStringLiteral("Product_Image_Characteristics"))
                                .firstChildElement(QStringLiteral("Radiometric_Info"));

    // L2A: BOA_ADD_OFFSET list + BOA_QUANTIFICATION_VALUE scalar.
    // L1C: RADIO_ADD_OFFSET list + RADIO_QUANTIFICATION_VALUE scalar.
    // The inner list/value tags also differ by prefix (BOA_LIST_* vs RADIO_LIST_*).
    const bool l2a = out->processingLevel == QStringLiteral("L2A");
    const QString prefix = l2a ? QStringLiteral("BOA") : QStringLiteral("RADIO");
    const QString offsetListTag = prefix + QStringLiteral("_ADD_OFFSET");
    const QString quantScalarTag = prefix + QStringLiteral("_QUANTIFICATION_VALUE");
    const QString toValuesTag = prefix + QStringLiteral("_LIST_TO_VALUES");
    const QString valueTag = prefix + QStringLiteral("_LIST_VALUE");

    const QMap<QString, double> offsets = parseQuantificationList(rad, offsetListTag,
                                                                  toValuesTag, valueTag);
    bool qOk = false;
    const double quant = toDouble(mtdValue(rad, quantScalarTag), &qOk);

    // Map S2 band names (B2, B8A, ...) to a 0-based band_id used in the XML.
    // The XML band_id ordering follows the spectral band sequence (B1=0, B2=1, ...).
    static const QStringList s2Order = {
        QStringLiteral("B1"), QStringLiteral("B2"), QStringLiteral("B3"), QStringLiteral("B4"),
        QStringLiteral("B5"), QStringLiteral("B6"), QStringLiteral("B7"), QStringLiteral("B8"),
        QStringLiteral("B8A"), QStringLiteral("B9"), QStringLiteral("B10"), QStringLiteral("B11"),
        QStringLiteral("B12")
    };
    const auto bandIdFor = [&](const QString &name) -> int {
        return s2Order.indexOf(name);
    };

    bool any = false;
    for (auto it = bandNames.constBegin(); it != bandNames.constEnd(); ++it) {
        BandCoefficients c;
        const int id = bandIdFor(it.value().toUpper());
        if (id >= 0 && offsets.contains(QString::number(id)))
            c.offset = offsets.value(QString::number(id));
        if (qOk && quant > 0.0)
            c.scale = quant;
        if (c.scale != 1.0 || c.offset != 0.0) {
            out->bands.insert(it.key(), c);
            any = true;
        }
    }
    if (!any && errorMessage)
        *errorMessage = QStringLiteral("No Sentinel-2 quantification values found in MTD");
    return any;
}

// ---------------------------------------------------------------------------
// Generic GDAL fallback (MODIS scale/offset, embedded metadata)
// ---------------------------------------------------------------------------

bool loadGdalMetadata(const QString &rasterPath, const QMap<int, QString> &bandNames,
                      CalibrationMetadata *out, QString *errorMessage)
{
    ensureGdalInit();
    GDALDatasetH ds = GDALOpen(rasterPath.toUtf8().constData(), GA_ReadOnly);
    if (!ds) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to open raster: %1").arg(rasterPath);
        return false;
    }

    out->sensor = SensorType::Generic;
    const char *sc = GDALGetMetadataItem(ds, "SICNU_SPACECRAFT", nullptr);
    if (sc)
        out->spacecraft = QString::fromUtf8(sc);
    const char *pl = GDALGetMetadataItem(ds, "SICNU_PROCESSING_LEVEL", nullptr);
    if (pl)
        out->processingLevel = QString::fromUtf8(pl);
    const char *se = GDALGetMetadataItem(ds, "SUN_ELEVATION", nullptr);
    bool seOk = false;
    const double sunEl = se ? QString::fromUtf8(se).toDouble(&seOk) : 90.0;
    if (seOk && sunEl > 0.0 && sunEl < 90.0)
        out->sunElevationDeg = sunEl;

    const int bandCount = GDALGetRasterCount(ds);
    bool any = false;
    for (int b = 1; b <= bandCount; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b);
        if (!band)
            continue;
        BandCoefficients c;
        int hasScale = 0, hasOffset = 0;
        c.scale = GDALGetRasterScale(band, &hasScale);
        c.offset = GDALGetRasterOffset(band, &hasOffset);
        if (hasScale == 0)
            c.scale = 1.0;
        if (hasOffset == 0)
            c.offset = 0.0;
        if (hasScale || hasOffset) {
            out->bands.insert(b, c);
            any = true;
        }
    }
    GDALClose(ds);
    if (!any && errorMessage)
        *errorMessage = QStringLiteral("No scale/offset metadata found in raster bands");
    return any;
}

} // namespace

// ---------------------------------------------------------------------------
// Public metadata loader
// ---------------------------------------------------------------------------

bool loadMetadata(const QString &rasterPath, const QString &metadataPath,
                  const QMap<int, QString> &bandNames, CalibrationMetadata *out,
                  QString *errorMessage)
{
    if (!out)
        return false;

    if (!metadataPath.isEmpty()) {
        const QString name = QFileInfo(metadataPath).fileName();
        if (name.contains(QStringLiteral("MTL"), Qt::CaseInsensitive))
            return loadLandsatMtl(metadataPath, bandNames, out, errorMessage);
        if (name.startsWith(QStringLiteral("MTD_MSI"), Qt::CaseInsensitive))
            return loadSentinel2Mtd(metadataPath, bandNames, out, errorMessage);
        if (errorMessage)
            *errorMessage = QStringLiteral("Unrecognized metadata file: %1").arg(metadataPath);
        return false;
    }
    // Fall back to GDAL-embedded metadata.
    return loadGdalMetadata(rasterPath, bandNames, out, errorMessage);
}

// ---------------------------------------------------------------------------
// Calibration kernels
// ---------------------------------------------------------------------------

bool toRadiance(const float *dn, float *radiance, size_t count, const BandCoefficients &c)
{
    return MathUtils::linearScale(dn, radiance, count,
                                  static_cast<float>(c.radianceGain),
                                  static_cast<float>(c.radianceBias));
}

bool toToaReflectance(const float *dn, float *reflectance, size_t count,
                      const BandCoefficients &c, SensorType sensor,
                      double sunElevationDeg)
{
    if (!dn || !reflectance || count == 0) return false;

    if (sensor == SensorType::Landsat) {
        // Landsat: rho = (reflMult*DN + reflAdd) / sin(sunEl)
        // Requires REFLECTANCE_MULT/ADD from MTL and a valid sun elevation.
        if (c.reflMult == 1.0 && c.reflAdd == 0.0)
            return false;  // no reflectance coefficients loaded
        const double sinEl = std::sin(sunElevationDeg * M_PI / 180.0);
        if (sinEl <= 0.0) return false;
        const float mult = static_cast<float>(c.reflMult);
        const float add = static_cast<float>(c.reflAdd);
        const float invSin = static_cast<float>(1.0 / sinEl);
        for (size_t i = 0; i < count; i++)
            reflectance[i] = (mult * dn[i] + add) * invSin;
    } else {
        // Sentinel-2 / generic: rho = (DN + offset) / scale
        if (c.scale == 0.0) return false;
        const float offset = static_cast<float>(c.offset);
        const float invScale = static_cast<float>(1.0 / c.scale);
        for (size_t i = 0; i < count; i++)
            reflectance[i] = (dn[i] + offset) * invScale;
    }
    return true;
}

bool toBrightnessTemperature(const float *dn, float *temperature, size_t count,
                             const BandCoefficients &c)
{
    if (!dn || !temperature || count == 0) return false;
    if (c.k1 <= 0.0 || c.k2 <= 0.0) return false;
    const float gain = static_cast<float>(c.radianceGain);
    const float bias = static_cast<float>(c.radianceBias);
    const float k1 = static_cast<float>(c.k1);
    const float k2 = static_cast<float>(c.k2);
    for (size_t i = 0; i < count; i++) {
        const float l = gain * dn[i] + bias;
        if (l <= 0.0f) {
            temperature[i] = 0.0f;
            continue;
        }
        temperature[i] = k2 / std::log(k1 / l + 1.0f);
    }
    return true;
}

// ---------------------------------------------------------------------------
// File-level processing
// ---------------------------------------------------------------------------

bool processFile(const QString &sourcePath, const QString &outputPath,
                 const QString &metadataPath, int method,
                 const QList<int> &bandIndices,
                 QString *errorMessage,
                 const std::function<void(double, const QString &)> &progress)
{
    if (method < 0 || method > 2) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Unknown radiometric calibration method: %1").arg(method);
        return false;
    }

    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();

    // Resolve the band set to process (1-based).
    QList<int> bands = bandIndices;
    if (bands.isEmpty()) {
        for (int b = 1; b <= bandCount; ++b)
            bands.append(b);
    }

    // Build band-name map from GDAL band descriptions (e.g. "B4") so that MTL/MTD
    // coefficients can be associated with the correct stacked band. When no
    // descriptions are set, fall back to synthetic "B<index>" names so identity
    // band mapping still works (raster band i == MTL band i).
    QMap<int, QString> bandNames;
    for (int b = 1; b <= bandCount; ++b) {
        const QString desc = srcDataset.bandDescription(b);
        if (!desc.isEmpty())
            bandNames.insert(b, desc);
    }
    if (bandNames.isEmpty()) {
        for (int b = 1; b <= bandCount; ++b)
            bandNames.insert(b, QStringLiteral("B%1").arg(b));
    }

    CalibrationMetadata meta;
    QString metaErr;
    if (!loadMetadata(sourcePath, metadataPath, bandNames, &meta, &metaErr)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to load calibration metadata: %1").arg(metaErr);
        return false;
    }

    const OutputUnit unit = static_cast<OutputUnit>(method);
    SICNU_LOG_INFO(SicnuLogTags::Algorithms,
                   QString("Radiometric calibration: %1 bands, unit=%2, sensor=%3, sunEl=%4")
                       .arg(bands.size())
                       .arg(static_cast<int>(unit))
                       .arg(static_cast<int>(meta.sensor))
                       .arg(meta.sunElevationDeg));

    // Validate every requested band up front so deterministic failures (bad
    // band index, missing coefficients) never leave a partial output file.
    for (int b : bands) {
        if (b < 1 || b > bandCount) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Band %1 out of range (1..%2)").arg(b).arg(bandCount);
            return false;
        }
        if (!meta.bands.contains(b)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("No calibration coefficients for band %1").arg(b);
            return false;
        }
    }

    // Create the output raster up front so each band can be written tile by
    // tile as it is produced. Memory stays O(tile) instead of O(width*height),
    // so multi-GB rasters calibrate without OOM (out-of-core path).
    GdalDatasetWrapper outDataset;
    if (!outDataset.create(outputPath, width, height, bands.size(), GDT_Float32,
                           srcDataset.geoTransform(), srcDataset.projection(), errorMessage))
        return false;

    constexpr int kTile = 256; // nominal stream tile size (edge-clamped)
    for (int idx = 0; idx < bands.size(); ++idx) {
        const int b = bands[idx];
        if (progress)
            progress(static_cast<double>(idx) / bands.size(),
                     QStringLiteral("Calibrating band %1").arg(b));

        const BandCoefficients &c = meta.bands.value(b);

        std::vector<float> out;
        QString tileError;
        const bool ok = GdalBlockStream(srcDataset, b, kTile, kTile).forEach(
            [&](const GdalBlockStream::Tile &tile, const float *pixels) {
                const size_t tileCount = static_cast<size_t>(tile.width) * tile.height;
                out.resize(tileCount);
                bool tOk = false;
                switch (unit) {
                case OutputUnit::Radiance:
                    tOk = toRadiance(pixels, out.data(), tileCount, c);
                    break;
                case OutputUnit::ToaReflectance:
                    tOk = toToaReflectance(pixels, out.data(), tileCount, c, meta.sensor, meta.sunElevationDeg);
                    break;
                case OutputUnit::BrightnessTemperature:
                    tOk = toBrightnessTemperature(pixels, out.data(), tileCount, c);
                    break;
                }
                if (!tOk) {
                    tileError = QStringLiteral("Calibration failed for band %1 (missing coefficients?)").arg(b);
                    return false;
                }
                return outDataset.writeBandWindow(idx + 1, tile.xOffset, tile.yOffset,
                                                  tile.width, tile.height, out.data());
            });
        if (!ok) {
            if (errorMessage)
                *errorMessage = tileError.isEmpty()
                                    ? QStringLiteral("Failed to stream band %1").arg(b)
                                    : tileError;
            return false;
        }
    }

    if (progress)
        progress(1.0, QStringLiteral("Radiometric calibration complete"));
    return true;
}

} // namespace RadiometricCalibration
