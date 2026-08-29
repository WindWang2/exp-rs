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
};

struct JobRecord {
  std::string id;
  JobRequest request;
  JobState state = JobState::Queued;
  double progress = -1.0; // -1 = indeterminate
  std::string statusMessage;
  std::vector<JobLogLine> logLines;
  /// Transmission hint for notify() copies: 0 = logLines is the cumulative
  /// vector; >0 = delta notify (#638) where logLines holds only the slice
  /// beginning at engine-side index (logLinesOffset - 1). Consumers that
  /// forward log lines must append the whole slice exactly once and treat
  /// (logLinesOffset - 1) + size as the new engine total. The authoritative
  /// record kept by the engine is always cumulative.
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
