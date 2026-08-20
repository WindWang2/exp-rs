// tests/test_post_classification_kernel.cpp — TDD for the transition matrix
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/post_classification.h"

#include <vector>

using TransitionMatrix::countTransitions;
using TransitionMatrix::marginals;

TEST_CASE("TransitionMatrix counts stable and changed classes", "[processing][post_classification]") {
    // before:  0 0 1 1
    // after:   0 1 1 2  -> (0->0), (0->1), (1->1), (1->2)
    const std::vector<int32_t> before = {0, 0, 1, 1};
    const std::vector<int32_t> after = {0, 1, 1, 2};
    std::vector<uint64_t> matrix(9, 0);
    countTransitions(before.data(), after.data(), nullptr, 4, matrix, 3);

    CHECK(matrix[0 * 3 + 0] == 1); // 0 -> 0
    CHECK(matrix[0 * 3 + 1] == 1); // 0 -> 1
    CHECK(matrix[1 * 3 + 1] == 1); // 1 -> 1
    CHECK(matrix[1 * 3 + 2] == 1); // 1 -> 2
    CHECK(matrix[2 * 3 + 2] == 0); // class 2 unchanged elsewhere
}

TEST_CASE("TransitionMatrix skips invalid pixels via the valid mask", "[processing][post_classification]") {
    const std::vector<int32_t> before = {0, 1, 0};
    const std::vector<int32_t> after = {1, 1, 1};
    const std::vector<uint8_t> valid = {1, 0, 1};
    std::vector<uint64_t> matrix(4, 0);
    countTransitions(before.data(), after.data(), valid.data(), 3, matrix, 2);

    // Only pixels 0 (0->1) and 2 (0->1) count; pixel 1 is invalid.
    CHECK(matrix[0 * 2 + 1] == 2);
    CHECK(matrix[1 * 2 + 1] == 0);
}

TEST_CASE("TransitionMatrix ignores out-of-range classes", "[processing][post_classification]") {
    const std::vector<int32_t> before = {0, 5, 1};
    const std::vector<int32_t> after = {1, 1, 0};
    std::vector<uint64_t> matrix(4, 0);
    countTransitions(before.data(), after.data(), nullptr, 3, matrix, 2);

    CHECK(matrix[0 * 2 + 1] == 1); // 0 -> 1
    CHECK(matrix[1 * 2 + 0] == 1); // 1 -> 0
    // class 5 is out of range and skipped.
    CHECK(matrix[0 * 2 + 0] == 0);
    CHECK(matrix[1 * 2 + 1] == 0);
}

TEST_CASE("TransitionMatrix accumulates across calls (block streaming)", "[processing][post_classification]") {
    const std::vector<int32_t> before1 = {0, 0};
    const std::vector<int32_t> after1 = {1, 1};
    const std::vector<int32_t> before2 = {1};
    const std::vector<int32_t> after2 = {2};
    std::vector<uint64_t> matrix(9, 0);
    countTransitions(before1.data(), after1.data(), nullptr, 2, matrix, 3);
    countTransitions(before2.data(), after2.data(), nullptr, 1, matrix, 3);

    CHECK(matrix[0 * 3 + 1] == 2);
    CHECK(matrix[1 * 3 + 2] == 1);
    CHECK(matrix.size() == 9);
}

TEST_CASE("TransitionMatrix marginals derive per-class totals", "[processing][post_classification]") {
    // from: 1->1, 1->2, 2->3
    std::vector<uint64_t> matrix(16, 0);
    matrix[1 * 4 + 1] = 3;
    matrix[1 * 4 + 2] = 1;
    matrix[2 * 4 + 3] = 2;

    std::vector<uint64_t> fromTotals, toTotals;
    marginals(matrix, 4, fromTotals, toTotals);

    CHECK(fromTotals[1] == 4);
    CHECK(fromTotals[2] == 2);
    CHECK(toTotals[1] == 3);
    CHECK(toTotals[2] == 1);
    CHECK(toTotals[3] == 2);
}

TEST_CASE("TransitionMatrix marginals handles classCount <= 0 safely", "[processing][post_classification]") {
    std::vector<uint64_t> matrix = {1, 2, 3, 4};
    std::vector<uint64_t> fromTotals = {10}, toTotals = {20};

    SECTION("classCount = 0") {
        marginals(matrix, 0, fromTotals, toTotals);
        CHECK(fromTotals.empty());
        CHECK(toTotals.empty());
    }

    SECTION("classCount < 0") {
        marginals(matrix, -1, fromTotals, toTotals);
        CHECK(fromTotals.empty());
        CHECK(toTotals.empty());
    }
}
