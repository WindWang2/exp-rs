#include "curriculum_editor.h"
#include "json_util.h"

#include <QDir>
#include <QHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQueue>
#include <QRegularExpression>

namespace sicnu::teaching_admin {

namespace {

const QSet<QString> kTopKeys = {
    QStringLiteral( "schema" ),        QStringLiteral( "id" ),
    QStringLiteral( "title" ),         QStringLiteral( "title_zh" ),
    QStringLiteral( "audience_zh" ),   QStringLiteral( "note_zh" ),
    QStringLiteral( "modules" ),       QStringLiteral( "forward_references" ),
};

const QSet<QString> kModuleKeys = {
    QStringLiteral( "id" ),
    QStringLiteral( "index" ),
    QStringLiteral( "title" ),
    QStringLiteral( "title_zh" ),
    QStringLiteral( "summary_zh" ),
    QStringLiteral( "learning_outcomes" ),
    QStringLiteral( "prerequisite_modules" ),
    QStringLiteral( "estimated_effort_minutes" ),
    QStringLiteral( "optional" ),
    QStringLiteral( "labs" ),
};

const QSet<QString> kLabKeys = {
    QStringLiteral( "lab_id" ),
    QStringLiteral( "role" ),
    QStringLiteral( "estimated_effort_minutes" ),
    QStringLiteral( "required_data_packs" ),
    QStringLiteral( "teacher_notes" ),
};

const QSet<QString> kRoles = {
    QStringLiteral( "core" ),
    QStringLiteral( "optional" ),
    QStringLiteral( "external" ),
};

QRegularExpression moduleIdRe()
{
    return QRegularExpression( QStringLiteral( "^m[0-9]{2}_[a-z][a-z0-9_]*$" ) );
}

QRegularExpression labIdRe()
{
    return QRegularExpression( QStringLiteral( "^[a-z][a-z0-9_]*$" ) );
}

void checkKeys( const QJsonObject &obj, const QSet<QString> &allowed, const QString &path,
                ValidationResult &r )
{
    for ( auto it = obj.begin(); it != obj.end(); ++it )
    {
        if ( !allowed.contains( it.key() ) )
            r.addError( QStringLiteral( "unknown_key" ), path + QStringLiteral( "." ) + it.key(),
                        QStringLiteral( "未知键；curriculum 拒绝未知字段。" ) );
    }
}

} // namespace

ValidationResult validateCurriculum( const QJsonObject &manifest, const QSet<QString> &knownLabIds,
                                     const QSet<QString> &knownPackIds )
{
    ValidationResult r;
    if ( manifest.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( "sicnu.curriculum/1" ) )
    {
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema" ),
                    QStringLiteral( "schema must be sicnu.curriculum/1" ) );
        return r;
    }
    checkKeys( manifest, kTopKeys, QStringLiteral( "" ), r );

    for ( const char *req : { "id", "title", "title_zh", "modules" } )
    {
        if ( !manifest.contains( QLatin1String( req ) ) )
            r.addError( QStringLiteral( "missing_field" ), QString::fromLatin1( req ),
                        QStringLiteral( "required field missing" ) );
    }

    const QJsonArray modules = manifest.value( QStringLiteral( "modules" ) ).toArray();
    if ( modules.isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "modules" ),
                    QStringLiteral( "modules must be a non-empty array" ) );

    QSet<QString> moduleIds;
    QSet<int> indices;
    QHash<QString, QStringList> prereqEdges;

    for ( int mi = 0; mi < modules.size(); ++mi )
    {
        const QString at = QStringLiteral( "modules[%1]" ).arg( mi );
        const QJsonObject mod = modules.at( mi ).toObject();
        checkKeys( mod, kModuleKeys, at, r );
        const QString id = mod.value( QStringLiteral( "id" ) ).toString();
        if ( !moduleIdRe().match( id ).hasMatch() )
            r.addError( QStringLiteral( "invalid_module_id" ), at + QStringLiteral( ".id" ),
                        QStringLiteral( "module id pattern failed" ) );
        if ( moduleIds.contains( id ) )
            r.addError( QStringLiteral( "duplicate_module_id" ), at + QStringLiteral( ".id" ),
                        QStringLiteral( "duplicate module id" ) );
        moduleIds.insert( id );
        const int index = mod.value( QStringLiteral( "index" ) ).toInt( -1 );
        if ( index < 1 )
            r.addError( QStringLiteral( "missing_field" ), at + QStringLiteral( ".index" ),
                        QStringLiteral( "index must be >= 1" ) );
        else if ( indices.contains( index ) )
            r.addError( QStringLiteral( "duplicate_module_index" ), at + QStringLiteral( ".index" ),
                        QStringLiteral( "duplicate module index" ) );
        indices.insert( index );

        QStringList prereqs;
        const QJsonArray pr = mod.value( QStringLiteral( "prerequisite_modules" ) ).toArray();
        for ( int pi = 0; pi < pr.size(); ++pi )
        {
            const QString p = pr.at( pi ).toString();
            prereqs.append( p );
        }
        prereqEdges.insert( id, prereqs );

        const QJsonArray labs = mod.value( QStringLiteral( "labs" ) ).toArray();
        if ( labs.isEmpty() )
            r.addError( QStringLiteral( "missing_field" ), at + QStringLiteral( ".labs" ),
                        QStringLiteral( "labs must be non-empty" ) );
        QSet<QString> labIdsInMod;
        for ( int li = 0; li < labs.size(); ++li )
        {
            const QString labAt = at + QStringLiteral( ".labs[%1]" ).arg( li );
            const QJsonObject lab = labs.at( li ).toObject();
            checkKeys( lab, kLabKeys, labAt, r );
            const QString labId = lab.value( QStringLiteral( "lab_id" ) ).toString();
            if ( !labIdRe().match( labId ).hasMatch() )
                r.addError( QStringLiteral( "invalid_lab_id" ), labAt + QStringLiteral( ".lab_id" ),
                            QStringLiteral( "lab_id pattern failed" ) );
            if ( labIdsInMod.contains( labId ) )
                r.addError( QStringLiteral( "duplicate_lab_ref" ), labAt + QStringLiteral( ".lab_id" ),
                            QStringLiteral( "duplicate lab_id in module" ) );
            labIdsInMod.insert( labId );
            if ( !knownLabIds.isEmpty() && !knownLabIds.contains( labId ) )
                r.addError( QStringLiteral( "unknown_lab_reference" ), labAt + QStringLiteral( ".lab_id" ),
                            QStringLiteral( "lab_id not in registry/labs" ) );
            const QString role = lab.value( QStringLiteral( "role" ) ).toString();
            if ( !kRoles.contains( role ) )
                r.addError( QStringLiteral( "invalid_role" ), labAt + QStringLiteral( ".role" ),
                            QStringLiteral( "role must be core|optional|external" ) );
            const QJsonArray packs = lab.value( QStringLiteral( "required_data_packs" ) ).toArray();
            for ( int pk = 0; pk < packs.size(); ++pk )
            {
                const QString packId = packs.at( pk ).toString();
                if ( !knownPackIds.isEmpty() && !knownPackIds.contains( packId ) )
                    r.addError( QStringLiteral( "unknown_pack_reference" ),
                                labAt + QStringLiteral( ".required_data_packs[%1]" ).arg( pk ),
                                QStringLiteral( "pack not found" ) );
            }
        }
    }

    // Prerequisite existence
    for ( auto it = prereqEdges.begin(); it != prereqEdges.end(); ++it )
    {
        for ( const QString &p : it.value() )
        {
            if ( !moduleIds.contains( p ) )
                r.addError( QStringLiteral( "unknown_prerequisite" ),
                            QStringLiteral( "modules" ) + QStringLiteral( "." ) + it.key(),
                            QStringLiteral( "prerequisite module unknown: " ) + p );
        }
    }

    // Kahn cycle check
    QHash<QString, int> indeg;
    for ( const QString &id : moduleIds )
        indeg[id] = 0;
    QHash<QString, QStringList> forward;
    for ( auto it = prereqEdges.begin(); it != prereqEdges.end(); ++it )
    {
        for ( const QString &p : it.value() )
        {
            if ( !moduleIds.contains( p ) )
                continue;
            forward[p].append( it.key() );
            indeg[it.key()] += 1;
        }
    }
    QQueue<QString> q;
    for ( auto it = indeg.begin(); it != indeg.end(); ++it )
        if ( it.value() == 0 )
            q.enqueue( it.key() );
    int seen = 0;
    while ( !q.isEmpty() )
    {
        const QString u = q.dequeue();
        ++seen;
        for ( const QString &v : forward.value( u ) )
        {
            indeg[v] -= 1;
            if ( indeg[v] == 0 )
                q.enqueue( v );
        }
    }
    if ( seen != moduleIds.size() )
        r.addError( QStringLiteral( "cyclic_prerequisites" ), QStringLiteral( "modules" ),
                    QStringLiteral( "prerequisite_modules 构成环；课程先修必须是 DAG。" ) );

    return r;
}

QSet<QString> discoverKnownLabIds( const CurriculumPaths &paths )
{
    QSet<QString> ids;
    QDir labs( paths.labsDir );
    if ( labs.exists() )
    {
        const auto entries = labs.entryList( { QStringLiteral( "*.lab.json" ), QStringLiteral( "*.labspec.json" ) },
                                             QDir::Files );
        for ( const QString &name : entries )
        {
            QFile f( labs.filePath( name ) );
            if ( !f.open( QIODevice::ReadOnly ) )
                continue;
            const auto doc = QJsonDocument::fromJson( f.readAll() );
            const QString id = doc.object().value( QStringLiteral( "id" ) ).toString();
            if ( !id.isEmpty() )
                ids.insert( id );
        }
    }
    QFile reg( paths.registryPath );
    if ( reg.open( QIODevice::ReadOnly ) )
    {
        const auto doc = QJsonDocument::fromJson( reg.readAll() ).object();
        const QJsonObject canonical = doc.value( QStringLiteral( "canonical" ) ).toObject();
        for ( auto it = canonical.begin(); it != canonical.end(); ++it )
        {
            ids.insert( it.key() );
            const QJsonArray aliases = it.value().toObject().value( QStringLiteral( "aliases" ) ).toArray();
            for ( const auto &a : aliases )
                ids.insert( a.toString() );
        }
        const QJsonObject oos = doc.value( QStringLiteral( "out_of_scope" ) ).toObject();
        for ( auto it = oos.begin(); it != oos.end(); ++it )
            ids.insert( it.key() );
    }
    return ids;
}

QSet<QString> discoverKnownPackIds( const CurriculumPaths &paths )
{
    QSet<QString> ids;
    QDir packs( paths.packsDir );
    if ( !packs.exists() )
        return ids;
    const auto entries = packs.entryList( { QStringLiteral( "*.pack.json" ) }, QDir::Files );
    for ( const QString &name : entries )
    {
        QString id = name;
        id.chop( QStringLiteral( ".pack.json" ).size() );
        ids.insert( id );
    }
    return ids;
}

QJsonObject exportCanonicalCurriculum( const QJsonObject &manifest )
{
    return sortKeys( manifest );
}

QJsonObject diffCurriculum( const QJsonObject &previous, const QJsonObject &current )
{
    QSet<QString> prevMods;
    QSet<QString> curMods;
    QSet<QString> prevLabs;
    QSet<QString> curLabs;
    auto collect = []( const QJsonObject &m, QSet<QString> &mods, QSet<QString> &labs ) {
        for ( const auto &mv : m.value( QStringLiteral( "modules" ) ).toArray() )
        {
            const QJsonObject mod = mv.toObject();
            mods.insert( mod.value( QStringLiteral( "id" ) ).toString() );
            for ( const auto &lv : mod.value( QStringLiteral( "labs" ) ).toArray() )
                labs.insert( lv.toObject().value( QStringLiteral( "lab_id" ) ).toString() );
        }
    };
    collect( previous, prevMods, prevLabs );
    collect( current, curMods, curLabs );

    QJsonArray addedMods, removedMods, addedLabs, removedLabs;
    for ( const auto &id : curMods )
        if ( !prevMods.contains( id ) )
            addedMods.append( id );
    for ( const auto &id : prevMods )
        if ( !curMods.contains( id ) )
            removedMods.append( id );
    for ( const auto &id : curLabs )
        if ( !prevLabs.contains( id ) )
            addedLabs.append( id );
    for ( const auto &id : prevLabs )
        if ( !curLabs.contains( id ) )
            removedLabs.append( id );

    return QJsonObject{
        { QStringLiteral( "added_modules" ), addedMods },
        { QStringLiteral( "removed_modules" ), removedMods },
        { QStringLiteral( "added_labs" ), addedLabs },
        { QStringLiteral( "removed_labs" ), removedLabs },
        { QStringLiteral( "previous_digest" ), sha256Hex( canonicalJsonBytes( sortKeys( previous ) ) ) },
        { QStringLiteral( "current_digest" ), sha256Hex( canonicalJsonBytes( sortKeys( current ) ) ) },
    };
}

QJsonObject projectCourseHomePreview( const QJsonObject &manifest )
{
    QJsonArray modulesOut;
    for ( const auto &mv : manifest.value( QStringLiteral( "modules" ) ).toArray() )
    {
        const QJsonObject mod = mv.toObject();
        QJsonArray labsOut;
        for ( const auto &lv : mod.value( QStringLiteral( "labs" ) ).toArray() )
        {
            const QJsonObject lab = lv.toObject();
            labsOut.append( QJsonObject{
                { QStringLiteral( "lab_id" ), lab.value( QStringLiteral( "lab_id" ) ) },
                { QStringLiteral( "role" ), lab.value( QStringLiteral( "role" ) ) },
                { QStringLiteral( "estimated_effort_minutes" ),
                  lab.value( QStringLiteral( "estimated_effort_minutes" ) ) },
            } );
        }
        modulesOut.append( QJsonObject{
            { QStringLiteral( "id" ), mod.value( QStringLiteral( "id" ) ) },
            { QStringLiteral( "index" ), mod.value( QStringLiteral( "index" ) ) },
            { QStringLiteral( "title_zh" ), mod.value( QStringLiteral( "title_zh" ) ) },
            { QStringLiteral( "summary_zh" ), mod.value( QStringLiteral( "summary_zh" ) ) },
            { QStringLiteral( "learning_outcomes" ), mod.value( QStringLiteral( "learning_outcomes" ) ) },
            { QStringLiteral( "prerequisite_modules" ), mod.value( QStringLiteral( "prerequisite_modules" ) ) },
            { QStringLiteral( "labs" ), labsOut },
        } );
    }
    return QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.teaching.course_home_preview/1" ) },
        { QStringLiteral( "course_id" ), manifest.value( QStringLiteral( "id" ) ) },
        { QStringLiteral( "title_zh" ), manifest.value( QStringLiteral( "title_zh" ) ) },
        { QStringLiteral( "modules" ), modulesOut },
        { QStringLiteral( "note" ),
          QStringLiteral( "Teacher-console preview only; student cockpit lives in #1237." ) },
    };
}

} // namespace sicnu::teaching_admin
