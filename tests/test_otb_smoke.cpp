// Phase 10B.0: OTB smoke test
// Verifies that OTB headers link and basic types instantiate correctly.
// All tests are guarded by SICNU_HAS_OTB; they SKIP when OTB is not built.

#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OTB

// Test 1: OTB link + Logger
#include "otbLogger.h"

// Test 2: otb::Image instantiation
#include "otbImage.h"

// Test 3: MeanShift filter header compiles
#include "otbMeanShiftSegmentationFilter.h"

#endif // SICNU_HAS_OTB

TEST_CASE("OTB link + Logger", "[otb][smoke]")
{
#ifdef SICNU_HAS_OTB
    auto logger = otb::Logger::New();
    REQUIRE(logger.IsNotNull());
#else
    SKIP("OTB not built (SICNU_HAS_OTB not defined)");
#endif
}

TEST_CASE("otb::Image instantiation", "[otb][smoke]")
{
#ifdef SICNU_HAS_OTB
    using ImageType = otb::Image<float, 2>;
    auto image = ImageType::New();

    ImageType::IndexType start;
    start[0] = 0;
    start[1] = 0;

    ImageType::SizeType size;
    size[0] = 32;
    size[1] = 32;

    ImageType::RegionType region;
    region.SetIndex(start);
    region.SetSize(size);

    image->SetRegions(region);
    image->Allocate();

    // Verify buffer: 32*32 = 1024 pixels
    REQUIRE(image->GetBufferedRegion().GetNumberOfPixels() == 1024);
#else
    SKIP("OTB not built (SICNU_HAS_OTB not defined)");
#endif
}

TEST_CASE("MeanShift filter header compiles", "[otb][smoke]")
{
#ifdef SICNU_HAS_OTB
    // Instantiate the MeanShift segmentation filter template.
    // This test verifies the header compiles and the template can be instantiated.
    using InputImageType = otb::VectorImage<float, 2>;
    using OutputImageType = otb::Image<unsigned int, 2>;
    using MeanShiftFilterType = otb::MeanShiftSegmentationFilter<InputImageType, OutputImageType, OutputImageType>;

    auto filter = MeanShiftFilterType::New();
    REQUIRE(filter.IsNotNull());
#else
    SKIP("OTB not built (SICNU_HAS_OTB not defined)");
#endif
}
