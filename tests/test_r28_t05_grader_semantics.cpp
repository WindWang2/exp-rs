// tests/test_r28_t05_grader_semantics.cpp — R28-T05 regressions for
// #1804/WE-19: the grader's conflicting-metric detection compares with the
// matcher's band epsilon (a last-ulp round-trip of the SAME value is not a
// conflict), and EvidenceIndex owns its items so a moved/reallocated source
// vector can no longer dangle every judgment.
#include <catch2/catch_test_macros.hpp>

#include "grader/grader_matcher.h"
#include "grader/grader_types.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace sicnu::grader;

namespace
{

GradeEvidenceItem metricItem( const std::string &id, const std::string &key, double value )
{
    GradeEvidenceItem item;
    item.evidenceId = id;
    item.kind = EvidenceKind::Metric;
    item.key = key;
    item.hasValue = true;
    item.value = value;
    return item;
}

Criterion atLeastCriterion( double threshold )
{
    Criterion criterion;
    criterion.criterionId = "c-1";
    criterion.kind = CriterionKind::Metric;
    criterion.evidenceKey = "kappa";
    criterion.maxPoints = 10.0;
    criterion.metric.mode = MetricExpectation::Mode::AtLeast;
    criterion.metric.value = threshold;
    criterion.metric.tolerance = 0.0;
    return criterion;
}

} // namespace

TEST_CASE( "ulp-equal metric recordings are not a conflict (WE-19)",
           "[r28_t05][grader]" )
{
    // The same kappa recorded twice, differing by one round-trip ulp
    // (0.1 + 0.2 vs its JSON round-trip spelling).
    std::vector<GradeEvidenceItem> items;
    items.push_back( metricItem( "obs-1", "kappa", 0.1 + 0.2 ) );
    items.push_back( metricItem( "obs-2", "kappa", 0.30000000000000004 ) );
    const EvidenceIndex index( items );

    const CriterionJudgment judgment =
        match::judgeMetric( atLeastCriterion( 0.3 ), index );
    CHECK( judgment.status == OutcomeStatus::Earned );
    CHECK( judgment.reasonCodes.empty() );
    CHECK( judgment.evidenceIds.size() == 2 );
}

TEST_CASE( "genuinely different metric recordings stay ambiguous (WE-19)",
           "[r28_t05][grader]" )
{
    std::vector<GradeEvidenceItem> items;
    items.push_back( metricItem( "obs-1", "kappa", 0.5 ) );
    items.push_back( metricItem( "obs-2", "kappa", 0.9 ) );
    const EvidenceIndex index( items );

    const CriterionJudgment judgment =
        match::judgeMetric( atLeastCriterion( 0.3 ), index );
    CHECK( judgment.status == OutcomeStatus::Indeterminate );
    REQUIRE( judgment.reasonCodes.size() == 1 );
    CHECK( judgment.reasonCodes.front() == "grader:metric-ambiguous" );
}

TEST_CASE( "evidence index survives a moved source vector (WE-19)",
           "[r28_t05][grader]" )
{
    std::vector<GradeEvidenceItem> items;
    // Zero-padded ids so the evidenceId string sort (the index's documented
    // deterministic order) matches numeric order.
    for ( int i = 0; i < 64; ++i )
    {
        char id[16];
        std::snprintf( id, sizeof( id ), "obs-%02d", i );
        items.push_back( metricItem( id, "kappa", 0.5 ) );
    }
    const EvidenceIndex index( items );

    // Reallocate/move the source out from under the index: pre-#1804 this
    // left every bucketed pointer dangling.
    std::vector<GradeEvidenceItem> moved = std::move( items );
    items = std::vector<GradeEvidenceItem>{};
    moved.push_back( metricItem( "obs-999", "kappa", 0.5 ) ); // force realloc

    const std::vector<const GradeEvidenceItem *> &bucket =
        index.byKey( "kappa" );
    REQUIRE( bucket.size() == 64 );
    // evidenceId-sorted order preserved.
    CHECK( bucket.front()->evidenceId == "obs-00" );
    CHECK( bucket.back()->evidenceId == "obs-63" );
    for ( const GradeEvidenceItem *item : bucket )
        CHECK( item->value == 0.5 );
}
