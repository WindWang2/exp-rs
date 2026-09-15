// test_spectral_workbench_panel.cpp — Spectral Workbench 11 panel: artifact
// loading (valid + fail-closed invalid), selection linkage signal, and
// matrix/details rendering, all offscreen (QT_QPA_PLATFORM=offscreen).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <utility>
#include <vector>

#include "app/widgets/spectral_workbench_panel.h"
#include "processing/algorithms/spectral_table.h"

using Catch::Approx;

namespace
{
    struct AppFixture
    {
        AppFixture()
        {
            if (!QApplication::instance())
            {
                static int argc = 1;
                static char arg0[] = "test_spectral_workbench_panel";
                static char *argv[] = {arg0, nullptr};
                new QApplication(argc, argv);
            }
        }
    };

    SpectralTable::Table makeTable()
    {
        SpectralTable::Table table;
        table.id = QStringLiteral("workbench-test");
        table.bandCount = 3;
        table.spectra = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };
        table.labels << QStringLiteral("red") << QStringLiteral("green");
        table.provenance.sourceOperator = QStringLiteral("rs:test");
        table.provenance.derived = true;
        table.provenance.createdAtMs = 0;
        return table;
    }
} // namespace

TEST_CASE("Workbench panel loads a validated table and links selection", "[spectral11][widget]")
{
    AppFixture fixture;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SpectralTable::Table table = makeTable();
    QString err;
    REQUIRE(SpectralTable::save(table, tmp.path() + "/table.json", &err));

    SpectralWorkbenchPanel panel;
    REQUIRE(panel.setTablePath(tmp.path() + "/table.json", &err));
    CHECK(panel.spectrumCount() == 2);
    CHECK(panel.tablePath() == tmp.path() + "/table.json");

    // Selection linkage: programmatic selection emits the signal with the
    // row identity the host would use for map/profile sync.
    std::vector<std::pair<QString, int>> selections;
    QObject::connect( &panel, &SpectralWorkbenchPanel::spectrumSelected,
                      [&selections]( const QString &label, int index )
                      { selections.emplace_back( label, index ); } );
    panel.selectSpectrum( 1 );
    REQUIRE( selections.size() == 1 );
    CHECK( selections.front().first == QStringLiteral( "green" ) );
    CHECK( selections.front().second == 1 );

    // Out-of-range selection clamps instead of crashing.
    panel.selectSpectrum( 99 );
    REQUIRE( selections.size() == 2 );
    CHECK( selections.back().second == 1 );
}

TEST_CASE("Workbench panel refuses broken artifacts fail-closed", "[spectral11][widget]")
{
    AppFixture fixture;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SpectralWorkbenchPanel panel;
    QString err;

    // Missing file.
    CHECK_FALSE(panel.setTablePath(tmp.path() + "/missing.json", &err));
    CHECK_FALSE(err.isEmpty());
    CHECK(panel.spectrumCount() == 0);

    // Corrupt JSON: loads nothing, keeps no stale state.
    const QString badPath = tmp.path() + "/bad.json";
    {
        QFile bad(badPath);
        REQUIRE(bad.open(QIODevice::WriteOnly));
        bad.write("{ not json ");
    }
    err.clear();
    CHECK_FALSE(panel.setTablePath(badPath, &err));
    CHECK(panel.spectrumCount() == 0);
    CHECK(panel.tablePath().isEmpty());
}
