// test_r29_t04_completion_ledger_loop.cpp — D5 (R29-T04).
//
// The full teaching persistence loop over the REAL dock, REAL grading
// engine (OutputVerifier over a real GTiff + real rules), REAL ledger:
//
//   lab A verified pass → durable per-lab evidence + ledger entry
//   → course home shows A completed and module-2 prerequisite unlocked
//   → switch to lab B, quit, restart → A still completed, B restored
//   → artifact bytes drift → the ledger entry demotes honestly (no stale
//     completion), the prerequisite chain re-locks.
//
// A minimal offline course (2 modules, prerequisite chain) is seeded under
// a temp SICNU_DATA_DIR; nothing here grades through a fake verdict.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QStandardPaths>
#include <QTextEdit>

#include "course_home_page.h"
#include "guided_lab_workspace.h"
#include "lab_cockpit_dock.h"

#include "teaching/lab_completion_ledger.h"

#include <gdal.h>

#include <filesystem>
#include <optional>
#include <fstream>

using namespace sicnu::app::teaching;
namespace fs = std::filesystem;

namespace
{

QApplication *ensureApp()
{
    if ( !QApplication::instance() )
    {
        static int argc = 1;
        static char name[] = "test_r29_t04_ledger_loop";
        static char *argv[] = { name, nullptr };
        QStandardPaths::setTestModeEnabled( true );
        static QApplication app( argc, argv );
        return &app;
    }
    return static_cast<QApplication *>( QApplication::instance() );
}

struct CourseRoot
{
    fs::path root;
    fs::path artifactPath;

    explicit CourseRoot( const char *tag )
    {
        root = fs::temp_directory_path() / ( std::string( "r29_t04_ledger_" ) + tag );
        fs::remove_all( root );
        fs::create_directories( root / "curriculum" );
        fs::create_directories( root / "labs" / "grading" );

        write( root / "curriculum" / "undergraduate_rs.curriculum.json", R"({
  "schema": "sicnu.curriculum/1",
  "id": "undergraduate_rs",
  "title": "R29 T04 loop course",
  "title_zh": "R29 T04 闭环课程",
  "modules": [
    {
      "id": "m01", "index": 1, "title": "M1", "title_zh": "模块一",
      "prerequisite_modules": [],
      "labs": [ { "lab_id": "lab_a_complete", "role": "core" } ]
    },
    {
      "id": "m02", "index": 2, "title": "M2", "title_zh": "模块二",
      "prerequisite_modules": [ "m01" ],
      "labs": [ { "lab_id": "lab_b_next", "role": "core" } ]
    }
  ]
}
)" );

        write( root / "labs" / "lab_a_complete.lab.json", R"({
  "spec_version": 2,
  "id": "lab_a_complete",
  "title": "Lab A",
  "title_zh": "实验A",
  "objective": "produce a constant raster",
  "grading_rules": "data/labs/grading/lab_a_complete.rules.json",
  "steps": [
    { "id": "a_step1", "title": "Produce", "title_zh": "产出",
      "teaching_note": "constant raster explanation A",
      "operator_id": "rs:threshold_raster",
      "params": { "input": "x.tif", "output": "out.tif", "threshold": 0.5 } }
  ]
}
)" );

        write( root / "labs" / "lab_b_next.lab.json", R"({
  "spec_version": 2,
  "id": "lab_b_next",
  "title": "Lab B",
  "title_zh": "实验B",
  "objective": "next lab",
  "steps": [
    { "id": "b_step1", "title": "Inspect", "title_zh": "检查",
      "teaching_note": "constant raster explanation B" }
  ]
}
)" );

        // The graded truth: interior constant 1.0 satisfies mean_sigma.
        write( root / "labs" / "grading" / "lab_a_complete.rules.json", R"({
  "schema_version": "sicnu.lab.rules/1",
  "lab_id": "lab_a_complete",
  "title": "constant one probe",
  "artifact": { "kind": "raster" },
  "passing_score": 60,
  "assertions": [
    {
      "id": "stats.mean_one",
      "kind": "mean_sigma",
      "weight": 100,
      "severity": "normal",
      "params": { "band": 1, "mean": 1.0, "mean_tolerance": 1e-6,
                  "sigma_min": 0.0, "sigma_max": 1e-6, "sigma_tolerance": 0.0 }
    }
  ]
}
)" );

        artifactPath = root / "lab_a_artifact.tif";
        writeConstantRaster( artifactPath, 1.0f );
    }

    static void write( const fs::path &path, const std::string &bytes )
    {
        std::ofstream out( path, std::ios::binary );
        out << bytes;
    }

    static void writeConstantRaster( const fs::path &path, float value )
    {
        // This fixture drives a plain QApplication (no QgsApplication), so
        // GDAL's driver registry is not auto-registered anywhere else.
        GDALAllRegister();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDatasetH ds = GDALCreate( driver, path.string().c_str(), 8, 8, 1, GDT_Float32,
                                      nullptr );
        REQUIRE( ds != nullptr );
        double geoTransform[6] = { 500000.0, 1.0, 0.0, 4000000.0, 0.0, -1.0 };
        GDALSetGeoTransform( ds, geoTransform );
        GDALSetProjection( ds, "EPSG:32648" );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        const float row[8] = { value, value, value, value, value, value, value, value };
        for ( int y = 0; y < 8; ++y )
            GDALRasterIO( band, GF_Write, 0, y, 8, 1, const_cast<float *>( row ), 8, 1,
                          GDT_Float32, 0, 0 );
        GDALClose( ds );
    }
};

/// Fresh AppData per case; the SAME AppData is kept across the docks inside
/// one case (that IS the restart under test).
void resetSessionDir( const char *tag )
{
    QCoreApplication::setApplicationName( QString::fromUtf8( tag ) );
    const QString root = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    fs::remove_all( root.toStdString() );
}

std::optional<sicnu::teaching::CourseLabCard> labCard( LabCockpitDock *dock, const std::string &labId )
{
    const auto vm = dock->findChild<CourseHomePage *>()->viewModel();
    for ( const auto &mod : vm.modules )
        for ( const auto &lab : mod.labs )
            if ( lab.labId == labId )
                return lab;
    return std::nullopt;
}

GuidedLabWorkspace *workspaceOf( LabCockpitDock *dock )
{
    return dock->findChild<GuidedLabWorkspace *>();
}

QTextEdit *feedbackViewOf( LabCockpitDock *dock )
{
    return dock->findChild<QTextEdit *>( QStringLiteral( "labWorkspaceFeedbackView" ) );
}

} // namespace

TEST_CASE( "D5: verified pass closes the ledger→home loop and survives restart",
           "[r29_t04][teaching][ledger][loop]" )
{
    ensureApp();
    resetSessionDir( "r29_t04_ledger_loop" );
    CourseRoot course( "loop" );

    struct EnvGuard
    {
        ~EnvGuard() { qputenv( "SICNU_DATA_DIR", QByteArrayLiteral( "" ) ); }
    } envGuard;
    qputenv( "SICNU_DATA_DIR", QString::fromStdString( course.root.string() ).toUtf8() );

    // ── Phase 1: before any completion, module 2 is prerequisite-locked.
    {
        LabCockpitDock dock( nullptr );
        const auto cardB = labCard( &dock, "lab_b_next" );
        REQUIRE( cardB.has_value() );
        CHECK( cardB->status == sicnu::teaching::LabUiStatus::Unavailable );
        REQUIRE_FALSE( cardB->blockersZh.empty() );
    }

    // ── Phase 2: lab A graded PASS through the real engine → durable
    // evidence + ledger entry + refreshed home.
    {
        LabCockpitDock dock( nullptr );
        dock.openLab( QStringLiteral( "m01" ), QStringLiteral( "lab_a_complete" ) );
        auto *ws = workspaceOf( &dock );
        REQUIRE( ws != nullptr );
        REQUIRE_FALSE( ws->timeline().steps.empty() );
        ws->setArtifactPath( QString::fromStdString( course.artifactPath.string() ) );
        emit ws->validateRequested();

        const QString feedback = feedbackViewOf( &dock )->toPlainText();
        INFO( feedback.toStdString() );
        REQUIRE( feedback.contains( QStringLiteral( "Completion recorded with verified evidence" ) ) );

        const auto cardA = labCard( &dock, "lab_a_complete" );
        REQUIRE( cardA.has_value() );
        CHECK( cardA->status == sicnu::teaching::LabUiStatus::Completed );
        const auto cardB2 = labCard( &dock, "lab_b_next" );
        REQUIRE( cardB2.has_value() );
        CHECK( cardB2->status != sicnu::teaching::LabUiStatus::Unavailable );
        CHECK( cardB2->blockersZh.empty() );

        // Durable per-lab evidence exists beside the ledger.
        const QString appData =
          QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
        REQUIRE( fs::exists( ( fs::path( appData.toStdString() ) / "teaching"
                              / "lab_completion_ledger.json" ) ) );
        REQUIRE( fs::exists( fs::path( appData.toStdString() ) / "teaching" / "sessions"
                             / "lab_a_complete" ) );

        // Switch to lab B before "restart": B's session is what reopens.
        dock.openLab( QStringLiteral( "m02" ), QStringLiteral( "lab_b_next" ) );
    }

    // ── Phase 3: restart. A stays completed (evidence re-verified), B's
    // workspace is restored as the current lab.
    {
        LabCockpitDock dock( nullptr );
        const auto cardA = labCard( &dock, "lab_a_complete" );
        REQUIRE( cardA.has_value() );
        CHECK( cardA->status == sicnu::teaching::LabUiStatus::Completed );
        auto *ws = workspaceOf( &dock );
        REQUIRE( ws != nullptr );
        CHECK( ws->timeline().labId == "lab_b_next" );
    }

    // ── Phase 4: the artifact bytes drift. The recorded verdict can no
    // longer be re-derived from the evidence: the ledger entry demotes and
    // the prerequisite chain honestly re-locks. No stale completion.
    CourseRoot::writeConstantRaster( course.artifactPath, 2.0f ); // drifted bytes
    {
        LabCockpitDock dock( nullptr );
        const auto cardA = labCard( &dock, "lab_a_complete" );
        REQUIRE( cardA.has_value() );
        CHECK( cardA->status != sicnu::teaching::LabUiStatus::Completed );
        const auto cardB = labCard( &dock, "lab_b_next" );
        REQUIRE( cardB.has_value() );
        CHECK( cardB->status == sicnu::teaching::LabUiStatus::Unavailable );

        // The ledger itself (loaded verified) carries the demotion issues.
        const QString appData =
          QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
        const auto ledger = sicnu::teaching::LabCompletionLedger::loadVerified(
          ( fs::path( appData.toStdString() ) / "teaching" / "lab_completion_ledger.json" )
            .string() );
        CHECK( ledger.completed.empty() );
        CHECK_FALSE( ledger.issuesZh.empty() );
    }
}

TEST_CASE( "D5: fail and missing-artifact verdicts never record completion",
           "[r29_t04][teaching][ledger][nopass]" )
{
    ensureApp();
    resetSessionDir( "r29_t04_ledger_nopass" );
    CourseRoot course( "nopass" );

    struct EnvGuard
    {
        ~EnvGuard() { qputenv( "SICNU_DATA_DIR", QByteArrayLiteral( "" ) ); }
    } envGuard;
    qputenv( "SICNU_DATA_DIR", QString::fromStdString( course.root.string() ).toUtf8() );

    LabCockpitDock dock( nullptr );
    dock.openLab( QStringLiteral( "m01" ), QStringLiteral( "lab_a_complete" ) );
    auto *ws = workspaceOf( &dock );
    REQUIRE( ws != nullptr );

    SECTION( "wrong values → fail → no ledger, no completion" )
    {
        const fs::path wrong = course.root / "wrong.tif";
        CourseRoot::writeConstantRaster( wrong, 9.0f ); // fails mean=1.0
        ws->setArtifactPath( QString::fromStdString( wrong.string() ) );
        emit ws->validateRequested();
        const QString feedback = feedbackViewOf( &dock )->toPlainText();
        INFO( feedback.toStdString() );
        // A graded FAIL projects a verdict (and its deductions) — but never
        // a completion recording.
        CHECK_FALSE( feedback.isEmpty() );
        CHECK_FALSE( feedback.contains( QStringLiteral( "Completion recorded" ) ) );
        const auto cardA = labCard( &dock, "lab_a_complete" );
        REQUIRE( cardA.has_value() );
        CHECK( cardA->status != sicnu::teaching::LabUiStatus::Completed );
    }

    SECTION( "missing artifact file → no stamp, no completion" )
    {
        ws->setArtifactPath(
          QString::fromStdString( ( course.root / "gone.tif" ).string() ) );
        emit ws->validateRequested();
        const QString feedback = feedbackViewOf( &dock )->toPlainText();
        CHECK_FALSE( feedback.contains( QStringLiteral( "Completion recorded" ) ) );
        const auto cardA = labCard( &dock, "lab_a_complete" );
        REQUIRE( cardA.has_value() );
        CHECK( cardA->status != sicnu::teaching::LabUiStatus::Completed );
    }

    const QString appData = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    CHECK_FALSE( fs::exists( fs::path( appData.toStdString() ) / "teaching"
                             / "lab_completion_ledger.json" ) );
}
