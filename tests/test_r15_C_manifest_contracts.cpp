// tests/test_r15_C_manifest_contracts.cpp — Track 15 WP-C02: registration-time
// cross-constraints for the model manifest contract (framework owner surface).
//
// Oracles (from .planning/r15-c/ORACLES.md):
//   * A detection decode contract that can never execute (no to_input resize,
//     no fixed input extent, or multimodal-only preprocess knobs) is refused
//     at REGISTRATION, mirroring DetectionTileEngine::checkContract, instead
//     of surfacing after the artifact was loaded.
//   * The square-head ambiguity (R14-B A3): with tensor_layout 'auto' a head
//     whose channel count equals the declared fed frame's pixel product is
//     ambiguous — registration refuses with an actionable token; the explicit
//     channels_first/channels_last path stays valid.
//   * output.heads[].dtype is a closed vocabulary (#646: a declared knob is
//     never silently ignored).
//   * Non-detection manifests parse bit-identically (no new rejection class).
//
// Truth source: the catalog's documented manifest contract
// (docs/models/model-manifest.md) plus the engine's checkContract.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
using sicnu::operators::ModelCatalog;

const std::string kManifestPath = "/models/r15-c-probe/model.json";

std::vector<std::string> validate( const std::string &json )
{
    const auto issues = ModelCatalog::instance().validateManifestJson( json, kManifestPath );
    for ( const auto &issue : issues )
        std::cout << "[ISSUE] " << issue << "\n";
    return issues;
}

bool anyIssueCarries( const std::vector<std::string> &issues, const std::string &field,
                      const std::string &token )
{
    for ( const auto &issue : issues )
        if ( issue.find( field ) != std::string::npos && issue.find( token ) != std::string::npos )
            return true;
    return false;
}

/// A well-formed detection manifest: fixed 16x16 frame, to_input resize, two
/// classes (head channels 5+2=7 vs 16*16=256 fed pixels — not square).
const char *kDetectionOk = R"({
    "name": "r15c-det-ok", "task": "detection", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster", "width": 16, "height": 16 } ],
    "preprocess": { "resize": "to_input", "normalize": "linear", "scale": 1.0 },
    "output": { "type": "vector",
                "detection": { "layout": "xywh_objectness", "tensor_layout": "auto",
                               "classes": [ "tree", "shrub" ] } }
})";

/// The square-head shape: an 8x8 fed frame carries 64 candidates for a dense
/// head and the head declares exactly 64 channels (5 + 59 classes).
const char *kDetectionSquareAuto = R"({
    "name": "r15c-det-square", "task": "detection", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster", "width": 8, "height": 8 } ],
    "preprocess": { "resize": "to_input", "normalize": "linear", "scale": 1.0 },
    "output": { "type": "vector",
                "detection": { "layout": "xywh_objectness", "tensor_layout": "auto",
                               "classes": [ "c00", "c01", "c02", "c03", "c04", "c05", "c06",
                                            "c07", "c08", "c09", "c10", "c11", "c12", "c13",
                                            "c14", "c15", "c16", "c17", "c18", "c19", "c20",
                                            "c21", "c22", "c23", "c24", "c25", "c26", "c27",
                                            "c28", "c29", "c30", "c31", "c32", "c33", "c34",
                                            "c35", "c36", "c37", "c38", "c39", "c40", "c41",
                                            "c42", "c43", "c44", "c45", "c46", "c47", "c48",
                                            "c49", "c50", "c51", "c52", "c53", "c54", "c55",
                                            "c56", "c57", "c58" ] } }
})";

/// Same square shape, but the layout is declared EXPLICITLY — the documented
/// recovery path, which must stay valid.
std::string squareExplicit( const std::string &layout )
{
    std::string json = R"({
    "name": "r15c-det-square-explicit", "task": "detection", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster", "width": 8, "height": 8 } ],
    "preprocess": { "resize": "to_input", "normalize": "linear", "scale": 1.0 },
    "output": { "type": "vector",
                "detection": { "layout": "xywh_objectness", "tensor_layout": ")";
    json += layout;
    json += R"(", "classes": [ "c00", "c01", "c02", "c03", "c04", "c05", "c06",
                                            "c07", "c08", "c09", "c10", "c11", "c12", "c13",
                                            "c14", "c15", "c16", "c17", "c18", "c19", "c20",
                                            "c21", "c22", "c23", "c24", "c25", "c26", "c27",
                                            "c28", "c29", "c30", "c31", "c32", "c33", "c34",
                                            "c35", "c36", "c37", "c38", "c39", "c40", "c41",
                                            "c42", "c43", "c44", "c45", "c46", "c47", "c48",
                                            "c49", "c50", "c51", "c52", "c53", "c54", "c55",
                                            "c56", "c57", "c58" ] } }
})";
    return json;
}

} // namespace

TEST_CASE( "R15-C C02: a well-formed detection contract validates unchanged",
           "[models][manifest][r15-c]" )
{
    const auto issues = validate( kDetectionOk );
    for ( const auto &issue : issues )
        INFO( "unexpected issue: " << issue );
    CHECK( issues.empty() );
}

TEST_CASE( "R15-C C02: square-head ambiguity with tensor_layout 'auto' is refused at registration",
           "[models][manifest][r15-c]" )
{
    const auto issues = validate( kDetectionSquareAuto );
    REQUIRE_FALSE( issues.empty() );
    // The finding names the knob, the reason and both magnitudes.
    CHECK( anyIssueCarries( issues, "output.detection.tensor_layout", "auto" ) );
    CHECK( anyIssueCarries( issues, "ambiguous", "64" ) );
    CHECK( anyIssueCarries( issues, "channels_first", "channels_last" ) );
}

TEST_CASE( "R15-C C02: the explicit tensor_layout path stays valid for the same square head",
           "[models][manifest][r15-c]" )
{
    CHECK( validate( squareExplicit( "channels_first" ) ).empty() );
    CHECK( validate( squareExplicit( "channels_last" ) ).empty() );
}

TEST_CASE( "R15-C C02: a detection contract requires the fixed-frame preprocess at registration",
           "[models][manifest][r15-c]" )
{
    // No to_input resize: the head cannot decode in the fixed frame.
    {
        const auto issues = validate( R"({
    "name": "r15c-det-noresize", "task": "detection", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster", "width": 16, "height": 16 } ],
    "output": { "type": "vector",
                "detection": { "classes": [ "tree" ] } } })" );
        REQUIRE_FALSE( issues.empty() );
        CHECK( anyIssueCarries( issues, "output.detection", "to_input" ) );
    }
    // No fixed input extent.
    {
        const auto issues = validate( R"({
    "name": "r15c-det-nosize", "task": "detection", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster" } ],
    "preprocess": { "resize": "to_input" },
    "output": { "type": "vector",
                "detection": { "classes": [ "tree" ] } } })" );
        REQUIRE_FALSE( issues.empty() );
        CHECK( anyIssueCarries( issues, "output.detection", "input.width/height" ) );
    }
    // Multimodal-only preprocess knobs on a detection model.
    {
        const auto issues = validate( R"({
    "name": "r15c-det-pad", "task": "detection", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster", "width": 16, "height": 16 } ],
    "preprocess": { "resize": "to_input", "pad": 8 },
    "output": { "type": "vector",
                "detection": { "classes": [ "tree" ] } } })" );
        REQUIRE_FALSE( issues.empty() );
        CHECK( anyIssueCarries( issues, "preprocess.pad", "multi-input" ) );
    }
}

TEST_CASE( "R15-C C02: output.heads[].dtype is a closed vocabulary",
           "[models][manifest][r15-c]" )
{
    {
        const auto issues = validate( R"({
    "name": "r15c-head-dtype-bad", "task": "segmentation", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster" } ],
    "output": { "type": "classes",
                "heads": [ { "name": "logits", "role": "segmentation", "dtype": "banana" } ] } })" );
        REQUIRE_FALSE( issues.empty() );
        CHECK( anyIssueCarries( issues, "output.heads", "banana" ) );
        CHECK( anyIssueCarries( issues, "float32", "uint8" ) );
    }
    {
        const auto issues = validate( R"({
    "name": "r15c-head-dtype-ok", "task": "segmentation", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster" } ],
    "output": { "type": "classes",
                "heads": [ { "name": "logits", "role": "segmentation", "dtype": "float32" } ] } })" );
        CHECK( issues.empty() );
    }
}

TEST_CASE( "R15-C C02: non-detection manifests gain no new rejection class",
           "[models][manifest][r15-c]" )
{
    // The same 8x8 fixed frame WITHOUT a detection contract must stay valid —
    // the square rule is scoped to the decode contract, not to input geometry.
    const auto issues = validate( R"({
    "name": "r15c-seg-square-frame", "task": "segmentation", "framework": "onnx",
    "inputs": [ { "name": "x", "data_type": "raster", "width": 8, "height": 8 } ],
    "output": { "type": "classes" } })" );
    CHECK( issues.empty() );
}
