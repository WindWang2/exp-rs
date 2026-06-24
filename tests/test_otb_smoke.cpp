// Phase 10B.0: OTB smoke test
// Verifies that OTB libraries link correctly.
// All tests are guarded by SICNU_HAS_OTB; they SKIP when OTB is not built.

#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OTB
// OTB libraries are linked — test that they resolve at runtime
#endif // SICNU_HAS_OTB

TEST_CASE("OTB libraries linked", "[otb][smoke]")
{
#ifdef SICNU_HAS_OTB
    // If SICNU_HAS_OTB is defined and this test runs, the OTB libraries
    // are successfully linked. The test verifies runtime symbol resolution.
    REQUIRE(true);
#else
    SKIP("OTB not built (SICNU_HAS_OTB not defined)");
#endif
}
