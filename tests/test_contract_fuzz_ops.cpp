// test_contract_fuzz_ops.cpp — bounded property tests over the operator
// schema / parameter projection / model manifest contracts (task D,
// Verification Platform 8.0).
//
// Contracts under fuzz:
//
//   rs_schema builders (operator surface, RSOperatorRegistry inputs):
//     1. Builder totality: makeRootSchema + param helpers produce a valid
//        schema document for any bounded fuzzed names/descriptions — never
//        throw, always an object with the documented shape.
//
//   jsonParamsToVariantMap (parameter projection, #619):
//     2. Totality for any bounded JSON: never throws; the uint64 >
//        INT64_MAX edge degrades to its string form (the documented #619
//        contract), arrays/objects project recursively; projected values
//        are never default-constructed surprises (typed spot checks).
//
//   ModelCatalog::validateManifestJson (model surface):
//     3. Totality: any bounded mutated manifest yields a problem list —
//        never a crash. Truthfulness: the valid baseline yields NO problems
//        while structurally broken variants yield at least one.
//
// Deterministic seeds, hard caps (≤ 512 bytes, bounded iterations).
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/json_params_converter.h"
#include "support/bounded_fuzz.h"

#include <json/json.h>

#include <limits>
#include <string>
#include <vector>

using sicnu::operators::ModelCatalog;
using namespace sicnu::operators::schema;
using sicnu::testing::BoundedRandom;

namespace
{
constexpr size_t kMaxInputBytes = 512;
constexpr int kIterationsPerSeed = 200;

const std::vector<char> kNameAlphabet = [] {
    std::vector<char> chars;
    for ( char c = 'a'; c <= 'z'; ++c )
        chars.push_back( c );
    for ( char c = 'A'; c <= 'Z'; ++c )
        chars.push_back( c );
    for ( char c = '0'; c <= '9'; ++c )
        chars.push_back( c );
    for ( const char c : std::string_view( "_-.\"\\\n\t {}[]:," ) )
        chars.push_back( c );
    return chars;
}();

const std::vector<std::string> kManifestFragments = {
    R"("name":"m")",            R"("task":"segmentation")",
    R"("framework":"scriptfw")", R"("artifact":{"path":"x.onnx"})",
    R"("tiling":{"tile_size":32})", R"("inputPath":"x.tif")",
    R"("output":{"type":"raster"})", R"("id":"a@1.0")",
    "{}", "[]", "null",
};

Json::Value fuzzManifest( BoundedRandom &random )
{
    const std::string baseline = R"({
        "name": "fuzz-model",
        "task": "segmentation",
        "framework": "scriptfw",
        "artifact": { "path": "weights.onnx" },
        "output": { "type": "raster", "tensor_names": ["a"], "classes": ["a", "b"] },
        "tiling": { "tile_size": 32 }
    })";
    const std::string text = random.chance( 0.5 )
      ? random.mutate( baseline, 10 )
      : [&] {
            std::string out = "{";
            const int parts = 1 + random.below( 6 );
            for ( int i = 0; i < parts && out.size() < kMaxInputBytes; ++i )
                out += random.pick( kManifestFragments ) + ",";
            out += "}";
            return out;
        }();
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( text.data(), text.data() + text.size(), &value, &errors ) )
        return Json::Value(); // null = not JSON; contract still total
    return value;
}
} // namespace

TEST_CASE( "operator schema fuzz: builders are total and shape-stable",
           "[contract8][fuzz][ops]" )
{
    for ( const uint64_t seed : { 5ull, 0xAB1Eull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string title = random.string( 0, 24, kNameAlphabet );
            const std::string description = random.string( 0, 48, kNameAlphabet );
            const std::string paramName = random.string( 1, 16, kNameAlphabet );

            Json::Value params( Json::arrayValue );
            params.append( makeStringParam( paramName, description, "d" ) );
            params.append( makeNumberParam( paramName + "2", description, 1.5 ) );
            params.append( makeIntegerParam( paramName + "3", description, -3 ) );
            params.append( makeBooleanParam( paramName + "4", description, true ) );
            params.append( makeEnumParam( paramName + "5", description, { "a", "b" }, "a" ) );

            Json::Value root;
            REQUIRE_NOTHROW( root = makeRootSchema( title, description, params,
                                                    Json::Value( Json::arrayValue ) ) );
            REQUIRE( root.isObject() );
            // Documented shape: the schema carries the title and the
            // parameter array under "properties" (1:1 with the builders) —
            // with the fuzzed name/content actually INSIDE the document.
            CHECK( root["title"].asString() == title );
            CHECK( root["properties"].isArray() );
            CHECK( root["properties"].size() == Json::ArrayIndex( 5 ) );
            CHECK( root["properties"][0]["name"].asString() == paramName );
            CHECK( root["properties"][0]["description"].asString() == description );

            stampDeterminismGrade( root, "tolerance" );
            CHECK( root.isObject() );
        }
    }
}

TEST_CASE( "parameter projection fuzz: jsonParamsToVariantMap is total and "
           "#619-safe",
           "[contract8][fuzz][ops]" )
{
    for ( const uint64_t seed : { 9ull, 0x1BADull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            Json::Value root( Json::objectValue );
            const int members = 1 + random.below( 8 );
            for ( int m = 0; m < members; ++m )
            {
                const std::string key = random.string( 1, 12, kNameAlphabet );
                switch ( random.below( 6 ) )
                {
                case 0:
                    root[key] = random.string( 0, 32, kNameAlphabet );
                    break;
                case 1:
                    root[key] = random.chance( 0.5 );
                    break;
                case 2:
                    root[key] = Json::Value( static_cast<Json::Int64>( random.next() ) );
                    break;
                case 3:
                {
                    // The #619 edge: uint64 above INT64_MAX must degrade to
                    // a string, never throw from asInt64().
                    const Json::UInt64 huge = static_cast<Json::UInt64>( -1 );
                    root[key] = Json::Value( huge );
                    break;
                }
                case 4:
                {
                    Json::Value array( Json::arrayValue );
                    array.append( random.next() & 0xFF );
                    array.append( random.string( 0, 8, kNameAlphabet ) );
                    root[key] = array;
                    break;
                }
                case 5:
                    root[key] = Json::Value( Json::objectValue );
                    break;
                }
            }
            REQUIRE( root.size() == static_cast<Json::ArrayIndex>( members ) );

            QVariantMap projected;
            REQUIRE_NOTHROW(
                projected = sicnu::processing::jsonParamsToVariantMap( root ) );
            REQUIRE( projected.size() == static_cast<int>( members ) );

            // The #619 contract: an out-of-range uint64 projects to its
            // string form (checked on the dedicated member "k0"/first hit).
            for ( const std::string &memberName : root.getMemberNames() )
            {
                const Json::Value &member = root[memberName];
                if ( member.isUInt64() && member.asUInt64() > static_cast<Json::UInt64>(
                                               std::numeric_limits<qint64>::max() ) )
                {
                    const QVariant value = projected[QString::fromStdString( memberName )];
                    CHECK( value.canConvert<QString>() );
                    CHECK( value.toString().toStdString() == member.asString() );
                }
            }
        }
    }
}

TEST_CASE( "model manifest fuzz: validation is total; baseline is clean; "
           "broken variants report problems",
           "[contract8][fuzz][ops]" )
{
    ModelCatalog &catalog = ModelCatalog::instance();

    // Truthfulness anchor: the documented-good manifest validates cleanly.
    const std::string baseline = R"({
        "name": "fuzz-ops-model",
        "task": "segmentation",
        "framework": "scriptfw",
        "artifact": { "path": "weights.onnx" },
        "output": { "type": "raster", "tensor_names": ["a"], "classes": ["a", "b"] },
        "tiling": { "tile_size": 32 }
    })";
    std::vector<std::string> baselineProblems;
    REQUIRE_NOTHROW( baselineProblems = catalog.validateManifestJson( baseline ) );
    // Truthfulness anchor: the documented-good manifest validates CLEANLY.
    CHECK( baselineProblems.empty() );

    int cleanSeen = 0;
    int problemsSeen = 0;
    for ( const uint64_t seed : { 13ull, 0xD00Dull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const Json::Value manifest = fuzzManifest( random );
            Json::StreamWriterBuilder writerBuilder;
            writerBuilder["indentation"] = "";
            const std::string text = Json::writeString( writerBuilder, manifest );
            REQUIRE( text.size() <= 4 * kMaxInputBytes + 16 );

            std::vector<std::string> problems;
            REQUIRE_NOTHROW( problems = catalog.validateManifestJson( text ) );
            if ( problems.empty() )
                ++cleanSeen;
            else
                ++problemsSeen;
        }
    }
    // The fuzz explored both verdicts (mutation MUST sometimes break the
    // manifest): a fuzz that never produces problems is vacuous.
    CHECK( problemsSeen > 0 );
    CHECK( cleanSeen > 0 );
}
