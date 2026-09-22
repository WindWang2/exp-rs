// test_lab_source.cpp — lab directory scan + lab-registry resolution
#include <catch2/catch_test_macros.hpp>

#include "recipes/lab_source.h"

#include <filesystem>
#include <fstream>

using namespace sicnu::recipes;
namespace fs = std::filesystem;

namespace {

void write( const fs::path &path, const std::string &content )
{
  fs::create_directories( path.parent_path() );
  std::ofstream out( path, std::ios::binary );
  out << content;
}

const char *kSimpleLab = R"({
  "spec_version": 2, "id": "lab01_image_enhancement", "title": "Enhancement",
  "title_zh": "增强", "objective": "o",
  "steps": [{"title": "Stretch", "title_zh": "拉伸", "description_zh": "…",
             "operator_id": "rs:contrast_stretch", "params": {"input": "data/x.tif"}}]
})";

const char *kWrapper = R"({
  "spec_version": 2, "id": "lab12_sar_processing", "title": "SAR Processing",
  "title_zh": "SAR", "objective": "wrapper"
})";

const char *kD3 = R"({
  "schema": "sicnu.labspec.v1", "id": "lab9_sar_processing", "version": 1,
  "title": "SAR 实验", "theme": "sar",
  "objectives": ["o"], "principles": [], "data": {"spec_ref": "data/spec.json"},
  "pipeline": {"ref": "p.json"}, "operators": [],
  "steps": [{"id": "s1", "title": "定标", "operator_id": "rs:sar_calibrate"}],
  "grading_ref": {}, "expected_results": [], "questions": []
})";

} // namespace

TEST_CASE( "Directory scan loads D2 labs in canonical order", "[labsrc]" )
{
  const fs::path dir = fs::temp_directory_path() / "labsrc_plain";
  fs::remove_all( dir );
  write( dir / "lab01_image_enhancement.lab.json", kSimpleLab );

  std::vector<LabDocumentError> errors;
  const auto entries = loadLabDirectory( dir.string(), "", errors );
  REQUIRE( errors.empty() );
  REQUIRE( entries.size() == 1 );
  REQUIRE( entries[0].canonicalId == "lab01_image_enhancement" );
  REQUIRE( entries[0].document.steps.size() == 1 );
  REQUIRE( !entries[0].resolvedViaRegistry );
}

TEST_CASE( "Registry resolves step-less wrappers to their D3 source", "[labsrc]" )
{
  const fs::path root = fs::temp_directory_path() / "labsrc_registry";
  fs::remove_all( root );
  const fs::path labs = root / "data" / "labs";

  write( labs / "lab12_sar_processing.lab.json", kWrapper );
  write( labs / "lab9_sar_processing.labspec.json", kD3 );
  write( labs / "lab-registry.json", R"({
    "schema": "sicnu.lab-registry/1",
    "canonical": {
      "lab12_sar_processing": {
        "source": "data/labs/lab9_sar_processing.labspec.json",
        "aliases": ["lab9_sar_processing"]
      }
    }
  })" );

  std::vector<LabDocumentError> errors;
  const auto entries =
    loadLabDirectory( labs.string(), ( labs / "lab-registry.json" ).string(), errors );
  REQUIRE( errors.empty() );

  // Exactly ONE entry: the D3 file is consumed by the wrapper, not listed twice.
  REQUIRE( entries.size() == 1 );
  const LabSourceEntry &e = entries[0];
  REQUIRE( e.canonicalId == "lab12_sar_processing" );
  REQUIRE( e.resolvedViaRegistry );
  REQUIRE( e.document.steps.size() == 1 );
  REQUIRE( e.document.steps[0].operatorId == "rs:sar_calibrate" );
  // Wrapper metadata (canonical id + zh title) wins; D3 steps survive.
  REQUIRE( e.document.id == "lab12_sar_processing" );
  REQUIRE( e.document.titleZh == "SAR" );
  REQUIRE( e.document.sourcePath.find( "lab9_sar_processing.labspec.json" ) !=
           std::string::npos );
}

TEST_CASE( "Step-less wrapper without registry stays an explicit gap", "[labsrc]" )
{
  const fs::path dir = fs::temp_directory_path() / "labsrc_noreg";
  fs::remove_all( dir );
  write( dir / "lab12_sar_processing.lab.json", kWrapper );

  std::vector<LabDocumentError> errors;
  const auto entries = loadLabDirectory( dir.string(), "", errors );
  REQUIRE( entries.size() == 1 );
  REQUIRE( entries[0].document.steps.empty() );
  REQUIRE( !entries[0].resolvedViaRegistry );
}

TEST_CASE( "Standalone D3 file not in registry compiles under its own id", "[labsrc]" )
{
  const fs::path dir = fs::temp_directory_path() / "labsrc_d3solo";
  fs::remove_all( dir );
  write( dir / "lab9_sar_processing.labspec.json", kD3 );

  std::vector<LabDocumentError> errors;
  const auto entries = loadLabDirectory( dir.string(), "", errors );
  REQUIRE( entries.size() == 1 );
  REQUIRE( entries[0].canonicalId == "lab9_sar_processing" );
}

TEST_CASE( "Unreadable/invalid files are typed errors, not aborts", "[labsrc]" )
{
  const fs::path dir = fs::temp_directory_path() / "labsrc_bad";
  fs::remove_all( dir );
  write( dir / "lab01_image_enhancement.lab.json", kSimpleLab );
  write( dir / "bad.lab.json", "{ not json" );

  std::vector<LabDocumentError> errors;
  const auto entries = loadLabDirectory( dir.string(), "", errors );
  REQUIRE( entries.size() == 1 );
  REQUIRE( errors.size() == 1 );
  REQUIRE( errors[0].path.find( "bad.lab.json" ) != std::string::npos );
}

TEST_CASE( "Missing directory yields empty scan without crash", "[labsrc]" )
{
  std::vector<LabDocumentError> errors;
  const auto entries = loadLabDirectory( "/nonexistent/dir", "", errors );
  REQUIRE( entries.empty() );
}
