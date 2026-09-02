// src/processing/algorithms/radiometric_calibration.cpp — DN to physical units
#include "radiometric_calibration.h"
#include "math_utils.h"
#include "satellite_products.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "core/sicnu_logging.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDir>
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
/// Landsat 7 VCID names ("B6_VCID_1", MTL suffix "6_VCID_1") are special: the
/// last digit group is the VCID index, so the FIRST digit group (the band
/// number 6) is returned (#699) — the trailing parse silently mapped the
/// thermal VCID bands onto band 1/2.
int bandNumberFromName(const QString &name)
{
    if (name.contains(QStringLiteral("VCID"), Qt::CaseInsensitive)) {
        static const QRegularExpression firstRe(QStringLiteral("(\\d+)"));
        const auto mFirst = firstRe.match(name);
        if (mFirst.hasMatch())
            return mFirst.captured(1).toInt();
        return 0;
    }
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

    // Reset: callers may reuse one struct across loads; stale flags/bands
    // from a previous metadata file must never leak through.
    *out = CalibrationMetadata{};
    out->sensor = SensorType::Landsat;
    out->spacecraft = kv.value(QStringLiteral("SPACECRAFT_ID"));
    out->processingLevel = kv.value(QStringLiteral("PROCESSING_LEVEL"));
    out->acquisitionDate = kv.value(QStringLiteral("DATE_ACQUIRED"));
    bool sunOk = false;
    const double sunEl = toDouble(kv.value(QStringLiteral("SUN_ELEVATION")), &sunOk);
    if (sunOk && sunEl > 0.0 && sunEl <= 90.0) {
        out->sunElevationDeg = sunEl;
        out->hasSunElevation = true;
    }

    // Map raster band index (1-based) -> Landsat band number.
    // Prefer the band-name mapping; fall back to identity (band i == MTL band i).
    // When bandNames is empty, auto-discover the band set from the MTL keys
    // themselves so callers relying on the documented "keyed by band number"
    // contract still get coefficients (see loadMetadata() doc).
    QMap<int, QString> effective = bandNames;
    if (effective.isEmpty()) {
        const QStringList coeffPrefixes = {
            QStringLiteral("RADIANCE_MULT_BAND_"), QStringLiteral("RADIANCE_ADD_BAND_"),
            QStringLiteral("REFLECTANCE_MULT_BAND_"), QStringLiteral("REFLECTANCE_ADD_BAND_"),
            QStringLiteral("K1_CONSTANT_BAND_"), QStringLiteral("K2_CONSTANT_BAND_")
        };
        for (auto kit = kv.constBegin(); kit != kv.constEnd(); ++kit) {
            for (const QString &prefix : coeffPrefixes) {
                if (!kit.key().startsWith(prefix))
                    continue;
                // Keep the raw token (e.g. "4" or "ST_B10") so Collection-2
                // prefixed keys match via the first candidate, with the plain
                // numeric key still covered by the numeric fallback.
                const QString token = kit.key().mid(prefix.size());
                const int n = bandNumberFromName(token);
                if (n > 0 && !effective.contains(n))
                    effective.insert(n, token);
            }
        }
    }
    QMap<int, int> rasterToLandsatBand;
    for (auto it = effective.constBegin(); it != effective.constEnd(); ++it) {
        const int n = bandNumberFromName(it.value());
        if (n > 0)
            rasterToLandsatBand.insert(it.key(), n);
    }

    const auto landsatBandFor = [&](int rasterBand) {
        auto it = rasterToLandsatBand.constFind(rasterBand);
        return it != rasterToLandsatBand.constEnd() ? it.value() : rasterBand;
    };
    // Keep original band token (e.g. ST_B10, SR_B4) for C2 key lookup alongside numeric fallback.
    QMap<int, QString> rasterToRawToken;
    for (auto it = effective.constBegin(); it != effective.constEnd(); ++it)
        rasterToRawToken.insert(it.key(), it.value().toUpper());

    // Landsat 7 stores band 6 twice (RADIANCE_MULT_BAND_6_VCID_1/_2). The
    // numeric fallback parses the LAST digit group, so "B6_VCID_1" would
    // resolve to band 1 and silently apply blue-band coefficients to a
    // thermal channel (#699): the verbatim MTL suffix must be tried first,
    // and a _VCID token must never fall through to the (wrong) numeric key.
    auto isVcidToken = [](const QString &token) {
        return token.contains(QStringLiteral("VCID"));
    };

    // Helper: try a list of candidate suffixes for a given MTL prefix and return the first match.
    auto tryCandidates = [&](const QString &prefix, const QStringList &candidates,
                             double *outVal, bool *ok) -> bool {
        for (const QString &cand : candidates) {
            bool tOk = false;
            const double v = toDouble(kv.value(prefix + cand), &tOk);
            if (tOk) {
                *outVal = v;
                *ok = true;
                return true;
            }
        }
        return false;
    };

    bool any = false;
    for (auto it = effective.constBegin(); it != effective.constEnd(); ++it) {
        const int rasterBand = it.key();
        const int lb = landsatBandFor(rasterBand);
        const QString raw = rasterToRawToken.value(rasterBand);
        // Candidate suffixes tried in order:
        //   1. raw token verbatim (ST_B10 / 6_VCID_1 from auto-discovery),
        //   2. raw token with a leading "B" stripped (B6_VCID_1 -> 6_VCID_1,
        //      the actual MTL key suffix for Landsat 7 VCID bands),
        //   3. the numeric band (10) — EXCEPT for _VCID tokens, whose last
        //      digit group is the VCID index, not the band number (#699):
        //      failing closed (defaults + hasRadiance=false, so dn_to_radiance
        //      throws) beats applying band-1 coefficients to a thermal channel.
        QStringList candNumeric;
        if (!raw.isEmpty())
            candNumeric.append(raw);
        if (raw.startsWith(QLatin1Char('B')) && raw.size() > 1 && raw.at(1).isDigit())
            candNumeric.append(raw.mid(1));
        const bool vcid = !raw.isEmpty() && isVcidToken(raw);
        if (!vcid)
            candNumeric.append(QString::number(lb));
        BandCoefficients c;  // defaults: gain=1, bias=0, reflMult=1, reflAdd=0, scale=1
        bool gOk = false, bOk = false, rmOk = false, raOk = false, k1Ok = false, k2Ok = false;
        double v;
        // Radiance
        tryCandidates(QStringLiteral("RADIANCE_MULT_BAND_"), candNumeric, &v, &gOk);
        if (gOk) c.radianceGain = v;
        tryCandidates(QStringLiteral("RADIANCE_ADD_BAND_"), candNumeric, &v, &bOk);
        if (bOk) c.radianceBias = v;
        if (!gOk && !bOk) {
            // Collection 1 stores rescaling as RADIANCE_MULT / RADIANCE_ADD (no band suffix).
            v = toDouble(kv.value(QStringLiteral("RADIANCE_MULT")), &gOk);
            if (gOk) c.radianceGain = v;
            v = toDouble(kv.value(QStringLiteral("RADIANCE_ADD")), &bOk);
            if (bOk) c.radianceBias = v;
        }
        if (gOk || bOk) c.hasRadiance = true; // per-band gate for hasRadiance validation.
        // Reflectance
        tryCandidates(QStringLiteral("REFLECTANCE_MULT_BAND_"), candNumeric, &v, &rmOk);
        if (rmOk) c.reflMult = v;
        tryCandidates(QStringLiteral("REFLECTANCE_ADD_BAND_"), candNumeric, &v, &raOk);
        if (raOk) c.reflAdd = v;
        if (rmOk || raOk) c.hasReflectance = true;
        // Thermal constants
        tryCandidates(QStringLiteral("K1_CONSTANT_BAND_"), candNumeric, &v, &k1Ok);
        if (k1Ok) c.k1 = v;
        tryCandidates(QStringLiteral("K2_CONSTANT_BAND_"), candNumeric, &v, &k2Ok);
        if (k2Ok) c.k2 = v;
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
        // Collect value elements: first try the documented intermediate container,
        // then fall back to direct descendants of listEl (handles real ESA variations
        // like BOA_ADD_OFFSET_LIST vs BOA_LIST_TO_VALUES).
        auto collectFrom = [&](const QDomNodeList &items) {
            for (int k = 0; k < items.size(); ++k) {
                const QDomElement it = items.item(k).toElement();
                const QString id = it.attribute(QStringLiteral("band_id"));
                bool ok = false;
                const double v = it.text().trimmed().toDouble(&ok);
                if (ok && !id.isEmpty())
                    result.insert(id, v);
            }
        };
        const QDomNodeList values = listEl.elementsByTagName(toValuesTag);
        if (!values.isEmpty()) {
            for (int j = 0; j < values.size(); ++j) {
                const QDomElement lv = values.item(j).toElement();
                collectFrom(lv.elementsByTagName(valueTag));
            }
        }
        // Fallback / supplement: valueTag directly under listEl (covers alternate structures).
        collectFrom(listEl.elementsByTagName(valueTag));
        // Also try generic "*_LIST_VALUE" pattern fallback for robustness
        if (result.isEmpty()) {
            // Search any element with band_id under listEl irrespective of tag name suffix
            const QDomNodeList all = listEl.elementsByTagName(QStringLiteral("*"));
            for (int k = 0; k < all.size(); ++k) {
                const QDomElement it = all.item(k).toElement();
                if (!it.hasAttribute(QStringLiteral("band_id")))
                    continue;
                if (!it.tagName().endsWith(QStringLiteral("LIST_VALUE"), Qt::CaseInsensitive))
                    continue;
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

    // Reset: callers may reuse one struct across loads (see loadLandsatMtl).
    *out = CalibrationMetadata{};
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
    if (zenOk && zenith >= 0.0 && zenith <= 90.0) {
        out->sunElevationDeg = 90.0 - zenith;
        out->hasSunElevation = true;
    }

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

    // Real ESA tags: L1C uses QUANTIFICATION_VALUE, L2A uses BOA_QUANTIFICATION_VALUE
    // (with fallback to plain QUANTIFICATION_VALUE for old PB). Offsets: RADIO_ADD_OFFSET / BOA_ADD_OFFSET.
    // Keep backward compat with fictional RADIO_QUANTIFICATION_VALUE used in older tests.
    bool qOk = false;
    double quant = 0.0;
    // Try BOA_QUANTIFICATION_VALUE first for L2A, then plain QUANTIFICATION_VALUE, then legacy RADIO_ prefix.
    const QStringList quantCandidates = {
        QStringLiteral("BOA_QUANTIFICATION_VALUE"),
        QStringLiteral("QUANTIFICATION_VALUE"),
        QStringLiteral("RADIO_QUANTIFICATION_VALUE")
    };
    for (const QString &tag : quantCandidates) {
        const QString txt = mtdValue(rad, tag);
        if (!txt.isEmpty()) {
            bool ok = false;
            const double v = toDouble(txt, &ok);
            if (ok && v > 0.0) {
                quant = v;
                qOk = true;
                break;
            }
        }
    }

    // Offsets: try both BOA and RADIO lists, merging results. Handles real products and legacy fixtures.
    QMap<QString, double> offsets;
    const struct { QString listTag; QString toValuesTag; QString valueTag; } offsetVariants[] = {
        {QStringLiteral("BOA_ADD_OFFSET"), QStringLiteral("BOA_LIST_TO_VALUES"), QStringLiteral("BOA_LIST_VALUE")},
        {QStringLiteral("RADIO_ADD_OFFSET"), QStringLiteral("RADIO_LIST_TO_VALUES"), QStringLiteral("RADIO_LIST_VALUE")},
        // Alternate container naming seen in some PB versions:
        {QStringLiteral("BOA_ADD_OFFSET"), QStringLiteral("BOA_ADD_OFFSET_LIST"), QStringLiteral("BOA_LIST_VALUE")},
        {QStringLiteral("BOA_ADD_OFFSET"), QStringLiteral("BOA_LIST_TO_VALUES"), QStringLiteral("BOA_ADD_OFFSET")},
    };
    for (auto &var : offsetVariants) {
        auto m = parseQuantificationList(rad, var.listTag, var.toValuesTag, var.valueTag);
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            if (!offsets.contains(it.key()))
                offsets.insert(it.key(), it.value());
    }
    // Final generic fallback: any LIST_VALUE under any ADD_OFFSET element
    if (offsets.isEmpty()) {
        auto m1 = parseQuantificationList(rad, QStringLiteral("BOA_ADD_OFFSET"),
                                          QStringLiteral("BOA_LIST_TO_VALUES"), QStringLiteral("BOA_LIST_VALUE"));
        for (auto it = m1.constBegin(); it != m1.constEnd(); ++it) offsets.insert(it.key(), it.value());
        auto m2 = parseQuantificationList(rad, QStringLiteral("RADIO_ADD_OFFSET"),
                                          QStringLiteral("RADIO_LIST_TO_VALUES"), QStringLiteral("RADIO_LIST_VALUE"));
        for (auto it = m2.constBegin(); it != m2.constEnd(); ++it) offsets.insert(it.key(), it.value());
    }

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

    // When bandNames is empty, auto-discover the band set from the metadata
    // itself: bands with an add-offset entry, or — when only a global
    // quantification value exists — the full spectral band sequence. Keys are
    // the 1-based band number (band_id + 1), per the loadMetadata() contract.
    QMap<int, QString> effective = bandNames;
    if (effective.isEmpty()) {
        for (auto it = offsets.constBegin(); it != offsets.constEnd(); ++it) {
            bool idOk = false;
            const int id = it.key().toInt(&idOk);
            if (idOk && id >= 0 && id < s2Order.size())
                effective.insert(id + 1, s2Order.at(id));
        }
        if (effective.isEmpty() && qOk && quant > 0.0) {
            for (int i = 0; i < s2Order.size(); ++i)
                effective.insert(i + 1, s2Order.at(i));
        }
    }

    bool any = false;
    for (auto it = effective.constBegin(); it != effective.constEnd(); ++it) {
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

    // Reset: callers may reuse one struct across loads (see loadLandsatMtl).
    *out = CalibrationMetadata{};
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
    if (seOk && sunEl > 0.0 && sunEl <= 90.0) {
        out->sunElevationDeg = sunEl;
        out->hasSunElevation = true;
    }

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

QString autoDetectMetadataFile(const QString &rasterPath, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    const QFileInfo fi(rasterPath);
    const QDir dir = fi.absoluteDir();
    const QStringList mtl = dir.entryList({QStringLiteral("*_MTL.txt")},
                                          QDir::Files, QDir::Name);
    const QStringList mtd = dir.entryList({QStringLiteral("MTD_MSI*.xml")},
                                          QDir::Files, QDir::Name);

    if (mtl.isEmpty() && mtd.isEmpty())
        return {};
    // #699: multiple candidates mean multiple scenes (or arbitrary files)
    // share the directory; picking .first() alphabetically attached the wrong
    // coefficients. Fail closed and name the candidates instead of guessing.
    if (mtl.size() > 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Multiple Landsat MTL files found next to %1 (%2). "
                                           "Pass metadata_path explicitly.")
                                .arg(rasterPath, mtl.join(QStringLiteral(", ")));
        return {};
    }
    if (mtd.size() > 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Multiple Sentinel-2 MTD files found next to %1 (%2). "
                                           "Pass metadata_path explicitly.")
                                .arg(rasterPath, mtd.join(QStringLiteral(", ")));
        return {};
    }
    if (mtl.isEmpty())
        return dir.absoluteFilePath(mtd.first());
    if (mtd.isEmpty())
        return dir.absoluteFilePath(mtl.first());

    // Both families present: prefer the one matching the raster's embedded
    // product type (Landsat MTL default on ambiguity).
    QString productType;
    ensureGdalInit();
    GDALDatasetH ds = GDALOpen(rasterPath.toUtf8().constData(), GA_ReadOnly);
    if (ds) {
        const char *type = GDALGetMetadataItem(ds, "SICNU_PRODUCT_TYPE", nullptr);
        if (type && type[0])
            productType = QString::fromUtf8(type);
        GDALClose(ds);
    }
    if (productType.contains(QStringLiteral("Sentinel"), Qt::CaseInsensitive))
        return dir.absoluteFilePath(mtd.first());
    return dir.absoluteFilePath(mtl.first());
}

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
    if (!dn || !radiance || count == 0) return false;
    if (!std::isfinite(c.radianceGain) || !std::isfinite(c.radianceBias))
        return false;
    // Fail closed on the identity defaults (gain=1, bias=0) unless real
    // coefficients were loaded (#301): stamping DN through an identity
    // transform is not a radiance conversion. Explicitly configured
    // non-identity coefficients remain accepted.
    if (!c.hasRadiance && c.radianceGain == 1.0 && c.radianceBias == 0.0)
        return false;
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
        if (!std::isfinite(c.reflMult) || !std::isfinite(c.reflAdd))
            return false;
        if (c.reflMult == 1.0 && c.reflAdd == 0.0)
            return false;  // no reflectance coefficients loaded
        if (!std::isfinite(sunElevationDeg))
            return false;
        const double sinEl = std::sin(sunElevationDeg * M_PI / 180.0);
        if (sinEl <= 0.0 || !std::isfinite(sinEl)) return false;
        const float mult = static_cast<float>(c.reflMult);
        const float add = static_cast<float>(c.reflAdd);
        const float invSin = static_cast<float>(1.0 / sinEl);
        for (size_t i = 0; i < count; i++)
            reflectance[i] = (mult * dn[i] + add) * invSin;
    } else if (sensor == SensorType::Sentinel2) {
        // Sentinel-2 MTD: rho = (DN + offset) / scale
        if (!std::isfinite(c.scale) || !std::isfinite(c.offset))
            return false;
        if (std::abs(c.scale) <= 1e-12) return false;
        const float offset = static_cast<float>(c.offset);
        const float invScale = static_cast<float>(1.0 / c.scale);
        for (size_t i = 0; i < count; i++)
            reflectance[i] = (dn[i] + offset) * invScale;
    } else {
        // Generic / GDAL: phys = DN * scale + offset
        if (!std::isfinite(c.scale) || !std::isfinite(c.offset))
            return false;
        if (c.scale == 0.0 && c.offset == 0.0) return false;
        const float scale = static_cast<float>(c.scale);
        const float offset = static_cast<float>(c.offset);
        for (size_t i = 0; i < count; i++)
            reflectance[i] = dn[i] * scale + offset;
    }
    return true;
}

bool toBrightnessTemperature(const float *dn, float *temperature, size_t count,
                             const BandCoefficients &c)
{
    if (!dn || !temperature || count == 0) return false;
    if (!std::isfinite(c.k1) || !std::isfinite(c.k2) ||
        !std::isfinite(c.radianceGain) || !std::isfinite(c.radianceBias))
        return false;
    if (c.k1 <= 0.0 || c.k2 <= 0.0) return false;
    const float gain = static_cast<float>(c.radianceGain);
    const float bias = static_cast<float>(c.radianceBias);
    const float k1 = static_cast<float>(c.k1);
    const float k2 = static_cast<float>(c.k2);
    for (size_t i = 0; i < count; i++) {
        const float l = gain * dn[i] + bias;
        if (l <= 0.0f || !std::isfinite(l)) {
            // Non-positive radiance has no physical temperature. 0 K would be
            // a finite, plausible-looking value that survives nodata stamping;
            // NaN matches the file-level NoData convention instead.
            temperature[i] = std::numeric_limits<float>::quiet_NaN();
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
        const BandCoefficients &c = meta.bands.value(b);
        if (unit == OutputUnit::BrightnessTemperature &&
            (!c.hasRadiance || !std::isfinite(c.k1) || !std::isfinite(c.k2) || c.k1 <= 0.0 || c.k2 <= 0.0 ||
             !std::isfinite(c.radianceGain) || !std::isfinite(c.radianceBias))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Band %1 lacks valid thermal K1/K2/radiance constants for brightness temperature conversion").arg(b);
            return false;
        }
        // #654: the BT path performs the radiance step implicitly, so an
        // MTL without RADIANCE_MULT/ADD would silently run DN through the
        // Planck inversion - the same identity-defaults fail-closed rule as
        // toRadiance (#301).
        if (unit == OutputUnit::Radiance &&
            (!c.hasRadiance || !std::isfinite(c.radianceGain) || !std::isfinite(c.radianceBias))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Band %1 has no valid radiance coefficients (RADIANCE_MULT/ADD missing or non-finite)").arg(b);
            return false;
        }
        if (unit == OutputUnit::ToaReflectance) {
            // Fail closed on a missing sun elevation: the 90-degree default
            // silently skips the 1/sin(theta) normalization (a systematic
            // ~1.5x multiplicative bias at 42 degrees elevation).
            if (meta.sensor == SensorType::Landsat && !meta.hasSunElevation) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("SUN_ELEVATION missing from metadata; Landsat TOA reflectance requires it (refusing to default to 90 degrees)");
                return false;
            }
            if (meta.sensor == SensorType::Landsat &&
                (!c.hasReflectance || !std::isfinite(c.reflMult) || !std::isfinite(c.reflAdd))) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Band %1 has no valid reflectance coefficients (REFLECTANCE_MULT/ADD missing or non-finite)").arg(b);
                return false;
            }
            if (meta.sensor == SensorType::Sentinel2 &&
                (!std::isfinite(c.scale) || !std::isfinite(c.offset) || std::abs(c.scale) <= 1e-12)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Band %1 has invalid Sentinel-2 scale/offset (scale must be non-zero and finite)").arg(b);
                return false;
            }
            if (meta.sensor == SensorType::Generic &&
                (!std::isfinite(c.scale) || !std::isfinite(c.offset) || (c.scale == 0.0 && c.offset == 0.0))) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Band %1 has invalid generic scale/offset").arg(b);
                return false;
            }
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
        outDataset.setBandNoDataValue(idx + 1, std::numeric_limits<double>::quiet_NaN());
        if (progress)
            progress(static_cast<double>(idx) / bands.size(),
                     QStringLiteral("Calibrating band %1").arg(b));

        const BandCoefficients &c = meta.bands.value(b);
        bool hasSrcNoData = false;
        const double bandNoData = srcDataset.bandNoDataValue(b, &hasSrcNoData);
        const bool srcNoDataFValid = hasSrcNoData && std::isfinite(bandNoData);
        const float srcNoDataF = srcNoDataFValid ? static_cast<float>(bandNoData) : 0.0f;

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
                for (size_t i = 0; i < tileCount; ++i) {
                    const float v = pixels[i];
                    // Note: srcNoDataF only participates when the band declares a
                    // finite NoData; float-space compare matches large sentinels (#444).
                    if (!std::isfinite(v) || (hasSrcNoData && srcNoDataFValid && v == srcNoDataF)) {
                        out[i] = std::numeric_limits<float>::quiet_NaN();
                    }
                }
                return outDataset.writeBandWindow(idx + 1, tile.xOffset, tile.yOffset,
                                                  tile.width, tile.height, out.data());
            });
        if (!ok) {
            outDataset.close();
            QFile::remove(outputPath);
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
