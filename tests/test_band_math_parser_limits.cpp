#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/band_math_ast.h"

#include <array>
#include <string>

namespace
{
std::string nestedMacro(const std::string &name, int depth)
{
    const std::string tail = name == "evi" || name == "savi" ? ", b2, 0.5)"
                           : name == "bsi" ? ", b2, b3, b4)" : ", b2)";
    std::string expression = "b1";
    for (int i = 0; i < depth; ++i)
        expression = name + "(" + expression + tail;
    return expression;
}
}

TEST_CASE("BandMath bounds macro cloning below the syntax depth limit (#2100)",
          "[bandmath][parser][2100]")
{
    // Only 25 calls and a few hundred input bytes, but unbounded lowering
    // duplicates the first argument at every level (millions of AST nodes).
    for (const auto *name : {"ndvi", "ndwi", "mndwi", "nbr", "evi", "savi", "bsi", "std::ndvi"}) {
        INFO(name);
        BandMath::Parser parser(QString::fromStdString(nestedMacro(name, 25)));
        REQUIRE_FALSE(parser.parse());
        REQUIRE(parser.hasError());
        CHECK(parser.error().message.contains("Macro expansion clone budget exceeded"));
        CHECK(parser.error().location.line == 1);
        CHECK(parser.error().location.column > 0);
    }
}

TEST_CASE("BandMath macro clone budget is shared by sibling expansions (#2100)",
          "[bandmath][parser][2100]")
{
    // Each subtree is affordable on its own; the whole expression is not.
    // A per-macro or per-depth limit would miss this shallow aggregate.
    const std::string subtree = nestedMacro("ndvi", 10);
    std::string expression = subtree;
    for (int i = 1; i < 16; ++i)
        expression += "+" + subtree;
    BandMath::Parser parser(QString::fromStdString(expression));
    REQUIRE_FALSE(parser.parse());
    CHECK(parser.error().message.contains("Macro expansion clone budget exceeded"));
}

TEST_CASE("BandMath preserves ordinary nested macro semantics and per-parser budget (#2100)",
          "[bandmath][parser][2100]")
{
    const BandMath::BandData bands = {{1, {0.8f}}, {2, {0.2f}}, {3, {0.1f}}, {4, {0.05f}}};
    const std::array<std::string, 8> names = {"ndvi", "ndwi", "mndwi", "nbr", "evi", "savi", "bsi", "std::ndvi"};
    for (const auto &name : names) {
        // Repeat enough times to exhaust a mistakenly global/static budget.
        for (int repetition = 0; repetition < 16; ++repetition) {
            INFO(name << " repetition " << repetition);
            BandMath::Parser parser(QString::fromStdString(nestedMacro(name, 8)));
            auto root = parser.parse();
            REQUIRE(root);
            CHECK_FALSE(parser.hasError());
            root->resolve(bands);
            float expected = 0.8f;
            for (int i = 0; i < 8; ++i) {
                if (name == "evi")
                    expected = 2.5f * (expected - 0.2f) / (expected + 6.0f * 0.2f - 7.5f * 0.5f + 1.0f);
                else if (name == "savi")
                    expected = (expected - 0.2f) * 1.5f / (expected + 0.2f + 0.5f);
                else if (name == "bsi")
                    expected = ((expected + 0.2f) - (0.1f + 0.05f)) / ((expected + 0.2f) + (0.1f + 0.05f));
                else
                    expected = (expected - 0.2f) / (expected + 0.2f);
            }
            // Same float operations/order as the expanded AST: no widened tolerance.
            CHECK(root->eval(bands, 0) == expected);
            std::vector<int> refs;
            root->collectRefs(refs);
            std::sort(refs.begin(), refs.end());
            refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
            const std::vector<int> expectedRefs = name == "bsi" ? std::vector<int>{1, 2, 3, 4}
                                                              : std::vector<int>{1, 2};
            CHECK(refs == expectedRefs);
            BandMath::BytecodeEmitter emitter;
            root->emitBytecode(emitter);
            CHECK_FALSE(emitter.instructions().empty());
        }
    }
}

TEST_CASE("BandMath clone budget errors identify the macro rather than the next token (#2100)",
          "[bandmath][parser][2100]")
{
    const QString expression = QString::fromStdString("max(b1,\n  " + nestedMacro("ndvi", 25) + ")");
    BandMath::Parser parser(expression);
    REQUIRE_FALSE(parser.parse());
    const auto &error = parser.error();
    CHECK(error.message.contains("Macro expansion clone budget exceeded"));
    REQUIRE(error.location.line == 2);
    CHECK(expression.section('\n', 1, 1).mid(error.location.column - 1).startsWith("ndvi("));
}
