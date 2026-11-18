// tests/test_r35_t01_science_validity.cpp — R35-T01 WP02–WP06 validity contracts
//
// Independent truths: hand-computed parameter domains (PCA/MNF component
// normalization happens before any pixel scan), uninformative-state refusal
// (MAD/IR-MAD constant pairs), whole-spectrum finite filtering (VCA), the
// exactly-requested-or-fail endmember contract (PPI typed seam) and the
// finite-row-only FCLS QA denominator.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>

#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/spectral_unmixing.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace exp_spectral;
using Catch::Matchers::WithinAbs;

namespace
{

/// 3-band 4x4 float32 GeoTIFF with deterministic full-rank per-band variation
/// so PCA/MNF have real signal: shared ramp + per-band modulated residuals
/// (rank 3, distinct eigenvalues, stable eigenvectors in float32).
QString writeVariancedRaster(const QDir &dir, const QString &name)
{
    ensureGdalInit();
    const QString path = dir.filePath(name);
    const int W = 4, H = 4;
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    std::vector<std::vector<float>> bands(3, std::vector<float>(W * H));
    for (int b = 0; b < 3; ++b)
        for (int p = 0; p < W * H; ++p)
            bands[b][p] = 10.0f * (b + 1) + static_cast<float>(p % 7) * (0.5f + 0.25f * b)
                          + static_cast<float>(p) * 0.125f * (b + 1)
                          + static_cast<float>((p * 7 + b * 13) % 11) * 0.37f * (b + 1);
    QString err;
    REQUIRE(writeGdalOutput(path, W, H, bands, gt, QString(), &err));
    return path;
}

GDALDatasetH reopenReadOnly(const QString &path)
{
    GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(ds != nullptr);
    return ds;
}

// 4-band, 3-endmember ground truth (same family as the D13 mission cube).
const std::vector<float> kEndmembers = {
    0.10f, 0.20f, 0.30f, 0.40f,
    0.05f, 0.10f, 0.60f, 0.70f,
    0.50f, 0.50f, 0.50f, 0.50f,
};

std::vector<float> mixedSpectrum(float f1, float f2, float f3)
{
    std::vector<float> y(4);
    for (int b = 0; b < 4; ++b)
        y[b] = f1 * kEndmembers[b] + f2 * kEndmembers[4 + b] + f3 * kEndmembers[8 + b];
    return y;
}

} // namespace

// ── WP02: processPcaFile parameter domain ───────────────────────────────────

TEST_CASE("processPcaFile normalizes non-positive component counts as all bands (WP02)",
          "[r35t01][pca]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = writeVariancedRaster(QDir(dir.path()), QStringLiteral("pca_in.tif"));

    SECTION("0 means all bands")
    {
        const QString out = dir.filePath(QStringLiteral("pca_all.tif"));
        QString err;
        REQUIRE(ImageEnhancement::processPcaFile(source, out, 0, &err));
        GDALDatasetH ds = reopenReadOnly(out);
        REQUIRE(GDALGetRasterCount(ds) == 3);
        GDALClose(ds);
    }
    SECTION("negative counts are normalized, not fatal")
    {
        const QString out = dir.filePath(QStringLiteral("pca_neg.tif"));
        QString err;
        REQUIRE(ImageEnhancement::processPcaFile(source, out, -1, &err));
        GDALDatasetH ds = reopenReadOnly(out);
        REQUIRE(GDALGetRasterCount(ds) == 3);
        GDALClose(ds);
    }
    SECTION("1 component succeeds with a single output band")
    {
        const QString out = dir.filePath(QStringLiteral("pca_one.tif"));
        QString err;
        REQUIRE(ImageEnhancement::processPcaFile(source, out, 1, &err));
        GDALDatasetH ds = reopenReadOnly(out);
        REQUIRE(GDALGetRasterCount(ds) == 1);
        GDALClose(ds);
    }
    SECTION("over-band requests still fail early with the typed message")
    {
        const QString out = dir.filePath(QStringLiteral("pca_over.tif"));
        QString err;
        REQUIRE_FALSE(ImageEnhancement::processPcaFile(source, out, 4, &err));
        REQUIRE(err.contains(QLatin1String("exceeds band count")));
        REQUIRE_FALSE(QFile::exists(out));
    }
}

// ── WP03: processMnfFile parameter domain ───────────────────────────────────

TEST_CASE("processMnfFile normalizes non-positive component counts before scanning (WP03)",
          "[r35t01][mnf]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = writeVariancedRaster(QDir(dir.path()), QStringLiteral("mnf_in.tif"));

    SECTION("0 and negative counts mean all bands")
    {
        for (int req : {0, -2})
        {
            const QString out = dir.filePath(QStringLiteral("mnf_all_%1.tif").arg(req));
            QString err;
            REQUIRE(ImageEnhancement::processMnfFile(source, out, req, &err));
            GDALDatasetH ds = reopenReadOnly(out);
            REQUIRE(GDALGetRasterCount(ds) == 3);
            GDALClose(ds);
        }
    }
    SECTION("over-band requests fail with the typed message, no output file")
    {
        const QString out = dir.filePath(QStringLiteral("mnf_over.tif"));
        QString err;
        REQUIRE_FALSE(ImageEnhancement::processMnfFile(source, out, 4, &err));
        REQUIRE(err.contains(QLatin1String("exceeds band count")));
        REQUIRE_FALSE(QFile::exists(out));
    }
    SECTION("single-band rasters keep the at-least-two-bands refusal")
    {
        const QString single = dir.filePath(QStringLiteral("mnf_single.tif"));
        ensureGdalInit();
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        std::vector<std::vector<float>> bands(1, std::vector<float>(16, 3.0f));
        QString werr;
        REQUIRE(writeGdalOutput(single, 4, 4, bands, gt, QString(), &werr));
        const QString out = dir.filePath(QStringLiteral("mnf_single_out.tif"));
        QString err;
        REQUIRE_FALSE(ImageEnhancement::processMnfFile(single, out, 0, &err));
        REQUIRE(err.contains(QLatin1String("at least 2 bands")));
    }
}

TEST_CASE("processMnfFile ordering is invariant under a global unit rescale (WP03)",
          "[r35t01][mnf][numerical]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir d(dir.path());
    const QString a = writeVariancedRaster(d, QStringLiteral("mnf_scale_a.tif"));

    // b = 100 * a: whitening rescales the transform by 1/s while the data
    // rescales by s, so every MNF component is scale-INVARIANT — the
    // ordering/reconstruction relation the affine contract relies on.
    ensureGdalInit();
    const QString b = d.filePath(QStringLiteral("mnf_scale_b.tif"));
    {
        GDALDatasetH src = reopenReadOnly(a);
        const int W = GDALGetRasterXSize(src), H = GDALGetRasterYSize(src);
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        std::vector<std::vector<float>> bands(3, std::vector<float>(W * H));
        for (int band = 1; band <= 3; ++band)
            REQUIRE(GDALRasterIO(GDALGetRasterBand(src, band), GF_Read, 0, 0, W, H,
                                 bands[band - 1].data(), W, H, GDT_Float32, 0, 0) == CE_None);
        GDALClose(src);
        for (auto &band : bands)
            for (float &v : band)
                v *= 100.0f;
        QString werr;
        REQUIRE(writeGdalOutput(b, W, H, bands, gt, QString(), &werr));
    }

    const QString outA = d.filePath(QStringLiteral("mnf_out_a.tif"));
    const QString outB = d.filePath(QStringLiteral("mnf_out_b.tif"));
    QString errA, errB;
    REQUIRE(ImageEnhancement::processMnfFile(a, outA, 2, &errA));
    REQUIRE(ImageEnhancement::processMnfFile(b, outB, 2, &errB));

    GDALDatasetH dsA = reopenReadOnly(outA);
    GDALDatasetH dsB = reopenReadOnly(outB);
    std::vector<float> pa(16), pb(16);
    for (int comp = 1; comp <= 2; ++comp)
    {
        REQUIRE(GDALRasterIO(GDALGetRasterBand(dsA, comp), GF_Read, 0, 0, 4, 4,
                             pa.data(), 4, 4, GDT_Float32, 0, 0) == CE_None);
        REQUIRE(GDALRasterIO(GDALGetRasterBand(dsB, comp), GF_Read, 0, 0, 4, 4,
                             pb.data(), 4, 4, GDT_Float32, 0, 0) == CE_None);
        for (int i = 0; i < 16; ++i)
        {
            INFO("component " << comp << " pixel " << i);
            REQUIRE(std::isfinite(pa[i]));
            REQUIRE(std::isfinite(pb[i]));
            const double scale = std::max(1.0, std::abs(static_cast<double>(pa[i])));
            REQUIRE_THAT(static_cast<double>(pb[i]),
                         WithinAbs(static_cast<double>(pa[i]), 1e-2 * scale));
        }
    }
    GDALClose(dsA);
    GDALClose(dsB);
}

// ── WP04: MAD uninformative states ──────────────────────────────────────────

namespace
{

std::vector<float> rampBand(size_t pixels, float base, float slope, size_t period)
{
    std::vector<float> v(pixels);
    for (size_t p = 0; p < pixels; ++p)
        v[p] = base + slope * static_cast<float>(p % period);
    return v;
}

/// Packs band-major planes into the const* API shape.
std::vector<const float *> bandPointers(std::vector<std::vector<float>> &planes)
{
    std::vector<const float *> ptrs;
    ptrs.reserve(planes.size());
    for (auto &plane : planes)
        ptrs.push_back(plane.data());
    return ptrs;
}

} // namespace

TEST_CASE("madChange refuses constant pairs instead of fabricating no-change (WP04)",
          "[r35t01][mad]")
{
    const int B = 3;
    const size_t N = 25; // >= B + 2

    SECTION("both images constant")
    {
        std::vector<std::vector<float>> before(B, std::vector<float>(N, 5.0f));
        std::vector<std::vector<float>> after(B, std::vector<float>(N, 7.0f));
        auto bp = bandPointers(before), ap = bandPointers(after);
        std::vector<float> out(N);
        QString err;
        REQUIRE_FALSE(ChangeDetection::madChange(bp.data(), ap.data(), B, N, out.data(), &err));
        REQUIRE(err.contains(QLatin1String("zero variance")));
    }
    SECTION("constant before, varying after")
    {
        std::vector<std::vector<float>> before(B, std::vector<float>(N, 5.0f));
        std::vector<std::vector<float>> after = {
            rampBand(N, 1.0f, 1.0f, 7), rampBand(N, 2.0f, 0.5f, 5), rampBand(N, 3.0f, 2.0f, 11)};
        auto bp = bandPointers(before), ap = bandPointers(after);
        std::vector<float> out(N);
        QString err;
        REQUIRE_FALSE(ChangeDetection::madChange(bp.data(), ap.data(), B, N, out.data(), &err));
        REQUIRE(err.contains(QLatin1String("zero variance")));
    }
    SECTION("identical images with real variance are a valid no-change pair")
    {
        // The uninformative refusal must not swallow genuine no-change
        // pairs: identical rasters carry variance, so MAD stays applicable.
        std::vector<std::vector<float>> img = {
            rampBand(N, 1.0f, 1.0f, 7), rampBand(N, 2.0f, 0.5f, 5), rampBand(N, 3.0f, 2.0f, 11)};
        auto bp = bandPointers(img), ap = bandPointers(img);
        std::vector<float> out(N);
        QString err;
        REQUIRE(ChangeDetection::madChange(bp.data(), ap.data(), B, N, out.data(), &err));
        for (float v : out)
        {
            REQUIRE(std::isfinite(v));
            REQUIRE_THAT(v, WithinAbs(0.0f, 1e-9f));
        }
    }
    SECTION("sample starvation (B+1 pixels) keeps its own message")
    {
        const size_t starved = static_cast<size_t>(B) + 1;
        std::vector<std::vector<float>> before = {
            rampBand(starved, 1.0f, 1.0f, 3), rampBand(starved, 2.0f, 0.5f, 2),
            rampBand(starved, 3.0f, 2.0f, 4)};
        std::vector<std::vector<float>> after = {
            rampBand(starved, 9.0f, 1.0f, 3), rampBand(starved, 8.0f, 0.5f, 2),
            rampBand(starved, 7.0f, 2.0f, 4)};
        auto bp = bandPointers(before), ap = bandPointers(after);
        std::vector<float> out(starved);
        QString err;
        REQUIRE_FALSE(ChangeDetection::madChange(bp.data(), ap.data(), B, starved, out.data(), &err));
        REQUIRE(err.contains(QLatin1String("Insufficient")));
    }
    SECTION("genuinely changed pair still reports positive statistics")
    {
        // A per-band constant shift is a no-change pair for centered CCA;
        // the real change here is localized pixel edits in band 0.
        std::vector<std::vector<float>> before = {
            rampBand(N, 1.0f, 1.0f, 7), rampBand(N, 2.0f, 0.5f, 5), rampBand(N, 3.0f, 2.0f, 11)};
        std::vector<std::vector<float>> after = before;
        for (size_t p = 0; p < 6; ++p)
            after[0][p] += 5.0f;
        auto bp = bandPointers(before), ap = bandPointers(after);
        std::vector<float> out(N);
        QString err;
        REQUIRE(ChangeDetection::madChange(bp.data(), ap.data(), B, N, out.data(), &err));
        REQUIRE(*std::max_element(out.begin(), out.end()) > 0.0f);
    }
}

TEST_CASE("irMadChange refuses constant pairs with the same typed message (WP04)",
          "[r35t01][mad][irmad]")
{
    const int B = 3;
    const size_t N = 25;
    std::vector<std::vector<float>> before(B, std::vector<float>(N, 5.0f));
    std::vector<std::vector<float>> after(B, std::vector<float>(N, 7.0f));
    auto bp = bandPointers(before), ap = bandPointers(after);
    std::vector<float> out(N);
    QString err;
    REQUIRE_FALSE(ChangeDetection::irMadChange(bp.data(), ap.data(), B, N, out.data(),
                                               5, 1e-4, &err));
    REQUIRE(err.contains(QLatin1String("zero variance")));
}

// ── WP05: VCA valid-spectrum set ────────────────────────────────────────────

TEST_CASE("VCA ignores non-finite spectra and extracts valid vertices (WP05)",
          "[r35t01][spectral][vca]")
{
    std::vector<float> cube;
    const auto append = [&cube](const std::vector<float> &v) {
        cube.insert(cube.end(), v.begin(), v.end());
    };
    append(mixedSpectrum(1.0f, 0.0f, 0.0f)); // pure E1
    append(mixedSpectrum(0.0f, 1.0f, 0.0f)); // pure E2
    append(mixedSpectrum(0.0f, 0.0f, 1.0f)); // pure E3
    append(mixedSpectrum(0.5f, 0.5f, 0.0f));
    append(mixedSpectrum(0.2f, 0.5f, 0.3f)); // 5 valid rows so far

    // Poison the cube with non-finite rows: NaN, +Inf, -Inf (one band each
    // and a whole-spectrum NaN). The valid simplex must still be extracted
    // from the remaining rows.
    for (int k = 0; k < 4; ++k)
    {
        std::vector<float> row(4, 0.5f);
        row[static_cast<size_t>(k) % 4] = std::numeric_limits<float>::quiet_NaN();
        if (k == 1)
            row[0] = std::numeric_limits<float>::infinity();
        if (k == 2)
            row[3] = -std::numeric_limits<float>::infinity();
        append(row);
    }

    std::vector<float> endmembers;
    REQUIRE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 3,
                                                EndmemberExtractionMethod::VertexComponentAnalysis,
                                                &endmembers));
    REQUIRE(endmembers.size() == 3 * 4);

    auto sameSpectrum = [](const float *a, const float *b) {
        for (int i = 0; i < 4; ++i)
            if (std::abs(a[i] - b[i]) > 1e-5f)
                return false;
        return true;
    };
    // Every output must be one of the ORIGINAL valid spectra — never a
    // non-finite row and never a zero-filled placeholder.
    for (int e = 0; e < 3; ++e)
    {
        const float *found = endmembers.data() + e * 4;
        for (int b = 0; b < 4; ++b)
            REQUIRE(std::isfinite(found[b]));
        const bool matched = sameSpectrum(found, kEndmembers.data())
                             || sameSpectrum(found, kEndmembers.data() + 4)
                             || sameSpectrum(found, kEndmembers.data() + 8)
                             || sameSpectrum(found, mixedSpectrum(0.5f, 0.5f, 0.0f).data())
                             || sameSpectrum(found, mixedSpectrum(0.2f, 0.5f, 0.3f).data());
        INFO("endmember slot " << e);
        REQUIRE(matched);
    }
}

TEST_CASE("VCA and PPI fail closed when finite spectra are fewer than requested (WP05/WP06)",
          "[r35t01][spectral][ppi]")
{
    // Two finite rows + three non-finite rows: three endmembers cannot exist.
    std::vector<float> cube;
    cube.insert(cube.end(), kEndmembers.begin(), kEndmembers.begin() + 8); // E1, E2
    for (int k = 0; k < 3; ++k)
    {
        std::vector<float> row(4, 0.25f);
        row[static_cast<size_t>(k)] = std::numeric_limits<float>::quiet_NaN();
        cube.insert(cube.end(), row.begin(), row.end());
    }

    for (auto method : {EndmemberExtractionMethod::VertexComponentAnalysis,
                        EndmemberExtractionMethod::PixelPurityIndex})
    {
        std::vector<float> endmembers(1, -1.0f); // sentinel: must be cleared/overwritten
        INFO("method " << static_cast<int>(method));
        REQUIRE_FALSE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 3,
                                                          method, &endmembers));
        REQUIRE(endmembers.empty());
    }

    // All-non-finite cube: refused as well, not zero-filled.
    std::vector<float> allNan(5 * 4, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> out;
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::extractEndmembers(allNan.data(), 5, 4, 2,
                                                      EndmemberExtractionMethod::PixelPurityIndex,
                                                      &out));
    REQUIRE(out.empty());
}

TEST_CASE("PPI typed seam returns exactly the requested count (WP06)",
          "[r35t01][spectral][ppi]")
{
    std::vector<float> cube;
    cube.insert(cube.end(), kEndmembers.begin(), kEndmembers.begin() + 4);
    cube.insert(cube.end(), kEndmembers.begin() + 4, kEndmembers.begin() + 8);
    cube.insert(cube.end(), kEndmembers.begin() + 8, kEndmembers.end());
    const std::vector<float> mixA = mixedSpectrum(0.5f, 0.5f, 0.0f);
    const std::vector<float> mixB = mixedSpectrum(0.2f, 0.5f, 0.3f);
    cube.insert(cube.end(), mixA.begin(), mixA.end());
    cube.insert(cube.end(), mixB.begin(), mixB.end());

    std::vector<float> endmembers;
    REQUIRE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 3,
                                                EndmemberExtractionMethod::PixelPurityIndex,
                                                &endmembers));
    REQUIRE(endmembers.size() == 3 * 4); // exactly requested — never silently short
}

// ── WP06: FCLS QA denominator ───────────────────────────────────────────────

TEST_CASE("FCLS QA violation is measured over finite abundance rows only (WP06)",
          "[r35t01][spectral][fcls]")
{
    SECTION("a NaN pixel does not poison the mean")
    {
        std::vector<float> pixels;
        const std::vector<float> valid = mixedSpectrum(0.5f, 0.3f, 0.2f);
        pixels.insert(pixels.end(), valid.begin(), valid.end());
        const std::vector<float> poisoned = mixedSpectrum(0.1f, 0.1f, 0.1f);
        pixels.insert(pixels.end(), poisoned.begin(), poisoned.end());
        pixels[4 * 1 + 1] = std::numeric_limits<float>::quiet_NaN(); // second row non-finite

        UnmixingResult result;
        REQUIRE(exp_spectral::SpectralUnmixing::unmixFcls(pixels.data(), 2, 4, kEndmembers.data(), 3, &result));
        REQUIRE(std::isnan(result.abundances[3]));
        REQUIRE(std::isfinite(result.meanSumConstraintViolation));
        // One valid contributor with a clean mixture: the QA mean is that row's
        // own violation, not an average diluted by the NaN row.
        double rowViolation = 0.0;
        double sum = 0.0;
        for (int e = 0; e < 3; ++e)
            sum += result.abundances[e];
        rowViolation = std::abs(sum - 1.0);
        REQUIRE_THAT(result.meanSumConstraintViolation, WithinAbs(rowViolation, 1e-12));
        REQUIRE(result.meanSumConstraintViolation < 1e-4);
    }
    SECTION("no finite contributor leaves the violation NaN, not a fake zero")
    {
        std::vector<float> pixels(2 * 4, std::numeric_limits<float>::quiet_NaN());
        UnmixingResult result;
        REQUIRE(exp_spectral::SpectralUnmixing::unmixFcls(pixels.data(), 2, 4, kEndmembers.data(), 3, &result));
        REQUIRE(std::isnan(result.meanSumConstraintViolation));
    }
}
