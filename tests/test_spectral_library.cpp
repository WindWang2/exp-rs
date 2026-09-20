// test_spectral_library.cpp — spectral library domain round-trip
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
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

// ─── Consolidated retriever semantics (Spectral Intelligence 13.0) ─────────
// The D13 `exp_spectral::SpectralLibrary` retriever (src/core/spectral_library.*)
// was deleted in the library consolidation: `SpectralLibrary::Library` +
// `matchSpectrum` (this file's authority) is the single retriever, and these
// tests re-express the D13 oracle intent on it — analytical spectral-angle
// ground truth, degenerate-input ordering, and Gaussian-SRF invariance —
// rather than dropping the coverage.
TEST_CASE( "Consolidated SAM analytical ground truth from orthogonal geometry",
           "[spectral][library][consolidated]" )
{
    Library lib;
    Entry unitX;
    unitX.id = QStringLiteral( "unit_x" );
    unitX.name = QStringLiteral( "unit_x" );
    unitX.material = QStringLiteral( "axis" );
    unitX.source = QStringLiteral( "synthetic" );
    unitX.spectrum = { 1.0f, 0.0f, 0.0f };
    lib.entries.append( unitX );

    // Query [1,1,0] vs reference [1,0,0]: cos θ = 1/√2 → θ = π/4 = 45° exactly.
    const std::vector<float> query = { 1.0f, 1.0f, 0.0f };
    const auto matches = SpectralLibrary::matchSpectrum( query, lib );
    REQUIRE( matches.size() == 1 );
    REQUIRE( matches[0].entryIndex == 0 );
    REQUIRE( matches[0].angleDegrees == Catch::Approx( 45.0 ).margin( 1e-9 ) );

    // Identical spectra: zero angle.
    const auto same = SpectralLibrary::matchSpectrum( unitX.spectrum, lib );
    REQUIRE( same.size() == 1 );
    REQUIRE( same[0].angleDegrees == Catch::Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "Consolidated undefined angles sort last without crashing",
           "[spectral][library][consolidated]" )
{
    Library lib;
    Entry zero;
    zero.id = QStringLiteral( "flat" );
    zero.name = QStringLiteral( "flat" );
    zero.material = QStringLiteral( "void" );
    zero.source = QStringLiteral( "synthetic" );
    zero.spectrum = { 0.0f, 0.0f, 0.0f };
    lib.entries.append( zero );
    Entry good;
    good.id = QStringLiteral( "good" );
    good.name = QStringLiteral( "good" );
    good.material = QStringLiteral( "axis" );
    good.source = QStringLiteral( "synthetic" );
    good.spectrum = { 1.0f, 0.0f, 0.0f };
    lib.entries.append( good );

    const std::vector<float> query = { 0.2f, 0.3f, 0.4f };
    const auto matches = SpectralLibrary::matchSpectrum( query, lib );
    REQUIRE( matches.size() == 2 );
    // The zero-norm entry has an undefined angle: it sorts LAST (never first,
    // never NaN-poisoning the ranking) and the finite entry ranks first.
    REQUIRE( matches[0].name == QStringLiteral( "good" ) );
    REQUIRE( matches[1].name == QStringLiteral( "flat" ) );
    REQUIRE( std::isnan( matches[1].angleDegrees ) );
    REQUIRE( std::isfinite( matches[0].angleDegrees ) );
}

TEST_CASE( "Consolidated SAM ordering and band-count skip",
           "[spectral][library][consolidated]" )
{
    Library lib;
    auto addEntry = [&lib]( const QString &id, std::vector<float> spectrum ) {
        Entry e;
        e.id = id;
        e.name = id;
        e.material = QStringLiteral( "synthetic" );
        e.source = QStringLiteral( "synthetic" );
        e.spectrum = std::move( spectrum );
        lib.entries.append( e );
    };
    // Reference angles vs query [1,1,0]: exact (0), π/4, and π/2 (orthogonal).
    addEntry( QStringLiteral( "same" ), { 1.0f, 1.0f, 0.0f } );
    addEntry( QStringLiteral( "diag" ), { 1.0f, 0.0f, 0.0f } );
    addEntry( QStringLiteral( "ortho" ), { 1.0f, -1.0f, 0.0f } );
    // Incomparable geometry: 4 bands vs the 3-band query, no grids → skipped.
    addEntry( QStringLiteral( "wide" ), { 1.0f, 0.0f, 0.0f, 0.0f } );

    const std::vector<float> query = { 1.0f, 1.0f, 0.0f };
    const auto all = SpectralLibrary::matchSpectrum( query, lib );
    REQUIRE( all.size() == 3 ); // "wide" skipped
    REQUIRE( all[0].name == QStringLiteral( "same" ) );
    REQUIRE( all[0].angleDegrees == Catch::Approx( 0.0 ).margin( 1e-9 ) );
    REQUIRE( all[1].name == QStringLiteral( "diag" ) );
    REQUIRE( all[1].angleDegrees == Catch::Approx( 45.0 ).margin( 1e-9 ) );
    REQUIRE( all[2].name == QStringLiteral( "ortho" ) );
    REQUIRE( all[2].angleDegrees == Catch::Approx( 90.0 ).margin( 1e-9 ) );
}

TEST_CASE( "Consolidated Gaussian SRF resampling preserves constant spectra exactly",
           "[spectral][library][consolidated]" )
{
    // A 161-band constant library entry (400..2000 nm, 10 nm step).
    Library lib;
    lib.id = QStringLiteral( "constant-lib" );
    Entry entry;
    entry.id = QStringLiteral( "constant" );
    entry.name = QStringLiteral( "constant" );
    entry.material = QStringLiteral( "synthetic" );
    entry.source = QStringLiteral( "synthetic" );
    for ( int i = 0; i < 161; ++i )
    {
        entry.spectrum.push_back( 0.35f );
        entry.wavelengths.push_back( 400.0f + 10.0f * i );
        entry.fwhm.push_back( 10.0f );
    }
    lib.entries.append( entry );

    // Sensor with three arbitrary bands; resampleTo projects the library.
    QJsonObject sensor;
    sensor[QStringLiteral( "id" )] = QStringLiteral( "test-sensor" );
    sensor[QStringLiteral( "name" )] = QStringLiteral( "Test Sensor" );
    QJsonArray bands;
    for ( const auto &b : { std::make_pair( 550.0, 20.0 ), std::make_pair( 840.0, 40.0 ),
                            std::make_pair( 1610.0, 100.0 ) } )
    {
        QJsonObject band;
        band[QStringLiteral( "name" )] = QStringLiteral( "b" );
        band[QStringLiteral( "wavelengthNm" )] = b.first;
        band[QStringLiteral( "fwhmNm" )] = b.second;
        bands.append( band );
    }
    sensor[QStringLiteral( "bands" )] = bands;

    SpectralLibrary::SensorProfile profile;
    QString error;
    REQUIRE( SpectralLibrary::SensorProfile::fromJson( sensor, &profile, &error ) );
    REQUIRE( profile.isValid() );

    Library resampled;
    REQUIRE( lib.resampleTo( profile, &resampled, &error ) );
    REQUIRE( resampled.entries.size() == 1 );
    REQUIRE( resampled.entries[0].spectrum.size() == 3 );
    for ( float value : resampled.entries[0].spectrum )
        REQUIRE( value == Catch::Approx( 0.35f ).margin( 1e-6f ) );
    // Identity and provenance survive the projection.
    REQUIRE( resampled.entries[0].id == QStringLiteral( "constant" ) );
    REQUIRE( resampled.entries[0].tags.contains( QStringLiteral( "resampled" ) ) );
    REQUIRE( resampled.entries[0].tags.contains( QStringLiteral( "sensor:test-sensor" ) ) );

    // Entries without a wavelength grid cannot be resampled — named refusal.
    Library gridless;
    Entry bare;
    bare.id = QStringLiteral( "bare" );
    bare.name = QStringLiteral( "bare" );
    bare.material = QStringLiteral( "synthetic" );
    bare.source = QStringLiteral( "synthetic" );
    bare.spectrum = { 1.0f, 2.0f };
    gridless.entries.append( bare );
    Library sink;
    REQUIRE_FALSE( gridless.resampleTo( profile, &sink, &error ) );
    REQUIRE( !error.isEmpty() );
}
