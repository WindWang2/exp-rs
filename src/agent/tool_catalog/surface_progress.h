// src/agent/tool_catalog/surface_progress.h
//
// Structured progress relay between the TaskCenter (the execution authority)
// and the surface protocols. MCP maps an event to notifications/progress
// (2024-11-05 shape: progressToken/progress/total); the CLI batch runner
// relays the same percentages through CliIO::reportProgress (--progress-json
// NDJSON on stderr). One source, two projections — never two truths.
#pragma once

#include <string>

namespace sicnu::agent::tool_catalog::progress {

/// TaskCenter progress contract: a 0..1 FRACTION. Completion stamps 1.0
/// (task_center.cpp), and RSOperatorContext::reportProgress is documented
/// "progress reaches 0/1" with every production operator reporting fractions
/// (rs_classification_pipeline 0.15*k, cartography 0.1/0.5/0.9, fused_chain
/// 0.05+0.9*p). MCP relays it with total = 1.0, so the wire value is the
/// fraction unchanged (R18-C C03: it used to be divided by 100, and the
/// 5-point limiter threshold could never be crossed by fraction steps,
/// suppressing all intermediate progress).
constexpr double kTaskCenterMax = 1.0;

/// Bounded-rate gate for progress emission: emit when the progress moved by
/// at least @p minDelta (on the 0..1 fraction scale, so the default 0.05
/// means ~5 percentage points) since the last emitted event, on any
/// state change, or when the event is terminal (always emitted, exactly once
/// per transition). Prevents NDJSON/notification flooding from chatty
/// operators while guaranteeing completion is announced.
class RateLimiter
{
public:
    explicit RateLimiter( double minDelta = 0.05 ) : m_minDelta( minDelta ) {}

    /// true when @p progress (0..1 fraction) should be emitted. @p terminal
    /// must be passed for exactly one call per task; @p state labels terminal
    /// transitions so a change is always announced.
    bool shouldEmit( double progress, bool terminal, const std::string &state );

    double lastEmitted() const { return m_lastEmitted; }

private:
    double m_minDelta;
    double m_lastEmitted = -1.0;
    std::string m_lastState;
    bool m_terminalSent = false;
};

} // namespace sicnu::agent::tool_catalog::progress
