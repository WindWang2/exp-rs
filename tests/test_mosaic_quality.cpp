// tests/test_mosaic_quality.cpp — F15 Package E oracle tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/mosaic_quality.h"

#include <vector>

using namespace rs::mosaic;
using Catch::Approx;

namespace {
QualityWeights allOn { 0.25, 0.25, 0.25, 0.25 };
}

TEST_CASE( "Quality: clear near scene scores highest", "[processing][mosaic][quality]" )
{
    SceneQualityInput clear;
    clear.hasCloud = true;
    clear.cloudFraction = 0.0;
    SceneQualityInput cloudy;
    cloudy.hasCloud = true;
    cloudy.cloudFraction = 0.8;

    QualityOptions opt;
    const auto s1 = QualityScorer::score( clear, allOn, opt );
    const auto s2 = QualityScorer::score( cloudy, allOn, opt );
    CHECK( s1.score == Approx( 1.0 ) );
    CHECK( s2.score == Approx( 0.2 ) ); // only the cloud dimension provided
    CHECK( s2.score < s1.score );
}

TEST_CASE( "Quality: weights renormalize over provided dimensions",
           "[processing][mosaic][quality]" )
{
    // Only time provided: score = 1 − |t|/scale regardless of nominal weights.
    SceneQualityInput recent;
    recent.hasTime = true;
    recent.timeDays = 30.0;
    SceneQualityInput old;
    old.hasTime = true;
    old.timeDays = 183.0;

    QualityOptions opt;
    opt.timeScaleDays = 365.0;
    CHECK( QualityScorer::score( recent, allOn, opt ).score == Approx( 1.0 - 30.0 / 365.0 ) );
    CHECK( QualityScorer::score( old, allOn, opt ).score == Approx( 1.0 - 183.0 / 365.0 ) );
}

TEST_CASE( "Quality: combined dimensions average by weight",
           "[processing][mosaic][quality]" )
{
    SceneQualityInput s;
    s.hasCloud = true;
    s.cloudFraction = 0.5;  // dimension score 0.5
    s.hasQuality = true;
    s.quality = 1.0;        // dimension score 1.0
    // Default weights: cloud 0.5, quality 0.25 -> (0.5*0.5 + 0.25*1)/0.75
    QualityWeights w;
    const auto r = QualityScorer::score( s, w, {} );
    CHECK( r.score == Approx( ( 0.5 * 0.5 + 0.25 * 1.0 ) / 0.75 ) );
}

TEST_CASE( "Quality: out-of-range inputs clamp and are flagged",
           "[processing][mosaic][quality]" )
{
    SceneQualityInput bad;
    bad.hasQuality = true;
    bad.quality = 1.7; // above 1
    bad.hasTime = true;
    bad.timeDays = 4000.0; // beyond timeScaleDays
    const auto r = QualityScorer::score( bad, allOn, {} );
    CHECK( r.clamped );
    // allOn weights: quality 0.25 (score 1 after clamp), time 0.25 (score 0).
    CHECK( r.score == Approx( ( 0.25 * 1.0 + 0.25 * 0.0 ) / 0.50 ) );
    CHECK( r.score >= 0.0 );
    CHECK( r.score <= 1.0 );
}

TEST_CASE( "Quality: no dimensions provided -> neutral score",
           "[processing][mosaic][quality]" )
{
    const auto r = QualityScorer::score( SceneQualityInput {}, allOn, {} );
    CHECK( r.score == 1.0 );
    CHECK_FALSE( r.clamped );
}

TEST_CASE( "Quality: ordering is score-descending with deterministic ties",
           "[processing][mosaic][quality]" )
{
    std::vector<QualityScore> scores( 4 );
    scores[0].score = 0.5;
    scores[1].score = 0.9;
    scores[2].score = 0.5;
    scores[3].score = 0.1;
    const std::vector<int> priorities { 9, 0, 1, 0 };
    const auto order = QualityScorer::compositeOrder( scores, priorities );
    REQUIRE( order.size() == 4 );
    CHECK( order[0] == 1 ); // 0.9 first
    // Tie 0.5 between scenes 0 (priority 9) and 2 (priority 1): higher
    // priority wins (plan convention).
    CHECK( order[1] == 0 );
    CHECK( order[2] == 2 );
    CHECK( order[3] == 3 );
}
