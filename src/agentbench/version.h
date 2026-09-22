// src/agentbench/version.h
#pragma once

//
// RS14 Agent Benchmark & Evaluation Harness.
//
// Offline, traces-in trajectory benchmark: versioned cases, deterministic
// fake agent, recorded-trace replay, metric evaluation and reporting.
// Pure C++20 + jsoncpp; deliberately no Qt (headless, portable, fast tests).
// Evaluation never executes tools, never opens a store, never schedules work.
//

namespace sicnu::agentbench
{

/// Framework version of the benchmark harness itself. Bumped when the
/// evaluation semantics change so historical scores stay explainable.
inline constexpr const char *kAgentBenchFrameworkVersion = "1.0.0";

} // namespace sicnu::agentbench
