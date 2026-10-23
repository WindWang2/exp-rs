// R20-05 P0 (R1 review) RED driver: hidden/nested entries must be ACCOUNTED
// skips, never silent vanishes (restored r15-E invariant).
#include "teaching_admin/batch_assessment.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

int main( int argc, char **argv )
{
    QCoreApplication app( argc, argv );
    using namespace sicnu::teaching_admin;

    QTemporaryDir temp;
    QDir submissions( temp.path() + QStringLiteral( "/submissions" ) );
    QDir().mkpath( submissions.path() );

    // Visible student.
    { QDir d( submissions.filePath( QStringLiteral( "lisi" ) ) ); d.mkpath( "." );
      QFile f( d.filePath( QStringLiteral( "hw.tif" ) ) ); f.open( QIODevice::WriteOnly ); f.write( "p" ); f.close(); }
    // Hidden student DIRECTORY.
    const QString hiddenDir = submissions.filePath( QStringLiteral( ".zhangsan" ) );
    { QDir d( hiddenDir ); d.mkpath( "." );
      QFile f( d.filePath( QStringLiteral( "hw.tif" ) ) ); f.open( QIODevice::WriteOnly ); f.write( "p" ); f.close(); }
    // Hidden root-level file.
    { QFile f( submissions.filePath( QStringLiteral( ".leak.tif" ) ) ); f.open( QIODevice::WriteOnly ); f.write( "p" ); f.close(); }
    // Hidden file inside a student dir + nested lesson dir.
    { QDir d( submissions.filePath( QStringLiteral( "wangwu" ) ) ); d.mkpath( "." );
      QFile f( d.filePath( QStringLiteral( "hw.tif" ) ) ); f.open( QIODevice::WriteOnly ); f.write( "p" ); f.close();
      QFile h( d.filePath( QStringLiteral( ".hw.tif" ) ) ); h.open( QIODevice::WriteOnly ); h.write( "p" ); h.close(); }
    const QString nested = submissions.filePath( QStringLiteral( "class2021/sunqi/hw.tif" ) );
    { QDir d( QFileInfo( nested ).absolutePath() ); d.mkpath( "." );
      QFile f( nested ); f.open( QIODevice::WriteOnly ); f.write( "p" ); f.close(); }

    QVector<DiscoverySkip> skips;
    const QVector<SubmissionItem> items = discoverSubmissions( submissions.path(), &skips );

    auto skipFor = [ & ]( const QString &path ) {
        for ( const auto &s : skips ) if ( s.path == path ) return s;
        return DiscoverySkip{};
    };
    const auto hiddenDirSkip = skipFor( hiddenDir );
    const auto nestedSkip = skipFor( QDir( submissions.filePath( QStringLiteral( "class2021/sunqi" ) ) ).absolutePath() );
    const bool hiddenDirOk = !hiddenDirSkip.path.isEmpty() && hiddenDirSkip.reason.contains( QStringLiteral( "hidden" ) );
    const bool nestedOk = !nestedSkip.path.isEmpty() && nestedSkip.reason.contains( QStringLiteral( "nested" ) );
    std::cout << "items=" << items.size() << " skips=" << skips.size()
              << " hiddenDirAccounted=" << ( hiddenDirOk ? 1 : 0 )
              << " nestedAccounted=" << ( nestedOk ? 1 : 0 ) << "\n";
    const bool pass = hiddenDirOk && nestedOk && skips.size() >= 3 && items.size() >= 1;
    std::cout << ( pass ? "ACCOUNTING-OK\n" : "SILENT-VANISH (RED on this tree)\n" );
    return pass ? 0 : 1;
}
