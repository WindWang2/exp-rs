// src/agent/copilot_history_retention.cpp
#include "copilot_history_retention.h"

#include <QJsonObject>
#include <QString>

namespace sicnu::agent
{

void trimMessageHistory( QJsonArray &history )
{
  if ( history.size() <= kMaxHistoryMessages )
    return;
  // Cut at the FIRST role=="user" boundary at or after the cap: the retained
  // suffix always starts at a user turn, so no assistant tool_calls entry is
  // orphaned from its tool result, and the cap is hard (unlike the previous
  // "last boundary before the cap" scan, which could leave the array over the
  // limit when a turn outran the cap).
  const int keepFrom = history.size() - kMaxHistoryMessages;
  int cut = -1;
  for ( int i = 1; i < history.size(); ++i )
  {
    if ( i < keepFrom )
      continue;
    if ( history.at( i ).toObject().value( QStringLiteral( "role" ) ).toString()
         == QLatin1String( "user" ) )
    {
      cut = i;
      break;
    }
  }
  if ( cut < 1 )
    return; // no safe boundary: keeping the array valid wins over the cap
  // QJsonArray has no range remove(): drop the head one entry at a time.
  for ( int i = 1; i < cut; ++i )
    history.removeAt( 1 );
}

} // namespace sicnu::agent
