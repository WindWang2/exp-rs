#include "teaching/lab_status.h"

namespace sicnu::teaching {

const char *labUiStatusWire( LabUiStatus s )
{
  switch ( s ) {
    case LabUiStatus::NotStarted: return "not_started";
    case LabUiStatus::Ready: return "ready";
    case LabUiStatus::InProgress: return "in_progress";
    case LabUiStatus::WaitingHuman: return "waiting_human";
    case LabUiStatus::PendingVerify: return "pending_verify";
    case LabUiStatus::Completed: return "completed";
    case LabUiStatus::NeedsCorrection: return "needs_correction";
    case LabUiStatus::Unavailable: return "unavailable";
    case LabUiStatus::Unknown: return "unknown";
  }
  return "unknown";
}

const char *labUiStatusLabelZh( LabUiStatus s )
{
  switch ( s ) {
    case LabUiStatus::NotStarted: return "未开始";
    case LabUiStatus::Ready: return "已准备";
    case LabUiStatus::InProgress: return "进行中";
    case LabUiStatus::WaitingHuman: return "等待人工操作";
    case LabUiStatus::PendingVerify: return "待验证";
    case LabUiStatus::Completed: return "已完成";
    case LabUiStatus::NeedsCorrection: return "需要修正";
    case LabUiStatus::Unavailable: return "数据或能力不可用";
    case LabUiStatus::Unknown: return "未知";
  }
  return "未知";
}

const char *labUiStatusIconToken( LabUiStatus s )
{
  switch ( s ) {
    case LabUiStatus::NotStarted: return "status-not-started";
    case LabUiStatus::Ready: return "status-ready";
    case LabUiStatus::InProgress: return "status-in-progress";
    case LabUiStatus::WaitingHuman: return "status-waiting-human";
    case LabUiStatus::PendingVerify: return "status-pending-verify";
    case LabUiStatus::Completed: return "status-completed";
    case LabUiStatus::NeedsCorrection: return "status-needs-correction";
    case LabUiStatus::Unavailable: return "status-unavailable";
    case LabUiStatus::Unknown: return "status-unknown";
  }
  return "status-unknown";
}

bool labUiStatusFromWire( const std::string &wire, LabUiStatus &out )
{
  if ( wire == "not_started" ) { out = LabUiStatus::NotStarted; return true; }
  if ( wire == "ready" ) { out = LabUiStatus::Ready; return true; }
  if ( wire == "in_progress" ) { out = LabUiStatus::InProgress; return true; }
  if ( wire == "waiting_human" ) { out = LabUiStatus::WaitingHuman; return true; }
  if ( wire == "pending_verify" ) { out = LabUiStatus::PendingVerify; return true; }
  if ( wire == "completed" ) { out = LabUiStatus::Completed; return true; }
  if ( wire == "needs_correction" ) { out = LabUiStatus::NeedsCorrection; return true; }
  if ( wire == "unavailable" ) { out = LabUiStatus::Unavailable; return true; }
  if ( wire == "unknown" ) { out = LabUiStatus::Unknown; return true; }
  return false;
}

const char *readinessLevelWire( ReadinessLevel r )
{
  switch ( r ) {
    case ReadinessLevel::Ready: return "READY";
    case ReadinessLevel::ReadyWithWarnings: return "READY_WITH_WARNINGS";
    case ReadinessLevel::Blocked: return "BLOCKED";
    case ReadinessLevel::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char *readinessLevelLabelZh( ReadinessLevel r )
{
  switch ( r ) {
    case ReadinessLevel::Ready: return "已就绪";
    case ReadinessLevel::ReadyWithWarnings: return "就绪（有警告）";
    case ReadinessLevel::Blocked: return "受阻";
    case ReadinessLevel::Unknown: return "未知";
  }
  return "未知";
}

bool readinessLevelFromWire( const std::string &wire, ReadinessLevel &out )
{
  if ( wire == "READY" ) { out = ReadinessLevel::Ready; return true; }
  if ( wire == "READY_WITH_WARNINGS" ) { out = ReadinessLevel::ReadyWithWarnings; return true; }
  if ( wire == "BLOCKED" ) { out = ReadinessLevel::Blocked; return true; }
  if ( wire == "UNKNOWN" ) { out = ReadinessLevel::Unknown; return true; }
  return false;
}

const char *experienceModeWire( ExperienceMode m )
{
  switch ( m ) {
    case ExperienceMode::Beginner: return "beginner";
    case ExperienceMode::Expert: return "expert";
  }
  return "beginner";
}

bool experienceModeFromWire( const std::string &wire, ExperienceMode &out )
{
  if ( wire == "beginner" || wire == "lab_mode" || wire == "实验模式" ) {
    out = ExperienceMode::Beginner;
    return true;
  }
  if ( wire == "expert" ) {
    out = ExperienceMode::Expert;
    return true;
  }
  return false;
}

} // namespace sicnu::teaching
