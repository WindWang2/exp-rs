// Consolidation tests — verify sicnu_native merged into qgis_algorithms
#include <catch2/catch_test_macros.hpp>

#include <processing/qgsprocessingalgorithm.h>
#include <processing/providers/qgis_algorithms/provider.h>

TEST_CASE("QgisAlgorithms provider has all algorithms", "[consolidate]") {
  QgisAlgorithmsProvider provider;
  provider.load();
  auto algs = provider.algorithms();

  // After consolidation, should have both original + sicnu_native algorithms
  // Original: 13 algorithms
  // sicnu_native: ~15 unique algorithms (some may overlap)
  // Total should be >= 25
  REQUIRE(algs.size() >= 25);
}

TEST_CASE("SicnuNative provider no longer exists", "[consolidate]") {
  QgisAlgorithmsProvider provider;
  provider.load();
  REQUIRE(provider.id() == QStringLiteral("qgis_algorithms"));
  REQUIRE(provider.name() == QStringLiteral("QGIS Basic Algorithms"));

  auto algs = provider.algorithms();
  REQUIRE_FALSE(algs.empty());
  for (const auto *alg : algs) {
    REQUIRE(alg != nullptr);
    REQUIRE_FALSE(alg->id().startsWith("sicnu_native:"));
  }
}
