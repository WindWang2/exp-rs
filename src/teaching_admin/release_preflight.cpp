#include "release_preflight.h"
#include "curriculum_editor.h"
#include "data_pack_manager.h"
#include "json_util.h"
#include "labspec_authoring.h"
#include "rubric_builder.h"

#include <QJsonArray>

namespace sicnu::teaching_admin {

QJsonObject TeachingReleaseReport::toJson() const
{
    return sortKeys( QJsonObject{
        { QStringLiteral( "schema" ), schema },
        { QStringLiteral( "ok" ), ok },
        { QStringLiteral( "software_version" ), softwareVersion },
        { QStringLiteral( "curriculum_digest" ), curriculumDigest },
        { QStringLiteral( "labspec_digest" ), labSpecDigest },
        { QStringLiteral( "rules_digest" ), rulesDigest },
        { QStringLiteral( "pack_digest" ), packDigest },
        { QStringLiteral( "issues" ), issuesToJson( issues ) },
        { QStringLiteral( "sections" ), sections },
    } );
}

QByteArray TeachingReleaseReport::canonicalBytes() const
{
    return canonicalJsonBytes( toJson() );
}

QString TeachingReleaseReport::canonicalDigest() const
{
    return sha256Hex( canonicalBytes() );
}

TeachingReleaseReport runPreflight( const PreflightInput &in )
{
    TeachingReleaseReport report;
    report.softwareVersion = in.softwareVersion;

    if ( !in.curriculum.isEmpty() )
    {
        const auto cv = validateCurriculum( in.curriculum, in.knownLabIds );
        report.sections.insert( QStringLiteral( "curriculum" ), cv.toJson() );
        report.issues += cv.issues;
        report.curriculumDigest = sha256Hex( canonicalJsonBytes( sortKeys( in.curriculum ) ) );
    }

    if ( !in.labSpec.isEmpty() )
    {
        const auto lv = validateLabSpec( in.labSpec, in.knownOperators, in.operatorParamSchemas );
        report.sections.insert( QStringLiteral( "labspec" ), lv.toJson() );
        report.issues += lv.issues;
        report.labSpecDigest = sha256Hex( canonicalJsonBytes( sortKeys( in.labSpec ) ) );
        const auto recipe = projectRecipeCompileView( in.labSpec, in.pipeline );
        report.sections.insert( QStringLiteral( "recipe_compile_view" ), recipe );
    }

    if ( !in.labRules.isEmpty() )
    {
        const auto rv = validateLabRules( in.labRules );
        report.sections.insert( QStringLiteral( "lab_rules" ), rv.toJson() );
        report.issues += rv.issues;
        report.rulesDigest = sha256Hex( canonicalJsonBytes( sortKeys( in.labRules ) ) );
    }

    if ( !in.processRubric.isEmpty() )
    {
        const auto pv = validateProcessRubric( in.processRubric );
        report.sections.insert( QStringLiteral( "process_rubric" ), pv.toJson() );
        report.issues += pv.issues;
    }

    if ( !in.packDocument.isEmpty() )
    {
        const auto pk = validatePackDocument( in.packDocument, in.repoRoot );
        report.sections.insert( QStringLiteral( "data_pack" ), pk.toJson() );
        report.issues += pk.issues;
        report.packDigest = sha256Hex( canonicalJsonBytes( sortKeys( in.packDocument ) ) );
    }

    report.sections.insert( QStringLiteral( "packs_checked" ),
                            QJsonArray::fromStringList( in.packIdsChecked ) );

    report.ok = true;
    for ( const auto &i : report.issues )
    {
        if ( i.severity == QLatin1String( "error" ) )
        {
            report.ok = false;
            break;
        }
    }
    return report;
}

} // namespace sicnu::teaching_admin
