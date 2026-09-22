// src/teaching/lab_status.h — closed student-facing status vocabulary.
// Text+icon contract (never color-only). Wire spellings are stable.
#pragma once

#include <string>

namespace sicnu::teaching {

/// Closed UI status for labs / steps / course cards.
enum class LabUiStatus {
  NotStarted,       // 未开始
  Ready,            // 已准备
  InProgress,       // 进行中
  WaitingHuman,     // 等待人工操作
  PendingVerify,    // 待验证
  Completed,        // 已完成
  NeedsCorrection,  // 需要修正
  Unavailable,      // 数据或能力不可用
  Unknown,          // fail-closed unknown
};

const char *labUiStatusWire( LabUiStatus s );
const char *labUiStatusLabelZh( LabUiStatus s );
/// Icon token for QIcon/theme mapping (text companion; not color-only).
const char *labUiStatusIconToken( LabUiStatus s );
bool labUiStatusFromWire( const std::string &wire, LabUiStatus &out );

enum class ReadinessLevel {
  Ready,
  ReadyWithWarnings,
  Blocked,
  Unknown,
};

const char *readinessLevelWire( ReadinessLevel r );
const char *readinessLevelLabelZh( ReadinessLevel r );
bool readinessLevelFromWire( const std::string &wire, ReadinessLevel &out );

/// Experience mode — same truth sources; different chrome density.
enum class ExperienceMode {
  Beginner, // 实验模式
  Expert,
};

const char *experienceModeWire( ExperienceMode m );
bool experienceModeFromWire( const std::string &wire, ExperienceMode &out );

} // namespace sicnu::teaching
