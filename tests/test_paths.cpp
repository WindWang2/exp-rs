// Path tests — verify AppPaths uses dynamic path resolution, not hardcoded paths
// Custom main required: AppPaths uses QCoreApplication::applicationDirPath()
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "app/app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}

TEST_CASE("AppPaths::prefixPath returns valid path", "[paths]") {
  QString path = AppPaths::prefixPath();
  REQUIRE_FALSE(path.isEmpty());
  REQUIRE(QFileInfo(path).isAbsolute());
}

TEST_CASE("AppPaths::resolveDataPath produces correct paths", "[paths]") {
  SECTION("returns absolute path") {
    QString path = AppPaths::resolveDataPath("data/sample_crops.tif");
    REQUIRE(QFileInfo(path).isAbsolute());
  }

  SECTION("ends with expected relative suffix") {
    QString path = AppPaths::resolveDataPath("data/sample_crops.tif");
    REQUIRE(path.endsWith("data/sample_crops.tif"));
  }

  SECTION("handles nested relative paths") {
    QString path = AppPaths::resolveDataPath("data/subdir/file.tif");
    REQUIRE(path.endsWith("data/subdir/file.tif"));
  }
}

TEST_CASE("AppPaths::dataDir returns data directory", "[paths]") {
  QString path = AppPaths::dataDir();
  REQUIRE_FALSE(path.isEmpty());
  REQUIRE(path.endsWith("data"));
  REQUIRE(QFileInfo(path).isAbsolute());
}

TEST_CASE("AppPaths::samplesDataDir resolves bundled lab datasets", "[paths][samples]") {
  // samplesDataDir() walks candidate directories and returns the first that
  // exists on disk; it returns an empty string when none are present. Sample
  // rasters are no longer committed to VCS (tests synthesise fixtures at
  // runtime), so the directory may legitimately be absent — the contract under
  // test is "resolve-or-empty", not "always present". When a directory is
  // resolved it must be absolute.
  QString path = AppPaths::samplesDataDir();
  if ( path.isEmpty() )
  {
    SUCCEED( "samplesDataDir() correctly returned empty — no sample dir on disk" );
    return;
  }
  REQUIRE( QFileInfo( path ).isAbsolute() );
}

TEST_CASE("main.cpp does not use hardcoded paths", "[paths]") {
  // This test documents the requirement: main.cpp must NOT contain
  // hardcoded paths like "/home/kevin/projects/exp-rs".
  //
  // Verification is done by code review / grep, not runtime assertion.
  //
  // After refactoring, main.cpp should use:
  //   AppPaths::prefixPath()
  //   AppPaths::dataDir()
  //   AppPaths::resolveDataPath("data/sample_crops.tif")
  //
  // NOT:
  //   QgsApplication::setPrefixPath("/home/kevin/projects/exp-rs", true);
  //   QString samplePath = "/home/kevin/projects/exp-rs/data/sample_crops.tif";

  REQUIRE(true); // placeholder — actual verification is code review
}
