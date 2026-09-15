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

/// TaskCenter progress is a 0..100 double; MCP progress is progress/total
/// with total = 1.0.
constexpr double kTaskCenterMax = 100.0;

/// Bounded-rate gate for progress emission: emit when the progress moved by
/// at least @p minDelta (0..100 scale) since the last emitted event, on any
/// state change, or when the event is terminal (always emitted, exactly once
/// per transition). Prevents NDJSON/notification flooding from chatty
/// operators while guaranteeing completion is announced.
class RateLimiter
{
public:
    explicit RateLimiter( double minDelta = 5.0 ) : m_minDelta( minDelta ) {}

    /// true when @p progress (0..100) should be emitted. @p terminal must be
    /// passed for exactly one call per task; @p state labels terminal
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
