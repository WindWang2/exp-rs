// tests/synthetic_raster_builder.h — Fluent builder for synthetic remote sensing raster datasets
#pragma once

#include <QString>
#include <QFile>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

namespace sicnu::testing {

enum class SceneryType {
    Wildfire,
    Agriculture,
    UrbanSprawl,
    FloodInundation
};

/**
 * Fluent builder for generating synthetic multi-spectral and temporal raster datasets
 * for hermetic unit and end-to-end testing.
 */
class RsSyntheticRasterBuilder {
public:
    explicit RsSyntheticRasterBuilder(int width, int height, int bands = 1, GDALDataType dtype = GDT_Float32)
        : m_width(width)
        , m_height(height)
        , m_bandCount(std::max(1, bands))
        , m_dataType(dtype)
        , m_geoTransform{0.0, 1.0, 0.0, static_cast<double>(height), 0.0, -1.0}
        , m_crs(QStringLiteral("EPSG:4326"))
        , m_hasNoData(false)
        , m_noDataVal(-9999.0)
    {
        ensureGdal();
        m_bandsData.resize(m_bandCount, std::vector<float>(static_cast<size_t>(m_width) * m_height, 0.0f));
    }

    RsSyntheticRasterBuilder& withCrs(const QString& epsg) {
        m_crs = epsg;
        return *this;
    }

    RsSyntheticRasterBuilder& withGeoTransform(double originX, double pixelSizeX, double originY, double pixelSizeY) {
        m_geoTransform[0] = originX;
        m_geoTransform[1] = pixelSizeX;
        m_geoTransform[2] = 0.0;
        m_geoTransform[3] = originY;
        m_geoTransform[4] = 0.0;
        m_geoTransform[5] = pixelSizeY;
        return *this;
    }

    RsSyntheticRasterBuilder& withNoData(double nodataValue) {
        m_hasNoData = true;
        m_noDataVal = nodataValue;
        return *this;
    }

    RsSyntheticRasterBuilder& withConstantValue(int band1Based, float value) {
        if (band1Based >= 1 && band1Based <= m_bandCount) {
            std::fill(m_bandsData[band1Based - 1].begin(), m_bandsData[band1Based - 1].end(), value);
        }
        return *this;
    }

    RsSyntheticRasterBuilder& withRampPattern(int band1Based, float minVal, float maxVal) {
        if (band1Based >= 1 && band1Based <= m_bandCount) {
            auto& buf = m_bandsData[band1Based - 1];
            const size_t total = buf.size();
            for (size_t i = 0; i < total; ++i) {
                const float t = (total > 1) ? static_cast<float>(i) / static_cast<float>(total - 1) : 0.0f;
                buf[i] = minVal + t * (maxVal - minVal);
            }
        }
        return *this;
    }

    RsSyntheticRasterBuilder& withCheckerboard(int band1Based, int squareSize, float val1, float val2) {
        if (band1Based >= 1 && band1Based <= m_bandCount && squareSize > 0) {
            auto& buf = m_bandsData[band1Based - 1];
            for (int y = 0; y < m_height; ++y) {
                for (int x = 0; x < m_width; ++x) {
                    const int check = ((x / squareSize) + (y / squareSize)) % 2;
                    buf[static_cast<size_t>(y) * m_width + x] = (check == 0) ? val1 : val2;
                }
            }
        }
        return *this;
    }

    RsSyntheticRasterBuilder& withRect(int band1Based, int x0, int y0, int x1, int y1, float rectVal) {
        if (band1Based >= 1 && band1Based <= m_bandCount) {
            auto& buf = m_bandsData[band1Based - 1];
            const int rx0 = std::max(0, std::min(x0, m_width - 1));
            const int rx1 = std::max(0, std::min(x1, m_width));
            const int ry0 = std::max(0, std::min(y0, m_height - 1));
            const int ry1 = std::max(0, std::min(y1, m_height));
            for (int y = ry0; y < ry1; ++y) {
                for (int x = rx0; x < rx1; ++x) {
                    buf[static_cast<size_t>(y) * m_width + x] = rectVal;
                }
            }
        }
        return *this;
    }

    RsSyntheticRasterBuilder& withCircle(int band1Based, int centerX, int centerY, int radius, float insideVal, float outsideVal) {
        if (band1Based >= 1 && band1Based <= m_bandCount) {
            auto& buf = m_bandsData[band1Based - 1];
            const double r2 = static_cast<double>(radius) * radius;
            for (int y = 0; y < m_height; ++y) {
                for (int x = 0; x < m_width; ++x) {
                    const double dx = x - centerX;
                    const double dy = y - centerY;
                    const bool inside = (dx * dx + dy * dy <= r2);
                    buf[static_cast<size_t>(y) * m_width + x] = inside ? insideVal : outsideVal;
                }
            }
        }
        return *this;
    }

    RsSyntheticRasterBuilder& withPixel(int band1Based, int x, int y, float val) {
        if (band1Based >= 1 && band1Based <= m_bandCount && x >= 0 && x < m_width && y >= 0 && y < m_height) {
            m_bandsData[band1Based - 1][static_cast<size_t>(y) * m_width + x] = val;
        }
        return *this;
    }

    RsSyntheticRasterBuilder& withSimulatedScenery(SceneryType type, bool isPostEvent = false) {
        switch (type) {
        case SceneryType::Wildfire: {
            // Bands: 1=Blue, 2=Green, 3=Red, 4=NIR (B8A), 5=SWIR1 (B11), 6=SWIR2 (B12)
            if (m_bandCount >= 6) {
                for (int y = 0; y < m_height; ++y) {
                    for (int x = 0; x < m_width; ++x) {
                        const size_t idx = static_cast<size_t>(y) * m_width + x;
                        // Background healthy forest: High NIR (0.6), Low Red (0.08), Low SWIR2 (0.1)
                        float blue = 0.04f, green = 0.08f, red = 0.06f;
                        float nir = 0.60f, swir1 = 0.18f, swir2 = 0.10f;

                        // Burn scar region (centered, radius 1/3 width)
                        const double dx = x - m_width / 2.0;
                        const double dy = y - m_height / 2.0;
                        const double dist2 = dx * dx + dy * dy;
                        const double r = std::min(m_width, m_height) / 3.0;

                        if (isPostEvent && (dist2 <= r * r)) {
                            // Post-fire burn scar: Low NIR (0.12), High SWIR2 (0.45), Elevated Red (0.18)
                            nir = 0.12f;
                            swir2 = 0.45f;
                            swir1 = 0.40f;
                            red = 0.18f;
                        }

                        m_bandsData[0][idx] = blue;
                        m_bandsData[1][idx] = green;
                        m_bandsData[2][idx] = red;
                        m_bandsData[3][idx] = nir;
                        m_bandsData[4][idx] = swir1;
                        m_bandsData[5][idx] = swir2;
                    }
                }
            }
            break;
        }
        case SceneryType::Agriculture: {
            // 4 bands: 1=Blue, 2=Green, 3=Red, 4=NIR
            if (m_bandCount >= 4) {
                const int fieldCols = 4;
                const int fieldRows = 4;
                const int cellW = m_width / fieldCols;
                const int cellH = m_height / fieldRows;

                for (int y = 0; y < m_height; ++y) {
                    for (int x = 0; x < m_width; ++x) {
                        const size_t idx = static_cast<size_t>(y) * m_width + x;
                        const int cellX = std::min(x / std::max(1, cellW), fieldCols - 1);
                        const int cellY = std::min(y / std::max(1, cellH), fieldRows - 1);
                        const int fieldId = cellY * fieldCols + cellX;

                        float red = 0.05f, green = 0.10f, blue = 0.03f, nir = 0.40f;
                        if (fieldId % 3 == 0) {
                            // High-vigor crop: very high NIR, low red
                            nir = 0.70f;
                            red = 0.04f;
                            green = 0.18f;
                        } else if (fieldId % 3 == 1) {
                            // Fallow / bare soil: moderate red, moderate NIR
                            nir = 0.22f;
                            red = 0.25f;
                            green = 0.15f;
                        } else {
                            // Moderate vegetation
                            nir = 0.45f;
                            red = 0.08f;
                            green = 0.12f;
                        }

                        m_bandsData[0][idx] = blue;
                        m_bandsData[1][idx] = green;
                        m_bandsData[2][idx] = red;
                        m_bandsData[3][idx] = nir;
                    }
                }
            }
            break;
        }
        case SceneryType::UrbanSprawl: {
            // 4 bands: 1=Blue, 2=Green, 3=Red, 4=NIR
            if (m_bandCount >= 4) {
                for (int y = 0; y < m_height; ++y) {
                    for (int x = 0; x < m_width; ++x) {
                        const size_t idx = static_cast<size_t>(y) * m_width + x;
                        // Base: Natural vegetation / rural
                        float blue = 0.03f, green = 0.08f, red = 0.05f, nir = 0.55f;

                        // Urban core on left side (x < width / 3)
                        if (x < m_width / 3) {
                            blue = 0.18f; green = 0.20f; red = 0.22f; nir = 0.24f;
                        }

                        // Expansion zone in middle (width/3 <= x < 2*width/3) that turns urban post-event
                        if (isPostEvent && x >= m_width / 3 && x < 2 * m_width / 3) {
                            blue = 0.19f; green = 0.21f; red = 0.23f; nir = 0.25f;
                        }

                        m_bandsData[0][idx] = blue;
                        m_bandsData[1][idx] = green;
                        m_bandsData[2][idx] = red;
                        m_bandsData[3][idx] = nir;
                    }
                }
            }
            break;
        }
        case SceneryType::FloodInundation: {
            // Bands: 1=Blue, 2=Green, 3=Red, 4=NIR, 5=SWIR1
            if (m_bandCount >= 5) {
                for (int y = 0; y < m_height; ++y) {
                    for (int x = 0; x < m_width; ++x) {
                        const size_t idx = static_cast<size_t>(y) * m_width + x;
                        // Base terrain: Green veg (moderate Green, low SWIR1, high NIR)
                        float blue = 0.05f, green = 0.12f, red = 0.08f, nir = 0.50f, swir1 = 0.15f;

                        // River channel / flooded basin across center
                        const bool inRiverValley = (y >= m_height * 2 / 5 && y <= m_height * 3 / 5);
                        if (isPostEvent && inRiverValley) {
                            // Water: Green > SWIR1, NIR drops drastically
                            green = 0.25f;
                            swir1 = 0.02f;
                            nir = 0.03f;
                            red = 0.04f;
                            blue = 0.15f;
                        }

                        m_bandsData[0][idx] = blue;
                        m_bandsData[1][idx] = green;
                        m_bandsData[2][idx] = red;
                        m_bandsData[3][idx] = nir;
                        m_bandsData[4][idx] = swir1;
                    }
                }
            }
            break;
        }
        }
        return *this;
    }

    QString writeToDisk(const QString& filePath) const {
        ensureGdal();
        GDALDriverH driver = GDALGetDriverByName("GTiff");
        if (!driver) {
            return QString();
        }

        GDALDatasetH ds = GDALCreate(
            driver,
            filePath.toUtf8().constData(),
            m_width,
            m_height,
            m_bandCount,
            m_dataType,
            nullptr
        );
        if (!ds) {
            return QString();
        }

        GDALSetGeoTransform(ds, const_cast<double*>(m_geoTransform.data()));

        if (!m_crs.isEmpty()) {
            OGRSpatialReference srs;
            srs.SetFromUserInput(m_crs.toUtf8().constData());
            char* wkt = nullptr;
            srs.exportToWkt(&wkt);
            if (wkt) {
                GDALSetProjection(ds, wkt);
                CPLFree(wkt);
            }
        }

        for (int b = 0; b < m_bandCount; ++b) {
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            if (!band) continue;

            if (m_hasNoData) {
                GDALSetRasterNoDataValue(band, m_noDataVal);
            }

            GDALRasterIO(
                band,
                GF_Write,
                0, 0,
                m_width, m_height,
                const_cast<float*>(m_bandsData[b].data()),
                m_width, m_height,
                GDT_Float32,
                0, 0
            );
        }

        GDALClose(ds);
        return filePath;
    }

    const std::vector<std::vector<float>>& toVectors() const {
        return m_bandsData;
    }

    const std::vector<float>& band(int band1Based) const {
        return m_bandsData.at(band1Based - 1);
    }

    int width() const { return m_width; }
    int height() const { return m_height; }
    int bandCount() const { return m_bandCount; }

private:
    static void ensureGdal() {
        static bool s_registered = (GDALAllRegister(), true);
        (void)s_registered;
    }

    int m_width;
    int m_height;
    int m_bandCount;
    GDALDataType m_dataType;
    std::array<double, 6> m_geoTransform;
    QString m_crs;
    bool m_hasNoData;
    double m_noDataVal;
    std::vector<std::vector<float>> m_bandsData;
};

} // namespace sicnu::testing
