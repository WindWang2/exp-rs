// tests/test_placeholder_grammar.cpp
#include <catch2/catch_test_macros.hpp>

#include "workflow/placeholder_grammar.h"

using namespace sicnu::workflow;

TEST_CASE("Placeholder Grammar - Parsing all supported syntax variants", "[workflow][grammar]") {
    SECTION("Unbraced $stepId.output form") {
        auto refs = parsePlaceholders("$step1.output");
        REQUIRE(refs.size() == 1);
        CHECK(refs[0].rawRef == "$step1.output");
        CHECK(refs[0].stepId == "step1");
        CHECK(refs[0].portName == "output");
        CHECK(refs[0].isValid());
    }

    SECTION("Braced ${step1.output} form") {
        auto refs = parsePlaceholders("${step1.output}");
        REQUIRE(refs.size() == 1);
        CHECK(refs[0].rawRef == "${step1.output}");
        CHECK(refs[0].stepId == "step1");
        CHECK(refs[0].portName == "output");
        CHECK(refs[0].isValid());
    }

    SECTION("Braced custom port ${step1.nir_band} form") {
        auto refs = parsePlaceholders("${step1.nir_band}");
        REQUIRE(refs.size() == 1);
        CHECK(refs[0].rawRef == "${step1.nir_band}");
        CHECK(refs[0].stepId == "step1");
        CHECK(refs[0].portName == "nir_band");
        CHECK(refs[0].isValid());
    }

    SECTION("Task ID reference ${task.42.output} form") {
        auto refs = parsePlaceholders("${task.42.output}");
        REQUIRE(refs.size() == 1);
        CHECK(refs[0].rawRef == "${task.42.output}");
        CHECK(refs[0].parentTaskId == 42);
        CHECK(refs[0].portName == "output");
        CHECK(refs[0].isValid());
    }

    SECTION("Parent keyword reference ${task.parent.output} form") {
        auto refs = parsePlaceholders("${task.parent.output}");
        REQUIRE(refs.size() == 1);
        CHECK(refs[0].rawRef == "${task.parent.output}");
        CHECK(refs[0].isParentKeyword == true);
        CHECK(refs[0].portName == "output");
        CHECK(refs[0].isValid());
    }

    SECTION("Multiple placeholders in single string") {
        auto refs = parsePlaceholders("file_${step1.output}_and_$step2.output");
        REQUIRE(refs.size() == 2);
        CHECK(refs[0].stepId == "step1");
        CHECK(refs[1].stepId == "step2");
    }
}

TEST_CASE("Placeholder Grammar - Substitution and edge inference", "[workflow][grammar]") {
    SECTION("substitutePlaceholders replaces resolved tokens") {
        std::string input = "Process ${step1.output} with ${task.5.output}";
        std::string resolved = substitutePlaceholders(input, [](const PlaceholderRef &ref) {
            if (ref.stepId == "step1") return std::string("/tmp/step1_out.tif");
            if (ref.parentTaskId == 5) return std::string("/tmp/task5_out.tif");
            return ref.rawRef;
        });
        CHECK(resolved == "Process /tmp/step1_out.tif with /tmp/task5_out.tif");
    }

    SECTION("inferStepConnections builds DAG connections for braced and unbraced forms") {
        auto conns1 = inferStepConnections("INPUT_LAYERS", "${step_alpha.output}");
        REQUIRE(conns1.size() == 1);
        CHECK(conns1[0].fromStepId == "step_alpha");
        CHECK(conns1[0].fromPort == "output");
        CHECK(conns1[0].toPort == "INPUT_LAYERS");

        auto conns2 = inferStepConnections("RASTER", "$step_beta.custom");
        REQUIRE(conns2.size() == 1);
        CHECK(conns2[0].fromStepId == "step_beta");
        CHECK(conns2[0].fromPort == "custom");
        CHECK(conns2[0].toPort == "RASTER");
    }
}
