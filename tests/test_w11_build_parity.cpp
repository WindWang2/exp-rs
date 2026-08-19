// tests/test_w11_build_parity.cpp — regression for W11 build issues
// Covers 337 BUILD-4, 383, 394, 366 (doc/CMake parity).
// Not registered in CMake; orchestrator will register.
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QTextStream>
#include <QString>

static QString readFileText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream s(&f);
    return s.readAll();
}

TEST_CASE("install_deps.sh Ubuntu package typo is fixed", "[build][w11]")
{
    const QString txt = readFileText(QStringLiteral("scripts/install_deps.sh"));
    // Positive: correct name must exist
    REQUIRE(txt.contains(QStringLiteral("libqca-qt6-dev")));
    // Negative: doubled typo must not exist
    REQUIRE(!txt.contains(QStringLiteral("libqca-qt6-qt6-dev")));
    // Parity with ci.yml: qtkeychain must be present on Ubuntu branch
    REQUIRE(txt.contains(QStringLiteral("qtkeychain-qt6-dev")));
    // macOS must bundle qca/qtkeychain
    REQUIRE((txt.contains(QStringLiteral("qca qtkeychain")) || txt.contains(QStringLiteral("qca\n"))));
}

TEST_CASE("README Qt floor matches CMake 6.8", "[build][w11]")
{
    const QString cmake = readFileText(QStringLiteral("CMakeLists.txt"));
    REQUIRE(cmake.contains(QStringLiteral("find_package(Qt6 6.8")));
    const QString readme = readFileText(QStringLiteral("README.md"));
    REQUIRE(readme.contains(QStringLiteral("Qt 6.8+")));
    REQUIRE(!readme.contains(QStringLiteral("Qt 6.2+")));
}

TEST_CASE("QGIS_OUTPUT_DIRECTORY defined centrally", "[build][w11]")
{
    const QString cmake = readFileText(QStringLiteral("CMakeLists.txt"));
    REQUIRE(cmake.contains(QStringLiteral("QGIS_OUTPUT_DIRECTORY")));
    const QString plugins = readFileText(QStringLiteral("src/plugins/CMakeLists.txt"));
    // src/plugins must guard undefined var
    REQUIRE(plugins.contains(QStringLiteral("QGIS_OUTPUT_DIRECTORY")));
    REQUIRE(!plugins.contains(QStringLiteral("set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${QGIS_OUTPUT_DIRECTORY}")));
}

TEST_CASE("Vendor modes guard missing prefix", "[build][w11]")
{
    const QString cmake = readFileText(QStringLiteral("CMakeLists.txt"));
    REQUIRE(cmake.contains(QStringLiteral("EXISTS \"${SICNU_VENDOR_PREFIX}/lib/cmake/gdal/GDALConfig.cmake\"")));
    REQUIRE(cmake.contains(QStringLiteral("SICNU_HAS_OTB stays FALSE")));
}

TEST_CASE("OTB pixel info test grammar uses coordx/coordy", "[build][w11]")
{
    const QString test = readFileText(QStringLiteral("tests/test_otb_info_upgrades.cpp"));
    REQUIRE(test.contains(QStringLiteral("-coordx")));
    REQUIRE(test.contains(QStringLiteral("-coordy")));
    // the standalone "-coord" token check must be negative
    REQUIRE((test.contains(QStringLiteral("indexOf( \"-coord\" ) < 0")) || test.contains(QStringLiteral("indexOf(\"-coord\") < 0"))));
}
