// test_classification_object_postprocess.cpp — F12 WP-E known-answer maps.
// Every expected output raster below is hand-drawn, not derived from the
// implementation.
#include <catch2/catch_test_macros.hpp>

#include "classification_object_postprocess.h"

#include <vector>

using rs::processing::ClassificationObjectPostProcessor;
using rs::processing::ObjectPostProcessConfig;

namespace
{
// 4x4 reference map, two classes. Hand-drawn; indices row-major.
// segments:                       labels:
//  1 1 2 2                         1 1 1 1
//  1 1 2 2                         1 2 1 1
//  3 3 2 2                         2 2 1 1
//  3 3 2 2                         2 2 1 1
struct TieMap
{
    static constexpr int W = 4, H = 4;
    std::vector<int> segments = { 1, 1, 2, 2,
                                  1, 1, 2, 2,
                                  3, 3, 2, 2,
                                  3, 3, 2, 2 };
    std::vector<int> labels = { 1, 1, 1, 1,
                                1, 2, 1, 1,
                                2, 2, 1, 1,
                                2, 2, 1, 1 };
};
} // namespace

TEST_CASE( "computeSegmentClasses: majority vote with lowest-id tiebreak",
           "[classify][object]" )
{
    TieMap m;
    const auto table = ClassificationObjectPostProcessor::computeSegmentClasses(
      m.labels, m.segments );
    REQUIRE( table.size() == 3 );
    // Segment 1 votes: rows 0-1 left half → {1,1,1,2} → class 1.
    REQUIRE( table.at( 1 ).majorityClass == 1 );
    REQUIRE( table.at( 1 ).area == 4 );
    // Segment 2: all 1 (8 pixels).
    REQUIRE( table.at( 2 ).majorityClass == 1 );
    REQUIRE( table.at( 2 ).area == 8 );
    // Segment 3: all 2.
    REQUIRE( table.at( 3 ).majorityClass == 2 );
    REQUIRE( table.at( 3 ).area == 4 );
}

TEST_CASE( "computeSegmentClasses: NoData labels never vote, NoData segments excluded",
           "[classify][object]" )
{
    // segments 0 = NoData; label -1 = NoData.
    std::vector<int> segments = { 0, 0, 1, 1 };
    std::vector<int> labels = { 5, 5, -1, 2 };
    const auto table = ClassificationObjectPostProcessor::computeSegmentClasses(
      labels, segments );
    REQUIRE( table.size() == 1 );          // segment 0 excluded
    REQUIRE( table.at( 1 ).area == 2 );    // area counts pixels, even NoData-labelled
    REQUIRE( table.at( 1 ).majorityClass == 2 ); // the only vote
}

TEST_CASE( "buildAdjacency: 4- vs 8-connectivity border counts", "[classify][object]" )
{
    const int W = 2, H = 2;
    std::vector<int> segments = { 1, 2,
                                  1, 2 };
    const auto adj4 = ClassificationObjectPostProcessor::buildAdjacency( segments, W, H, 4 );
    REQUIRE( adj4.at( 1 ).at( 2 ) == 2 ); // two orthogonal borders
    const auto adj8 = ClassificationObjectPostProcessor::buildAdjacency( segments, W, H, 8 );
    REQUIRE( adj8.at( 1 ).at( 2 ) == 4 ); // + the two diagonal contacts
}

TEST_CASE( "run: identity painting (no rules) reproduces the majority map",
           "[classify][object]" )
{
    TieMap m;
    ObjectPostProcessConfig cfg; // nothing enabled
    std::vector<int> out( TieMap::W * TieMap::H, 99 );
    const auto result = ClassificationObjectPostProcessor::run(
      m.labels, m.segments, TieMap::W, TieMap::H, cfg, out );
    REQUIRE( result.ok );
    REQUIRE( result.stats.segmentCount == 3 );
    REQUIRE( result.stats.mergedSegments == 0 );
    // Hand-derived: {1→1, 2→1, 3→2}.
    const std::vector<int> expected = { 1, 1, 1, 1,
                                        1, 1, 1, 1,
                                        2, 2, 1, 1,
                                        2, 2, 1, 1 };
    REQUIRE( out == expected );
}

TEST_CASE( "run: smoothing adopts a segment with a strict class-border majority",
           "[classify][object]" )
{
    TieMap m;
    ObjectPostProcessConfig cfg;
    cfg.smoothingIterations = 1;
    std::vector<int> out( TieMap::W * TieMap::H, -1 );
    const auto result = ClassificationObjectPostProcessor::run(
      m.labels, m.segments, TieMap::W, TieMap::H, cfg, out );
    REQUIRE( result.ok );
    // Hand-derived (4-connectivity): segment 3 (class 2) borders only
    // segment 1 (border 4, class 1) → strict majority → adopts class 1.
    // Segment 1 (class 1): neighbours class1 border 4 (seg 2) vs class2
    // border 4 (seg 3) → tie keeps class 1. Segment 2 keeps class 1.
    const std::vector<int> expected = { 1, 1, 1, 1,
                                        1, 1, 1, 1,
                                        1, 1, 1, 1,
                                        1, 1, 1, 1 };
    REQUIRE( out == expected );
    REQUIRE( result.stats.smoothedSegments >= 1 );
}

TEST_CASE( "run: min-area rule merges a small segment into its longest-border "
           "neighbour (tie → lowest id)",
           "[classify][object]" )
{
    // Hand-drawn: segment 3 (area 4, class 2) borders segment 1 (2 px) and
    // segment 2 (2 px), both class 1. Tie on border AND class → lowest id.
    const int W = 4, H = 4;
    std::vector<int> segments = { 1, 1, 2, 2,
                                  1, 1, 2, 2,
                                  1, 1, 3, 3,
                                  1, 1, 3, 3 };
    std::vector<int> labels = { 1, 1, 1, 1,
                                1, 1, 1, 1,
                                1, 1, 2, 2,
                                1, 1, 2, 2 };
    ObjectPostProcessConfig cfg;
    cfg.minSegmentArea = 5;
    std::vector<int> out( W * H, -1 );
    const auto result = ClassificationObjectPostProcessor::run(
      labels, segments, W, H, cfg, out );
    REQUIRE( result.ok );
    // Hand-traced: segment 2 (area 4) merges into segment 1 (border tie
    // 2v2 with segment 3 → lowest class 1 wins), then segment 3 (area 4)
    // merges into the grown segment 1 group (border 4). Both adopt class 1.
    REQUIRE( result.stats.mergedSegments == 2 );
    // Segment 3 adopted class 1 via segment 1; no smoothing configured.
    const std::vector<int> expected = { 1, 1, 1, 1,
                                        1, 1, 1, 1,
                                        1, 1, 1, 1,
                                        1, 1, 1, 1 };
    REQUIRE( out == expected );
}

TEST_CASE( "run: NoData segments and labels never absorb or merge",
           "[classify][object]" )
{
    // Segment 0 is NoData; its pixels must stay -1 even with aggressive
    // rules. Segment 1 holds an isolated island of NoData labels.
    const int W = 4, H = 2;
    std::vector<int> segments = { 0, 0, 1, 1,
                                  0, 0, 1, 1 };
    std::vector<int> labels = { 1, 1, -1, 2,
                                1, 1, 2, 2 };
    ObjectPostProcessConfig cfg;
    cfg.minSegmentArea = 1;   // would merge everything below 1 px (none)
    cfg.smoothingIterations = 3;
    std::vector<int> out( W * H, -1 );
    const auto result = ClassificationObjectPostProcessor::run(
      labels, segments, W, H, cfg, out );
    REQUIRE( result.ok );
    // Segment 0 pixels stay -1 (typed NoData sentinel).
    REQUIRE( out[0] == -1 );
    REQUIRE( out[1] == -1 );
    REQUIRE( out[4] == -1 );
    REQUIRE( out[5] == -1 );
    // Segment 1: votes {2,2} (the -1 pixel never votes) → class 2.
    REQUIRE( out[2] == 2 );
    REQUIRE( out[3] == 2 );
    REQUIRE( out[6] == 2 );
    REQUIRE( out[7] == 2 );
}

TEST_CASE( "run: typed refusals for size mismatch and segment cap",
           "[classify][object]" )
{
    std::vector<int> labels = { 1, 2, 1, 2 };
    std::vector<int> segments = { 1, 1, 2, 2 };
    std::vector<int> out( 4, -1 );
    ObjectPostProcessConfig cfg;

    // Wrong out size.
    std::vector<int> tooSmall( 3, -1 );
    auto r = ClassificationObjectPostProcessor::run(
      labels, segments, 2, 2, cfg, tooSmall );
    REQUIRE( !r.ok );
    REQUIRE( r.error.code ==
             ClassificationObjectPostProcessor::RunResult::Error::Code::SizeMismatch );

    // Segment cap.
    cfg.maxSegments = 1; // map holds 2 segments
    r = ClassificationObjectPostProcessor::run( labels, segments, 2, 2, cfg, out );
    REQUIRE( !r.ok );
    REQUIRE( r.error.code ==
             ClassificationObjectPostProcessor::RunResult::Error::Code::TooManySegments );
}
