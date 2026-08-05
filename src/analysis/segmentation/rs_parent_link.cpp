// rs_parent_link.cpp — Pixel-majority parent-link (P1).
#include "rs_parent_link.h"

#include "rs_majority_vote.h"

#include <QHash>
#include <QSet>

RsParentTable RsPixelMajorityParentLink::link( const RsSegmentMap &fine,
                                               const RsSegmentMap &coarse ) const
{
    RsParentTable table;

    if ( fine.isEmpty() || coarse.isEmpty() )
    {
        table.ok = false;
        table.errorMessage = QStringLiteral( "Parent-link requires non-empty fine and coarse maps" );
        return table;
    }

    if ( fine.width() != coarse.width() || fine.height() != coarse.height() )
    {
        table.ok = false;
        table.errorMessage = QStringLiteral(
                                 "Parent-link size mismatch: fine %1x%2 vs coarse %3x%4" )
                               .arg( fine.width() )
                               .arg( fine.height() )
                               .arg( coarse.width() )
                               .arg( coarse.height() );
        return table;
    }

    const auto &fineLabels = fine.labels();
    const auto &coarseLabels = coarse.labels();
    const int n = fineLabels.size();

    // fineId → (coarseId → vote count)
    QHash<quint32, QHash<quint32, int>> votes;

    for ( int i = 0; i < n; ++i )
    {
        const quint32 f = fineLabels[i];
        if ( f == 0 )
            continue;
        const quint32 c = coarseLabels[i];
        if ( c == 0 )
            continue;
        ++votes[f][c];
    }

    // Ensure every non-zero fine segment appears (orphan → parent 0).
    const QSet<quint32> fineIds = fine.uniqueLabels();
    for ( quint32 f : fineIds )
    {
        const auto voteIt = votes.constFind( f );
        if ( voteIt == votes.constEnd() || voteIt->isEmpty() )
        {
            table.fineToParent.insert( f, 0 );
            continue;
        }

        // ADR 0060: the majority decision (max votes, ties → smaller coarse
        // id) is the single analysis-layer kernel, majorityKeyWithTieBreak.
        table.fineToParent.insert( f, majorityKeyWithTieBreak( voteIt.value() ) );
    }

    table.ok = true;
    return table;
}
