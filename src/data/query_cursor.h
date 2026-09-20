// query_cursor.h — opaque keyset-pagination cursors (12.0 data foundation).
//
// Offset paging degrades to O(offset) on deep pages; a 100k-row catalog
// needs keyset paging ("WHERE (sort_key) > (?) LIMIT n") instead. The
// cursor that threads the sort key between pages is an OPACITY BOUNDARY:
// callers carry it, they never parse it. The encoding carries
//
//   v1 | opaque filter echo | keyset tuple
//
// so a store can (a) detect a cursor replayed against a different filter
// and (b) version the encoding — a future key change fails closed with
// `data.cursor_invalid` instead of silently resuming from a misinterpreted
// tuple.
#pragma once

#include "data_result.h"

#include <QString>
#include <QStringList>

namespace sicnu::data
{

class QueryCursor
{
  public:
    /// Encodes @p parts (filter echo first, keyset tuple last) as one opaque
    /// cursor string. Fields may be empty (an absent filter must
    /// round-trip); at least a filter echo and one key column are required.
    static QString encode( const QStringList &parts );

    /// Decodes a cursor produced by encode(). Fails with
    /// `data.cursor_invalid` when the text was truncated/re-padded/corrupted
    /// or was written by a different encoding version — never with a
    /// best-effort partial decode.
    static Result<QStringList> decode( const QString &cursor );

    QueryCursor() = delete;
};

} // namespace sicnu::data
