/***************************************************************************
 * rs_sar_temporal_events_operator.h — multi-temporal SAR event dating
 * (Advanced SAR / PolSAR / InSAR 10.0, package D; closes ISSUES.md S-1)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_temporal_events — per-pixel EVENT analysis of N co-registered SAR
 * scenes with REAL acquisition-time semantics: dates come from the explicit
 * `dates` parameter or each scene's SICNU_SAR_ACQUISITION_UTC metadata;
 * missing dates are a typed refusal (ACQUISITION_DATES_MISSING) — the
 * operator never emits index-only products (DECISIONS D-007).
 *
 * Products (fixed 9-band Float32 output, SICNU_SAR_TEMPORAL_EVENT_BANDS):
 *   1 event_flag        0/1 — any date at/above the threshold
 *   2 event_count       dates at/above the threshold
 *   3 first_event_index 0-based scene index (−1 = none)
 *   4 last_event_index
 *   5 first_event_days  floating days since scene 0 (REAL time semantics)
 *   6 last_event_days
 *   7 max_deviation_db  robust |log deviation| vs the median baseline
 *   8 argmax_days       days since scene 0 of the max backscatter
 *   9 valid_count       valid observations (never NaN)
 *
 * Irregular revisit intervals are the normal case (D-008): all time
 * quantities are actual day offsets, never equal-interval indices.
 */
class RsSarTemporalEventsOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_temporal_events"; }
    std::string displayName() const override { return "SAR Temporal Events"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Per-pixel change-event dating across N co-registered SAR "
               "scenes with declared acquisition dates: event flags, first/last "
               "event scene and day offsets, max deviation, and argmax timing.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
