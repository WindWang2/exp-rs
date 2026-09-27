// test_model_manifest_r4.cpp — Track 15 WP-A: the P1-8 manifest-validation
// error contract (ADR 0130 model-runtime platform; the non-security leftover
// #1334 explicitly deferred). Two contract axes, both derived from the
// catalog's own documented semantics:
//   1. Every rejection class reports an actionable quadruple — manifest path,
//      field name, expected domain, actual value — assembled at ONE exit
//      (markInvalid), never re-spliced per call site.
//   2. Identity checks precede content checks: name (entry layer), then
//      manifest_version (+shape cross-check), then vocabulary/bounds. The
//      order is contract, not implementation accident — regression-tested by
//      injecting an identity defect AND a content defect together and
//      requiring the identity finding first.
// Truth sources: the constructed manifest bytes themselves plus the catalog's
// documented vocabularies (not runtime code paths). Every case is a complete
// manifest literal — no textual splicing, no reliance on duplicate-key rules.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
using sicnu::operators::ModelCatalog;

const std::string kManifestPath = "/models/r4-probe/model.json";

/// Minimal WELL-FORMED v3 manifest (no declared manifest_version: the shape
/// alone is valid; version-declaring cases add it explicitly).
const char *kBase = R"({
    "name": "r4-probe", "task": "segmentation", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster" } ],
    "output": { "type": "classes" },
    "artifact": { "path": "weights.onnx" }
})";

std::vector<std::string> validate( const std::string &json )
{
    const auto issues = ModelCatalog::instance().validateManifestJson( json, kManifestPath );
    for ( const auto &issue : issues )
        std::cout << "[ISSUE] " << issue << "\n";
    return issues;
}

/// One issue must carry the whole quadruple: manifest path + field + expected
/// + actual (free text not pinned — the contract is the quadruple).
void expectQuadruple( const std::string &json, const std::string &field,
                      const std::string &expected, const std::string &actual )
{
    const auto issues = validate( json );
    REQUIRE_FALSE( issues.empty() );
    for ( const auto &issue : issues )
    {
        INFO( "issue: " << issue );
        const bool full = issue.find( kManifestPath ) != std::string::npos
                          && issue.find( field ) != std::string::npos
                          && issue.find( expected ) != std::string::npos
                          && issue.find( actual ) != std::string::npos;
        if ( full )
            return;
    }
    FAIL( "no issue carried the quadruple (path + '" << field << "' + '" << expected << "' + '"
                                                     << actual << "')" );
}

bool anyIssueCarries( const std::vector<std::string> &issues, const std::string &path,
                      const std::string &field, const std::string &expected,
                      const std::string &actual )
{
    for ( const auto &issue : issues )
        if ( issue.find( path ) != std::string::npos && issue.find( field ) != std::string::npos
             && issue.find( expected ) != std::string::npos
             && issue.find( actual ) != std::string::npos )
            return true;
    return false;
}

/// First "; "-segment of the first issue (findings are joined in contract
/// order — identity before content).
std::string firstFinding( const std::vector<std::string> &issues )
{
    REQUIRE_FALSE( issues.empty() );
    const std::string &first = issues.front();
    const auto split = first.find( "; " );
    return split == std::string::npos ? first : first.substr( 0, split );
}
} // namespace

// ── 1. The rejection quadruple: 14 classes ──────────────────────────────────

TEST_CASE( "P1-8: manifest_version range/type rejections carry path, field, domain and actual value",
           "[models][manifest][r4]" )
{
    expectQuadruple( R"({
        "name": "r4-mv0", "task": "t", "framework": "onnx", "manifest_version": 0,
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" } })",
                     "manifest_version", "1..6", " 0" );
    expectQuadruple( R"({
        "name": "r4-mv9", "task": "t", "framework": "onnx", "manifest_version": 9,
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" } })",
                     "manifest_version", "1..6", "9" );
    // Non-integral declared value: the ACTUAL token (5.5) must surface, never
    // a silently-defaulted 0.
    expectQuadruple( R"({
        "name": "r4-mvf", "task": "t", "framework": "onnx", "manifest_version": 5.5,
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" } })",
                     "manifest_version", "1..6", "5.5" );
    // Wrong type: the declared token is part of the finding.
    expectQuadruple( R"({
        "name": "r4-mvs", "task": "t", "framework": "onnx", "manifest_version": "four",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" } })",
                     "manifest_version", "integer", "four" );
}

TEST_CASE( "P1-8: the manifest_version shape cross-check names declared and actual shape",
           "[models][manifest][r4]" )
{
    // 'inputs' array is the v3 shape; declaring 2 is a contract lie.
    const auto issues = validate( R"({
        "name": "r4-shape", "task": "t", "framework": "onnx", "manifest_version": 2,
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" } })" );
    REQUIRE_FALSE( issues.empty() );
    REQUIRE( anyIssueCarries( issues, kManifestPath, "manifest_version", "2",
                              "shape is version 3" ) );
}

TEST_CASE( "P1-8: unknown keys report the full section path and the contract hint",
           "[models][manifest][r4]" )
{
    expectQuadruple( R"({
        "name": "r4-uk1", "task": "t", "framework": "onnx", "lucky_number": 7,
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" } })",
                     "unknown key 'lucky_number'", "manifest contract", "lucky_number" );
    expectQuadruple( R"({
        "name": "r4-uk2", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "preprocess": { "lucky": true } })",
                     "unknown key 'preprocess.lucky'", "manifest contract", "preprocess.lucky" );
}

TEST_CASE( "P1-8: output vocabulary rejections name field, supported set and actual token",
           "[models][manifest][r4]" )
{
    const auto formatIssues = validate( R"({
        "name": "r4-fmt", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ],
        "output": { "type": "classes", "format": "postcards" } })" );
    REQUIRE(
        anyIssueCarries( formatIssues, kManifestPath, "output.format", "postcards",
                         "probability, labels, mask, confidence" ) );

    const auto uncertaintyIssues = validate( R"({
        "name": "r4-unc", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ],
        "output": { "type": "classes", "uncertainty": "crystal_ball" } })" );
    REQUIRE( anyIssueCarries( uncertaintyIssues, kManifestPath, "output.uncertainty",
                              "crystal_ball", "none, entropy, margin" ) );
}

TEST_CASE( "P1-8: class_mapping type breaches are refused, never silently dropped or defaulted",
           "[models][manifest][r4]" )
{
    // A non-array class_mapping used to be silently DROPPED (declared knob,
    // zero effect — the #646 class). It must be a typed refusal naming the
    // actual declared type.
    expectQuadruple( R"({
        "name": "r4-cms", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "postprocess": { "class_mapping": "0,1,2" } })",
                     "postprocess.class_mapping", "array", "string" );

    // A non-integer element used to collapse to -1 and report a misleading
    // "element is -1" negative-value finding; the declared token must surface.
    const auto issues = validate( R"({
        "name": "r4-cme", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "postprocess": { "class_mapping": [ 0, "two" ] } })" );
    REQUIRE_FALSE( issues.empty() );
    REQUIRE( anyIssueCarries( issues, kManifestPath, "postprocess.class_mapping", "element 1",
                              "two" ) );
}

TEST_CASE( "P1-8: class_mapping value rejections carry element index and value",
           "[models][manifest][r4]" )
{
    expectQuadruple( R"({
        "name": "r4-cmn", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "postprocess": { "class_mapping": [ -3 ] } })",
                     "postprocess.class_mapping", ">= 0", "-3" );
    expectQuadruple( R"({
        "name": "r4-cmc", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "postprocess": { "class_mapping": [ 1, 1 ] } })",
                     "postprocess.class_mapping", "same", "product class 1" );
    expectQuadruple( R"({
        "name": "r4-cmb", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "postprocess": { "class_mapping": [ 70000 ] } })",
                     "postprocess.class_mapping", "65534", "70000" );
}

TEST_CASE( "P1-8: bound rejections carry the field, the bound and the declared value",
           "[models][manifest][r4]" )
{
    expectQuadruple( R"({
        "name": "r4-pad", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "preprocess": { "pad": 5000 } })",
                     "preprocess.pad", "1024", "5000" );
    expectQuadruple( R"({
        "name": "r4-tile", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "tiling": { "tile_size": 40000 } })",
                     "tiling.tile_size", "32768", "40000" );
}

TEST_CASE( "P1-8: provider rejections name the exact missing provider field",
           "[models][manifest][r4]" )
{
    const auto httpIssues = validate( R"({
        "name": "r4-http", "task": "t", "framework": "http",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "runtime": { "provider": { } } })" );
    REQUIRE( anyIssueCarries( httpIssues, kManifestPath, "runtime.provider.url", "http",
                              "url" ) );

    const auto pythonIssues = validate( R"({
        "name": "r4-py", "task": "t", "framework": "python",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "runtime": { "provider": { } } })" );
    REQUIRE( anyIssueCarries( pythonIssues, kManifestPath, "runtime.provider.worker_script",
                              "python", "worker_script" ) );
}

TEST_CASE( "P1-8: detection contract bounds carry field and range",
           "[models][manifest][r4]" )
{
    const auto issues = validate( R"({
        "name": "r4-det", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ],
        "output": { "type": "classes", "detection": { "layout": "xywh_objectness",
                                                      "conf_threshold": 4.5 } } })" );
    REQUIRE(
        anyIssueCarries( issues, kManifestPath, "output.detection.conf_threshold", "[0, 1]",
                         "4.5" ) );
}

TEST_CASE( "P1-8: conflict rejections name both conflicting knobs",
           "[models][manifest][r4]" )
{
    // Calibration temperature on the (implicit) probability stack: a declared
    // knob that would do nothing — must be a refusal naming both sides.
    const auto issues = validate( R"({
        "name": "r4-cal", "task": "t", "framework": "onnx",
        "inputs": [ { "name": "x" } ], "output": { "type": "classes" },
        "postprocess": { "calibration_temperature": 2.0 } })" );
    REQUIRE( anyIssueCarries( issues, kManifestPath, "postprocess.calibration_temperature",
                              "output.format", "labels/mask/confidence" ) );
}

TEST_CASE( "P1-8: a manifest without a name is reported first, with the path",
           "[models][manifest][r4]" )
{
    const auto issues = validate( R"({ "task": "segmentation", "unknown_knob": true })" );
    REQUIRE_FALSE( issues.empty() );
    // Name (identity) is the FIRST finding and carries the manifest path.
    REQUIRE( issues.front().find( kManifestPath ) == 0 );
    REQUIRE( issues.front().find( "no 'name'" ) != std::string::npos );
}

// ── 2. Order is contract: identity before content ───────────────────────────

TEST_CASE( "P1-8 order: manifest_version outranks a content defect declared beside it",
           "[models][manifest][r4][order]" )
{
    // Identity defect (version 99) + content defect (bad aux entry) in ONE
    // manifest: the readinessReason must OPEN with the identity finding.
    const auto issues = validate( R"({
        "name": "r4-order-1", "task": "segmentation", "framework": "onnx",
        "manifest_version": 99,
        "inputs": [ { "name": "x" } ],
        "output": { "type": "classes" },
        "package": { "aux_files": [ { "path": "" } ] },
        "artifact": { "path": "weights.onnx" }
    })" );
    REQUIRE_FALSE( issues.empty() );
    const std::string first = firstFinding( issues );
    INFO( "first finding: " << first );
    REQUIRE( first.find( "manifest_version 99" ) != std::string::npos );
    REQUIRE( first.find( "aux_files" ) == std::string::npos );
}

TEST_CASE( "P1-8 order: the shape cross-check outranks vocabulary defects",
           "[models][manifest][r4][order]" )
{
    // Identity/shape defect (declared 2, v3 shape) + vocabulary defect
    // (output.format) together: identity segment first.
    const auto issues = validate( R"({
        "name": "r4-order-2", "task": "segmentation", "framework": "onnx",
        "manifest_version": 2,
        "inputs": [ { "name": "x" } ],
        "output": { "type": "classes", "format": "postcards" },
        "artifact": { "path": "weights.onnx" }
    })" );
    REQUIRE_FALSE( issues.empty() );
    const std::string first = firstFinding( issues );
    REQUIRE( first.find( "manifest_version" ) != std::string::npos );
    REQUIRE( first.find( "output.format" ) == std::string::npos );
}

TEST_CASE( "P1-8 order: a well-formed manifest yields no issues",
           "[models][manifest][r4][order]" )
{
    // Guard against the tests above passing vacuously: the base shape and a
    // benign extension must be clean when nothing is violated.
    REQUIRE( validate( kBase ).empty() );
    REQUIRE( validate( R"({
        "name": "r4-clean", "task": "segmentation", "framework": "onnx",
        "manifest_version": 3,
        "inputs": [ { "name": "x", "data_type": "raster" } ],
        "output": { "type": "classes", "format": "labels" },
        "postprocess": { "class_mapping": [ 1, 2 ] },
        "artifact": { "path": "weights.onnx" }
    })" ).empty() );
}
