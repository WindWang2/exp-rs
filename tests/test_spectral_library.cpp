// test_spectral_library.cpp — spectral library domain round-trip
#include <catch2/catch_test_macros.hpp>

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
