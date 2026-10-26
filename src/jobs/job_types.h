// src/jobs/job_types.h
#pragma once
#include <json/json.h>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace sicnu::jobs {

enum class JobState { Queued, Running, Succeeded, Failed, Cancelled };

enum class JobLogLevel { Info, Warning, Error };

struct JobLogLine {
  int64_t unixMs = 0;
  JobLogLevel level = JobLogLevel::Info;
  std::string text;
};

struct JobRequest {
  std::string algorithmId;
  Json::Value params{Json::objectValue};
  std::string title;
  std::string source; // ui|task_panel|dialog|toolbox|module|mcp|workflow
  bool exclusive = false;
  std::string clientTag;
  /// Scheduling priority mirror of sicnu::TaskPriority (0 = High, 1 = Normal,
  /// 2 = Low). Set by TaskCenter at staging so the engine's queue pick honors
  /// the same priority order the admission pass used; workers pick the lowest
  /// value first, FIFO within equal priority.
  int priority = 1;
};

struct JobRecord {
  std::string id;
  JobRequest request;
  JobState state = JobState::Queued;
  double progress = -1.0; // -1 = indeterminate
  std::string statusMessage;
  std::vector<JobLogLine> logLines;
  /// Bounded-log accounting (#1705): lines evicted from the FRONT of
  /// logLines once the per-job cap was reached. Kept alongside logLines so
  /// consumers can tell "job printed N lines" from "N lines are still
  /// retained" — the delta protocol delivers a contiguous recent window,
  /// never the full history of a chatty job.
  std::size_t droppedLogLines = 0;
  /// Transmission hint for notify() copies: 0 = logLines is the cumulative
  /// retained vector; >0 = delta notify (#638) where logLines holds only the
  /// slice of ABSOLUTE appended-line index (logLinesOffset - 1) onward
  /// (#1705: absolute = droppedLogLines + local index, so eviction never
  /// renumbers what consumers already indexed). Consumers that forward log
  /// lines must append every slice line whose absolute index they have not
  /// yet seen and treat (logLinesOffset - 1) + size as the new total
  /// appended count. The authoritative record kept by the engine is always
  /// the bounded cumulative retained window.
  std::size_t logLinesOffset = 0;
  Json::Value result;
  std::string error;
  int64_t createdAtMs = 0;
  int64_t startedAtMs = 0;
  int64_t finishedAtMs = 0;
};

inline int64_t nowUnixMs()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>( system_clock::now().time_since_epoch() ).count();
}

} // namespace sicnu::jobs
