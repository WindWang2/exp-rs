// src/app/shell/operator_preflight.cpp — R25-D pre-run preflight line
#include "operator_preflight.h"

#include "help/help_descriptor.h"
#include "processing/framework/algorithm_meta_store.h"

#include <QLocale>

namespace sicnu::app
{

QString operatorPreflightSummary( const std::string &operatorId )
{
    const auto entry = sicnu::processing::AlgorithmMetaStore::instance().find( operatorId );
    if ( !entry )
        return QString();

    QStringList parts;
    if ( !entry->task.empty() )
        parts << QString::fromStdString( entry->task );
    if ( !entry->input.empty() || !entry->output.empty() )
    {
        const QString input = entry->input.empty() ? QStringLiteral( "?" )
                                                   : QString::fromStdString( entry->input );
        const QString output = entry->output.empty() ? QStringLiteral( "?" )
                                                     : QString::fromStdString( entry->output );
        parts << QObject::tr( "%1 → %2" ).arg( input, output );
    }
    // #707 drift rule: only a DECLARED gpu flag may speak — a missing field
    // must never read as a confident CPU-only claim.
    if ( entry->gpuDeclared )
        parts << ( entry->gpu ? QObject::tr( "GPU-accelerated" ) : QObject::tr( "CPU inference" ));
    if ( entry->accuracy >= 0.0 )
        parts << QObject::tr( "benchmark accuracy %1%" )
                     .arg( QLocale().toString( entry->accuracy * 100.0, 'f', 1 ) );

    QString summary = parts.isEmpty() ? QString() : parts.join( QStringLiteral( " · " ) );
    if ( !entry->notes.empty() )
    {
        const QString notes =
          sicnu::help::truncateSummary( QString::fromStdString( entry->notes ), 220 );
        summary = summary.isEmpty() ? notes
                                    : summary + QStringLiteral( " — " ) + notes;
    }
    return summary;
}

} // namespace sicnu::app
