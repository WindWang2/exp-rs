#include "experiment_studio/studio_export.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

namespace sicnu::experiment_studio
{

QJsonObject StudioExportBundle::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "document_type" ), QStringLiteral( "sicnu.experiment_studio.export/1" ) );
    o.insert( QStringLiteral( "study_id" ), studyId );
    QJsonArray runs;
    for ( const QString &id : runIds )
        runs.append( id );
    o.insert( QStringLiteral( "run_ids" ), runs );
    o.insert( QStringLiteral( "study_report" ), studyReport );
    o.insert( QStringLiteral( "fault_teaching" ), faultTeaching );
    o.insert( QStringLiteral( "first_divergence" ), firstDivergence );
    o.insert( QStringLiteral( "designer" ), designer );
    o.insert( QStringLiteral( "run_matrix" ), runMatrix );
    QJsonArray caps;
    for ( const QString &c : capsuleRefs )
        caps.append( c );
    o.insert( QStringLiteral( "capsule_refs" ), caps );
    o.insert( QStringLiteral( "csv_run_table" ), csvRunTable );
    QJsonArray iss;
    for ( const QString &s : issues )
        iss.append( s );
    o.insert( QStringLiteral( "issues" ), iss );
    return o;
}

Result<StudioExportBundle> StudioExportBundle::fromJson( const QJsonObject &json )
{
    const QString schema = json.value( QStringLiteral( "schema" ) ).toString();
    const QString docType = json.value( QStringLiteral( "document_type" ) ).toString();
    if ( schema != QString::fromUtf8( kStudioExportSchema )
         && docType != QStringLiteral( "sicnu.experiment_studio.export/1" ) )
    {
        return Result<StudioExportBundle>::failure(
            studioError( QStringLiteral( "experiment_studio.export_schema_mismatch" ),
                         QStringLiteral( "foreign export schema refused" ) ) );
    }
    StudioExportBundle b;
    b.studyId = json.value( QStringLiteral( "study_id" ) ).toString();
    const QJsonArray runs = json.value( QStringLiteral( "run_ids" ) ).toArray();
    for ( const QJsonValue &v : runs )
        b.runIds.append( v.toString() );
    b.studyReport = json.value( QStringLiteral( "study_report" ) ).toObject();
    b.faultTeaching = json.value( QStringLiteral( "fault_teaching" ) ).toObject();
    b.firstDivergence = json.value( QStringLiteral( "first_divergence" ) ).toObject();
    b.designer = json.value( QStringLiteral( "designer" ) ).toObject();
    b.runMatrix = json.value( QStringLiteral( "run_matrix" ) ).toObject();
    const QJsonArray caps = json.value( QStringLiteral( "capsule_refs" ) ).toArray();
    for ( const QJsonValue &v : caps )
        b.capsuleRefs.append( v.toString() );
    b.csvRunTable = json.value( QStringLiteral( "csv_run_table" ) ).toString();
    return Result<StudioExportBundle>::success( b );
}

QString studyRunTableToCsv( const QJsonObject &studyReportJson )
{
    QString out;
    QTextStream ts( &out );
    ts << "point_id,replicate_index,run_id,status,seed,error_summary,output_asset_path\n";
    const QJsonArray table = studyReportJson.value( QStringLiteral( "run_table" ) ).toArray();
    for ( const QJsonValue &v : table )
    {
        const QJsonObject r = v.toObject();
        auto esc = []( const QString &s ) {
            QString t = s;
            t.replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) );
            if ( t.contains( QLatin1Char( ',' ) ) || t.contains( QLatin1Char( '"' ) )
                 || t.contains( QLatin1Char( '\n' ) ) )
                return QStringLiteral( "\"%1\"" ).arg( t );
            return t;
        };
        ts << esc( r.value( QStringLiteral( "point_id" ) ).toString() ) << ','
           << r.value( QStringLiteral( "replicate_index" ) ).toInt() << ','
           << esc( r.value( QStringLiteral( "run_id" ) ).toString() ) << ','
           << esc( r.value( QStringLiteral( "status" ) ).toString() ) << ','
           << static_cast<qint64>( r.value( QStringLiteral( "seed" ) ).toDouble() ) << ','
           << esc( r.value( QStringLiteral( "error_summary" ) ).toString() ) << ','
           << esc( r.value( QStringLiteral( "output_asset_path" ) ).toString() ) << '\n';
    }
    return out;
}

Result<void> writeStudioExportJson( const StudioExportBundle &bundle, const QString &path )
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) )
    {
        return Result<void>::failure(
            studioError( QStringLiteral( "experiment_studio.export_write_failed" ),
                         QStringLiteral( "cannot open %1" ).arg( path ) ) );
    }
    const QByteArray bytes = QJsonDocument( bundle.toJson() ).toJson( QJsonDocument::Indented );
    if ( file.write( bytes ) != bytes.size() )
    {
        return Result<void>::failure(
            studioError( QStringLiteral( "experiment_studio.export_write_failed" ),
                         QStringLiteral( "short write to %1" ).arg( path ) ) );
    }
    if ( !file.commit() )
    {
        return Result<void>::failure(
            studioError( QStringLiteral( "experiment_studio.export_write_failed" ),
                         QStringLiteral( "commit failed for %1" ).arg( path ) ) );
    }
    return Result<void>::success();
}

Result<StudioExportBundle> readStudioExportJson( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        return Result<StudioExportBundle>::failure(
            studioError( QStringLiteral( "experiment_studio.export_read_failed" ),
                         QStringLiteral( "cannot open %1" ).arg( path ) ) );
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &err );
    if ( err.error != QJsonParseError::NoError || !doc.isObject() )
    {
        return Result<StudioExportBundle>::failure(
            studioError( QStringLiteral( "experiment_studio.export_read_failed" ),
                         QStringLiteral( "invalid JSON: %1" ).arg( err.errorString() ) ) );
    }
    return StudioExportBundle::fromJson( doc.object() );
}

} // namespace sicnu::experiment_studio
