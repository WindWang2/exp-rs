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
