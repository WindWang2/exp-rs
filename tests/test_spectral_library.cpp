// test_spectral_library.cpp — spectral library domain round-trip
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "processing/algorithms/spectral_library.h"

using SpectralLibrary::Entry;
using SpectralLibrary::Library;

TEST_CASE("Spectral library round-trips through JSON", "[spectral_library]")
{
    Library library;
    Entry vegetation;
    vegetation.name = QStringLiteral("Vegetation");
    vegetation.material = QStringLiteral("green vegetation");
    vegetation.source = QStringLiteral("USGS");
    vegetation.spectrum = {0.1f, 0.3f, 0.5f, 0.7f};
    vegetation.wavelengths = {400.0f, 500.0f, 600.0f, 700.0f};
    Entry water;
    water.name = QStringLiteral("Water");
    water.material = QStringLiteral("clear water");
    water.spectrum = {0.5f, 0.3f, 0.1f, 0.05f};
    water.wavelengths = vegetation.wavelengths;
    library.entries = {vegetation, water};

    REQUIRE(library.bandCount() == 4);
    REQUIRE(library.wavelengths() == vegetation.wavelengths);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("library.json"));

    QString err;
    REQUIRE(library.save(path, &err));

    Library loaded;
    REQUIRE(Library::load(path, &loaded, &err));
    REQUIRE(loaded.entries.size() == 2);
    CHECK(loaded.entries[0] == vegetation);
    CHECK(loaded.entries[1] == water);
    CHECK(loaded.bandCount() == 4);
    CHECK(loaded.wavelengths() == vegetation.wavelengths);
}

TEST_CASE("Spectral library round-trips empty and wavelength-free libraries", "[spectral_library]")
{
    SECTION("Empty library") {
        Library library;
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("empty.json"));
        QString err;
        REQUIRE(library.save(path, &err));
        Library loaded;
        REQUIRE(Library::load(path, &loaded, &err));
        CHECK(loaded.entries.isEmpty());
        CHECK(loaded.bandCount() == 0);
    }
    SECTION("No wavelengths") {
        Library library;
        Entry e;
        e.name = QStringLiteral("A");
        e.spectrum = {0.2f, 0.4f};
        library.entries = {e};
        CHECK(library.wavelengths().empty());

        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("no_wl.json"));
        QString err;
        REQUIRE(library.save(path, &err));
        Library loaded;
        REQUIRE(Library::load(path, &loaded, &err));
        CHECK(loaded.entries.first() == e);
    }
}

TEST_CASE("Spectral library rejects malformed input", "[spectral_library]")
{
    SECTION("Missing file") {
        Library out;
        QString err;
        CHECK_FALSE(Library::load(QStringLiteral("/nonexistent/library.json"), &out, &err));
        CHECK_FALSE(err.isEmpty());
    }
    SECTION("Missing entry name") {
        const QByteArray json = R"({"entries":[{"spectrum":[0.1,0.2]}]})";
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("bad.json"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(json);
        f.close();

        Library out;
        QString err;
        CHECK_FALSE(Library::load(path, &out, &err));
        CHECK(err.contains(QStringLiteral("name")));
    }
    SECTION("Inconsistent band counts") {
        const QByteArray json = R"({"entries":[
            {"name":"a","spectrum":[0.1,0.2]},
            {"name":"b","spectrum":[0.1,0.2,0.3]}
        ]})";
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("bad2.json"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(json);
        f.close();

        Library out;
        QString err;
        CHECK_FALSE(Library::load(path, &out, &err));
        CHECK(err.contains(QStringLiteral("inconsistent")));
    }
    SECTION("Wavelength grid size mismatch") {
        const QByteArray json = R"({"wavelengths":[400,500],"entries":[
            {"name":"a","spectrum":[0.1,0.2,0.3]}
        ]})";
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("bad3.json"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(json);
        f.close();

        Library out;
        QString err;
        CHECK_FALSE(Library::load(path, &out, &err));
    }
}

TEST_CASE("matchSpectrum ranks library entries by SAM angle", "[processing][spectral_library]") {
    // Test spectrum: a straight ramp 0.1, 0.2, 0.3, 0.4.
    const std::vector<float> spectrum = {0.1f, 0.2f, 0.3f, 0.4f};

    SpectralLibrary::Library lib;
    SpectralLibrary::Entry identical;
    identical.name = QStringLiteral("target");
    identical.material = QStringLiteral("vegetation");
    identical.spectrum = spectrum; // same direction -> angle ~0

    SpectralLibrary::Entry orthogonal;
    orthogonal.name = QStringLiteral("different");
    orthogonal.spectrum = {0.4f, 0.3f, 0.2f, 0.1f}; // reversed -> large angle

    SpectralLibrary::Entry shorter;
    shorter.name = QStringLiteral("short");
    shorter.spectrum = {0.1f, 0.2f, 0.3f}; // band mismatch -> skipped

    lib.entries.append( identical );
    lib.entries.append( orthogonal );
    lib.entries.append( shorter );

    const auto scores = SpectralLibrary::matchSpectrum( spectrum, lib );
    REQUIRE( scores.size() == 2 ); // the band-mismatched entry is skipped
    CHECK( scores[0].name == QStringLiteral( "target" ) );
    CHECK( scores[0].angleDegrees == Catch::Approx( 0.0 ).margin( 1e-4 ) );
    CHECK( scores[0].divergence == Catch::Approx( 0.0 ).margin( 1e-4 ) );
    CHECK( scores[1].name == QStringLiteral( "different" ) );
    CHECK( scores[1].angleDegrees > 10.0 );
}

TEST_CASE("matchSpectrum tolerates empty libraries and spectra", "[processing][spectral_library]") {
    SpectralLibrary::Library empty;
    CHECK( SpectralLibrary::matchSpectrum( {0.1f, 0.2f}, empty ).empty() );

    SpectralLibrary::Entry e;
    e.name = QStringLiteral( "e" );
    e.spectrum = {0.1f, 0.2f};
    empty.entries.append( e );
    CHECK( SpectralLibrary::matchSpectrum( {}, empty ).empty() );
}

TEST_CASE("matchSpectrum resamples onto the library grid when wavelengths are available", "[processing][spectral_library]") {
    // Test spectrum: linear ramp over 5 bands (value = wavelength / 1000).
    const std::vector<float> spectrum = {0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    const std::vector<float> spectrumWl = {400.0f, 500.0f, 600.0f, 700.0f, 800.0f};

    SpectralLibrary::Library lib;

    // 3-band entry on a coarser grid; the ramp interpolates exactly onto it.
    SpectralLibrary::Entry coarse;
    coarse.name = QStringLiteral("coarse_target");
    coarse.wavelengths = {500.0f, 600.0f, 700.0f};
    coarse.spectrum = {0.5f, 0.6f, 0.7f};

    // Entry without wavelength metadata and a mismatched band count -> skipped.
    SpectralLibrary::Entry noGrid;
    noGrid.name = QStringLiteral("no_grid");
    noGrid.spectrum = {0.5f, 0.6f, 0.7f};

    lib.entries.append( coarse );
    lib.entries.append( noGrid );

    const auto scores = SpectralLibrary::matchSpectrum( spectrum, spectrumWl, lib );
    REQUIRE( scores.size() == 1 );
    CHECK( scores[0].name == QStringLiteral( "coarse_target" ) );
    CHECK( scores[0].resampled == true );
    CHECK( scores[0].angleDegrees == Catch::Approx( 0.0 ).margin( 1e-4 ) );
    CHECK( scores[0].divergence == Catch::Approx( 0.0 ).margin( 1e-4 ) );
}

TEST_CASE("matchSpectrum keeps skipping band-mismatched entries without wavelengths", "[processing][spectral_library]") {
    // Same fixture as the resampling test, but the test spectrum carries no
    // wavelength grid: the mismatched entries must still be skipped.
    const std::vector<float> spectrum = {0.4f, 0.5f, 0.6f, 0.7f, 0.8f};

    SpectralLibrary::Library lib;
    SpectralLibrary::Entry coarse;
    coarse.name = QStringLiteral( "coarse_target" );
    coarse.wavelengths = {500.0f, 600.0f, 700.0f};
    coarse.spectrum = {0.5f, 0.6f, 0.7f};
    lib.entries.append( coarse );

    const auto scores = SpectralLibrary::matchSpectrum( spectrum, lib );
    CHECK( scores.empty() );
}

// ─── D13 · exp_spectral::SpectralLibrary retriever ────────────────────────
// Independent truths: orthogonal-geometry spectral angle (t=[1,1,0] vs
// r=[1,0,0] → θ = π/4), constant-spectrum resampling invariance, and JSON
// round-trip identity.
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <vector>

#include "core/spectral_library.h"

namespace
{
exp_spectral::SpectralLibraryEntry makeEntry(const QString &id, const QString &material,
                                             std::vector<float> spectrum)
{
    exp_spectral::SpectralLibraryEntry entry;
    entry.id = id;
    entry.name = id;
    entry.materialClass = material;
    entry.source = QStringLiteral("D13 synthetic");
    entry.spectrum = std::move(spectrum);
    return entry;
}
} // namespace

TEST_CASE("D13 SAM analytical ground truth from orthogonal geometry", "[spectral][library][d13]")
{
    exp_spectral::SpectralLibrary lib;
    auto unitX = makeEntry(QStringLiteral("unit_x"), QStringLiteral("axis"),
                           {1.0f, 0.0f, 0.0f});
    REQUIRE(lib.addEntry(unitX));

    // Query [1,1,0] vs reference [1,0,0]: cos θ = 1/√2 → θ = π/4 exactly.
    const std::vector<float> query = {1.0f, 1.0f, 0.0f};
    auto matches = lib.matchSpectrum(query.data(), 3, 1, 1.5);
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].id == QStringLiteral("unit_x"));
    REQUIRE_THAT(matches[0].spectralAngleRad, Catch::Matchers::WithinAbs(0.78539816, 1e-6));

    // Identical spectra: zero angle, perfect correlation, zero distance.
    auto same = lib.matchSpectrum(unitX.spectrum.data(), 3, 1, 0.1);
    REQUIRE(same.size() == 1);
    REQUIRE_THAT(same[0].spectralAngleRad, Catch::Matchers::WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(same[0].correlation, Catch::Matchers::WithinAbs(1.0, 1e-9));
    REQUIRE(same[0].euclideanDistance < 1e-9);
}

TEST_CASE("D13 SAM zero-norm spectra score at the maximum angle", "[spectral][library][d13]")
{
    exp_spectral::SpectralLibrary lib;
    REQUIRE(lib.addEntry(makeEntry(QStringLiteral("flat"), QStringLiteral("void"),
                                   {0.0f, 0.0f, 0.0f})));
    const std::vector<float> query = {0.2f, 0.3f, 0.4f};

    // Zero-norm reference → θ = π/2 (never NaN), and filtered out by a
    // maxAngleRad < π/2 gate.
    auto gated = lib.matchSpectrum(query.data(), 3, 5, 0.5);
    REQUIRE(gated.empty());
    // Gate above the physical maximum (pi/2 ~ 1.5708) admits the worst match.
    auto permissive = lib.matchSpectrum(query.data(), 3, 5, 1.6);
    REQUIRE(permissive.size() == 1);
    REQUIRE_THAT(permissive[0].spectralAngleRad, Catch::Matchers::WithinAbs(1.5707963, 1e-6));
}

TEST_CASE("D13 SAM ordering, topK truncation and band-count skip", "[spectral][library][d13]")
{
    exp_spectral::SpectralLibrary lib;
    // Reference angles vs query [1,1,0]: exact (0), π/4, and π/2 (orthogonal).
    REQUIRE(lib.addEntry(makeEntry(QStringLiteral("same"), QStringLiteral("a"), {1.0f, 1.0f, 0.0f})));
    REQUIRE(lib.addEntry(makeEntry(QStringLiteral("diag"), QStringLiteral("b"), {1.0f, 0.0f, 0.0f})));
    REQUIRE(lib.addEntry(makeEntry(QStringLiteral("ortho"), QStringLiteral("c"), {1.0f, -1.0f, 0.0f})));
    // Incomparable geometry: 4 bands vs 3-band query → skipped.
    REQUIRE(lib.addEntry(makeEntry(QStringLiteral("wide"), QStringLiteral("d"),
                                   {1.0f, 0.0f, 0.0f, 0.0f})));

    const std::vector<float> query = {1.0f, 1.0f, 0.0f};
    auto all = lib.matchSpectrum(query.data(), 3, 10, 1.6);
    REQUIRE(all.size() == 3); // "wide" skipped
    REQUIRE(all[0].id == QStringLiteral("same"));
    REQUIRE(all[1].id == QStringLiteral("diag"));
    REQUIRE(all[2].id == QStringLiteral("ortho"));

    auto top1 = lib.matchSpectrum(query.data(), 3, 1, 1.6);
    REQUIRE(top1.size() == 1);
    REQUIRE(top1[0].id == QStringLiteral("same"));

    auto gated = lib.matchSpectrum(query.data(), 3, 10, 0.8);
    REQUIRE(gated.size() == 2);
}

TEST_CASE("D13 library JSON round-trip is lossless", "[spectral][library][d13]")
{
    exp_spectral::SpectralLibrary lib;
    exp_spectral::SpectralLibraryEntry entry =
        makeEntry(QStringLiteral("veg_940"), QStringLiteral("vegetation"), {0.1f, 0.3f, 0.6f, 0.4f});
    entry.wavelengthsNm = {500.0f, 660.0f, 850.0f, 1600.0f};
    entry.fwhmNm = {20.0f, 30.0f, 40.0f, 50.0f};
    REQUIRE(lib.addEntry(entry));

    const QJsonObject json = lib.toJson();
    QString parseError;
    const exp_spectral::SpectralLibrary restored = exp_spectral::SpectralLibrary::fromJson(json, &parseError);
    REQUIRE(parseError.isEmpty());
    REQUIRE(restored.size() == 1);
    const auto &back = restored.entries()[0];
    REQUIRE(back.id == entry.id);
    REQUIRE(back.name == entry.name);
    REQUIRE(back.materialClass == entry.materialClass);
    REQUIRE(back.source == entry.source);
    REQUIRE(back.spectrum == entry.spectrum);
    REQUIRE(back.wavelengthsNm == entry.wavelengthsNm);
    REQUIRE(back.fwhmNm == entry.fwhmNm);

    // File round-trip through save/load.
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("lib_d13.json"));
    QString fileError;
    REQUIRE(lib.saveToFile(path, &fileError));
    exp_spectral::SpectralLibrary loaded;
    REQUIRE(loaded.loadFromFile(path, &fileError));
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded.entries()[0].spectrum == entry.spectrum);
}

TEST_CASE("D13 library rejects malformed JSON with named errors", "[spectral][library][d13]")
{
    QString error;
    REQUIRE(exp_spectral::SpectralLibrary::fromJson(QJsonObject(), &error).size() == 0);
    REQUIRE(!error.isEmpty());

    QJsonObject bad;
    bad.insert(QStringLiteral("entries"), QJsonArray{QJsonValue(42)});
    error.clear();
    REQUIRE(exp_spectral::SpectralLibrary::fromJson(bad, &error).size() == 0);
    REQUIRE(!error.isEmpty());

    QTemporaryDir tmp;
    QString fileError;
    exp_spectral::SpectralLibrary missing;
    REQUIRE_FALSE(missing.loadFromFile(tmp.filePath(QStringLiteral("nope.json")), &fileError));
    REQUIRE(!fileError.isEmpty());
}

TEST_CASE("D13 Gaussian SRF resampling preserves constant spectra exactly", "[spectral][library][d13]")
{
    exp_spectral::SpectralLibrary lib;
    exp_spectral::SpectralLibraryEntry entry =
        makeEntry(QStringLiteral("constant"), QStringLiteral("invariant"), {});
    for (int i = 0; i < 161; ++i) // 400..2000 nm, 10 nm step
    {
        entry.spectrum.push_back(0.35f);
        entry.wavelengthsNm.push_back(400.0f + 10.0f * i);
        entry.fwhmNm.push_back(10.0f);
    }
    REQUIRE(lib.addEntry(entry));

    // Resample onto an arbitrary sensor grid: constant in → constant out
    // (SRF weights form a partition of unity over the window).
    exp_spectral::SpectralLibrary resampled;
    REQUIRE(lib.resampleToSensor({550.0f, 840.0f, 1610.0f}, {20.0f, 40.0f, 100.0f}, &resampled));
    REQUIRE(resampled.size() == 1);
    REQUIRE(resampled.entries()[0].spectrum.size() == 3);
    for (float value : resampled.entries()[0].spectrum)
        REQUIRE_THAT(value, Catch::Matchers::WithinAbs(0.35f, 1e-6f));
    REQUIRE(resampled.entries()[0].wavelengthsNm.front() == 550.0f);
    REQUIRE(resampled.entries()[0].id == QStringLiteral("constant"));

    // Entries without a wavelength grid cannot be resampled — named refusal.
    exp_spectral::SpectralLibrary gridless;
    REQUIRE(gridless.addEntry(makeEntry(QStringLiteral("bare"), QStringLiteral("x"), {1.0f, 2.0f})));
    exp_spectral::SpectralLibrary sink;
    REQUIRE_FALSE(gridless.resampleToSensor({550.0f}, {20.0f}, &sink));

    // Target bands outside the source coverage refuse (no NaN fills).
    exp_spectral::SpectralLibrary outside;
    REQUIRE_FALSE(lib.resampleToSensor({2500.0f}, {20.0f}, &outside));

    // Malformed target grids refuse at the seam.
    REQUIRE_FALSE(lib.resampleToSensor({}, {}, &sink));
    REQUIRE_FALSE(lib.resampleToSensor({550.0f}, {0.0f}, &sink));
    REQUIRE_FALSE(lib.resampleToSensor({550.0f}, {20.0f}, nullptr));
}
