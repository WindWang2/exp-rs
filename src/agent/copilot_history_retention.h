#pragma once
#include <QJsonArray>
namespace sicnu::agent {
/// Conversation-retention bound for the copilot request history (#1056).
inline constexpr int kMaxHistoryMessages = 80;
/// Drop the oldest complete turns so the array stays bounded. Invariants:
///  - index 0 (the system prompt) is always kept;
///  - the array may only restart at a role=="user" entry, so an assistant
///    tool_calls entry is never orphaned from its tool result;
///  - when no user boundary exists at/after the cap the array is left
///    unchanged (validity wins over the cap).
void trimMessageHistory( QJsonArray &history );
}
