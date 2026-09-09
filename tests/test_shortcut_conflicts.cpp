// test_shortcut_conflicts.cpp — keyboard/a11y guardrail (UX 4.0, Milestone G)
//
// The hidden menu bar is the app's action/shortcut registry; two actions
// claiming the same QKeySequence in overlapping scope makes the binding
// unpredictable. This source-scan walks every QKeySequence literal and
// standard-sequence declaration in the shell action host and fails when one
// key sequence is claimed twice (explicit, commented whitelist entries are
// allowed so real conflicts must be a documented decision).
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace {

QString readSource( const QString &relativePath )
{
    const QStringList candidates = {
        QStringLiteral( "%1/%2" ).arg( QStringLiteral( CMAKE_SOURCE_DIR ), relativePath ),
        QStringLiteral( "../%1" ).arg( relativePath ),
        relativePath,
    };
    for ( const QString &path : candidates )
    {
        QFile f( path );
        if ( f.open( QIODevice::ReadOnly | QIODevice::Text ) )
            return QString::fromUtf8( f.readAll() );
    }
    return {};
}

struct SequenceUse
{
    QString sequence;
    int line = 0;
};

QVector<SequenceUse> collectSequenceUses( const QString &source )
{
    QVector<SequenceUse> uses;
    const QStringList lines = source.split( QLatin1Char( '\n' ) );

    static const QRegularExpression stringLit(
        QStringLiteral( "QKeySequence\\(\\s*\"([^\"]+)\"" ) );
    // Standard sequences: QKeySequence::New / Open / Save / Quit / Undo / ...
    static const QRegularExpression stdLit(
        QStringLiteral( "QKeySequence::([A-Za-z]+)" ) );

    for ( int i = 0; i < lines.size(); ++i )
    {
        auto m = stringLit.globalMatch( lines.at( i ) );
        while ( m.hasNext() )
        {
            uses.append( { m.next().captured( 1 ).toUpper(), i + 1 } );
        }
        m = stdLit.globalMatch( lines.at( i ) );
        while ( m.hasNext() )
        {
            uses.append( { QStringLiteral( "std:%1" ).arg( m.next().captured( 1 ) ),
                           i + 1 } );
        }
    }
    return uses;
}

} // namespace

TEST_CASE( "Shortcuts: no duplicate key sequences in the shell action host",
           "[ux4][shortcuts][a11y]" )
{
    const QString source = readSource( QStringLiteral( "src/app/main_window_menus.cpp" ) );
    REQUIRE_FALSE( source.isEmpty() );

    const QVector<SequenceUse> uses = collectSequenceUses( source );

    // Explicit whitelist (documented decisions): line-specific duplicates the
    // team accepts. Keep this list empty in the ideal case.
    static const QSet<int> whitelistedLines = {};

    QMap<QString, int> firstUse;
    QStringList conflicts;
    for ( const SequenceUse &use : uses )
    {
        if ( whitelistedLines.contains( use.line ) )
            continue;
        if ( firstUse.contains( use.sequence ) )
        {
            conflicts << QStringLiteral( "%1 claimed at lines %2 and %3" )
                             .arg( use.sequence )
                             .arg( firstUse.value( use.sequence ) )
                             .arg( use.line );
        }
        else
        {
            firstUse.insert( use.sequence, use.line );
        }
    }

    INFO( conflicts.join( QStringLiteral( "; " ) ).toStdString() );
    // UX 4.0 baseline: menus declare each binding once. Surfaces may share a
    // handler slot, but a repeated literal here means two competing actions —
    // exactly what this test pins down.
    REQUIRE( conflicts.isEmpty() );
}

// ── Workbench 6.0 Milestone D: conflict scans cover every command source
// (#795) — the hidden menu host is no longer the only place shortcuts are
// declared; the registry definitions and the workbench/ribbon wiring each
// own bindings too. Each source must be internally duplicate-free; the
// registry itself enforces cross-command uniqueness at registration
// (test_command_registry pins that primitive).
TEST_CASE( "Shortcuts: no duplicate key sequences in any command source",
           "[ux6][shortcuts][a11y]" )
{
    static const QStringList commandSources = {
        QStringLiteral( "src/app/main_window_menus.cpp" ),
        QStringLiteral( "src/app/workbench/command_defs.cpp" ),
        QStringLiteral( "src/app/main_window_workbench.cpp" ),
        QStringLiteral( "src/app/shell/ribbon_controller.cpp" ),
        QStringLiteral( "src/app/layer_tree_menu.cpp" ),
    };

    for ( const QString &relative : commandSources )
    {
        const QString source = readSource( relative );
        REQUIRE_FALSE( source.isEmpty() );

        const QVector<SequenceUse> uses = collectSequenceUses( source );

        static const QSet<int> whitelistedLines = {};
        QMap<QString, int> firstUse;
        QStringList conflicts;
        for ( const SequenceUse &use : uses )
        {
            if ( whitelistedLines.contains( use.line ) )
                continue;
            if ( firstUse.contains( use.sequence ) )
            {
                conflicts << QStringLiteral( "%1: %2 claimed at lines %3 and %4" )
                                 .arg( relative )
                                 .arg( use.sequence )
                                 .arg( firstUse.value( use.sequence ) )
                                 .arg( use.line );
            }
            else
            {
                firstUse.insert( use.sequence, use.line );
            }
        }

        INFO( conflicts.join( QStringLiteral( "; " ) ).toStdString() );
        CAPTURE( relative.toStdString() );
        REQUIRE( conflicts.isEmpty() );
    }
}
