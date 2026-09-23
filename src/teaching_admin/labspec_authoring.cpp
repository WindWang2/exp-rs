#include "labspec_authoring.h"
#include "json_util.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <cmath>

namespace sicnu::teaching_admin {

QSet<QString> legalLabspecTopKeys()
{
    return {
        QStringLiteral( "schema" ),
        QStringLiteral( "id" ),
        QStringLiteral( "version" ),
        QStringLiteral( "spec_version" ),
        QStringLiteral( "title" ),
        QStringLiteral( "title_zh" ),
        QStringLiteral( "theme" ),
        QStringLiteral( "audience" ),
        QStringLiteral( "duration_minutes" ),
        QStringLiteral( "prerequisites" ),
        QStringLiteral( "objectives" ),
        QStringLiteral( "principles" ),
        QStringLiteral( "data" ),
        QStringLiteral( "pipeline" ),
        QStringLiteral( "operators" ),
        QStringLiteral( "steps" ),
        QStringLiteral( "grading_ref" ),
        QStringLiteral( "expected_results" ),
        QStringLiteral( "questions" ),
        QStringLiteral( "common_mistakes" ),
        QStringLiteral( "outputs" ),
        QStringLiteral( "human_only" ),
        QStringLiteral( "reflection" ),
        QStringLiteral( "autonomy" ), // only if already legal in LabSpec chain
        QStringLiteral( "glossary" ),
        QStringLiteral( "description" ),
        QStringLiteral( "description_zh" ),
        QStringLiteral( "note_zh" ),
    };
}

ValidationResult validateLabSpec( const QJsonObject &spec, const QSet<QString> &knownOperators,
                                  const QJsonObject &operatorParamSchemas, const QString &repoRoot )
{
    ValidationResult r;
    const QString schema = spec.value( QStringLiteral( "schema" ) ).toString();
    const bool hasSpecVersion = spec.contains( QStringLiteral( "spec_version" ) );
    if ( schema.isEmpty() && !hasSpecVersion )
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema" ),
                    QStringLiteral( "LabSpec must declare schema or spec_version" ) );

    // Fail-closed version contract: D3 pins schema to sicnu.labspec.v1
    // (data/labs/labspec.schema.json); D2 pins spec_version to 1|2|3
    // (data/schemas/labspec.schema.json, lab_spec_loader). Anything else is a
    // typed error, never a silent accept.
    if ( !schema.isEmpty() && schema != QLatin1String( "sicnu.labspec.v1" ) )
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema" ),
                    QStringLiteral( "unsupported LabSpec schema: " ) + schema );
    if ( hasSpecVersion )
    {
        bool versionOk = false;
        const double v = spec.value( QStringLiteral( "spec_version" ) ).toDouble( -1 );
        for ( const double legal : { 1.0, 2.0, 3.0 } )
            if ( std::fabs( v - legal ) < 1e-9 )
                versionOk = true;
        if ( !versionOk )
            r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "spec_version" ),
                        QStringLiteral( "unsupported spec_version (need 1, 2 or 3)" ) );
    }

    // D3 offline contract: labs must not require network access.
    const QJsonObject data = spec.value( QStringLiteral( "data" ) ).toObject();
    if ( spec.contains( QStringLiteral( "data" ) ) && data.contains( QStringLiteral( "offline" ) )
         && !data.value( QStringLiteral( "offline" ) ).toBool( false ) )
        r.addError( QStringLiteral( "offline_required" ), QStringLiteral( "data.offline" ),
                    QStringLiteral( "LabSpec must be offline-capable (data.offline=true)" ) );

    const QSet<QString> legal = legalLabspecTopKeys();
    for ( auto it = spec.begin(); it != spec.end(); ++it )
    {
        if ( !legal.contains( it.key() ) )
            r.addError( QStringLiteral( "unknown_field" ), it.key(),
                        QStringLiteral( "unknown LabSpec field" ) );
    }

    const QString id = spec.value( QStringLiteral( "id" ) ).toString();
    if ( id.isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "id" ),
                    QStringLiteral( "id required" ) );

    // Operators list (array of strings or objects with id)
    auto checkOpId = [&]( const QString &opId, const QString &path ) {
        if ( opId.isEmpty() )
        {
            r.addError( QStringLiteral( "invalid_operator" ), path, QStringLiteral( "empty operator id" ) );
            return;
        }
        // Fail-closed: an empty registry means nothing is known — every
        // operator reference is flagged instead of silently passing.
        if ( !knownOperators.contains( opId ) )
            r.addError( QStringLiteral( "unknown_operator" ), path,
                        QStringLiteral( "operator not in registry: " ) + opId );
    };

    const QJsonValue opsVal = spec.value( QStringLiteral( "operators" ) );
    if ( opsVal.isArray() )
    {
        const QJsonArray ops = opsVal.toArray();
        for ( int i = 0; i < ops.size(); ++i )
        {
            const QJsonValue v = ops.at( i );
            if ( v.isString() )
                checkOpId( v.toString(), QStringLiteral( "operators[%1]" ).arg( i ) );
            else if ( v.isObject() )
                checkOpId( v.toObject().value( QStringLiteral( "id" ) ).toString(),
                           QStringLiteral( "operators[%1].id" ).arg( i ) );
        }
    }

    const QJsonArray steps = spec.value( QStringLiteral( "steps" ) ).toArray();
    for ( int i = 0; i < steps.size(); ++i )
    {
        const QJsonObject step = steps.at( i ).toObject();
        const QString path = QStringLiteral( "steps[%1]" ).arg( i );
        const QString opId = step.value( QStringLiteral( "operator_id" ) ).toString(
            step.value( QStringLiteral( "operator" ) ).toString() );
        const bool humanOnly = step.value( QStringLiteral( "human_only" ) ).toBool(
            spec.value( QStringLiteral( "human_only" ) ).toBool( false ) );
        if ( !opId.isEmpty() )
            checkOpId( opId, path + QStringLiteral( ".operator_id" ) );
        else if ( !humanOnly && !step.value( QStringLiteral( "reflection" ) ).toBool()
                  && step.value( QStringLiteral( "kind" ) ).toString() != QLatin1String( "reflection" ) )
        {
            // soft: some teaching steps have no operator
        }

        // Params against optional schema
        if ( !opId.isEmpty() && operatorParamSchemas.contains( opId ) )
        {
            const QJsonObject schemaObj = operatorParamSchemas.value( opId ).toObject();
            const QJsonObject props = schemaObj.value( QStringLiteral( "properties" ) ).toObject();
            const QJsonObject params = step.value( QStringLiteral( "params" ) ).toObject(
                step.value( QStringLiteral( "parameters" ) ).toObject() );
            for ( auto pit = params.begin(); pit != params.end(); ++pit )
            {
                if ( !props.isEmpty() && !props.contains( pit.key() ) )
                    r.addError( QStringLiteral( "invalid_param" ),
                                path + QStringLiteral( ".params." ) + pit.key(),
                                QStringLiteral( "param not in operator schema" ) );
            }
        }
    }

    // Dangling repo-relative references — only checkable when a root is
    // provided. Authoring save passes the repo root; an empty root skips
    // existence checks (pure structural lint), but an unsafe ref is still a
    // typed error wherever it appears.
    if ( !repoRoot.isEmpty() )
    {
        auto checkRef = [&]( const QString &relPath, const QString &path ) {
            if ( relPath.isEmpty() )
                return;
            if ( isUnsafeRelativePath( relPath ) )
            {
                r.addError( QStringLiteral( "unsafe_ref" ), path,
                            QStringLiteral( "reference escapes the repo root: " ) + relPath );
                return;
            }
            if ( !QFileInfo::exists( QDir( repoRoot ).filePath( relPath ) ) )
                r.addError( QStringLiteral( "dangling_ref" ), path,
                            QStringLiteral( "referenced file not found: " ) + relPath );
        };
        checkRef( spec.value( QStringLiteral( "grading_ref" ) ).toObject()
                    .value( QStringLiteral( "intent_ref" ) ).toString(),
                  QStringLiteral( "grading_ref.intent_ref" ) );
        checkRef( data.value( QStringLiteral( "spec_ref" ) ).toString(),
                  QStringLiteral( "data.spec_ref" ) );
    }

    return r;
}

QJsonObject projectRecipeCompileView( const QJsonObject &labSpec, const QJsonObject &pipelineDoc )
{
    QJsonArray stepOps;
    for ( const auto &sv : labSpec.value( QStringLiteral( "steps" ) ).toArray() )
    {
        const QJsonObject step = sv.toObject();
        stepOps.append( QJsonObject{
            { QStringLiteral( "id" ), step.value( QStringLiteral( "id" ) ) },
            { QStringLiteral( "operator_id" ),
              step.value( QStringLiteral( "operator_id" ) ).toString(
                  step.value( QStringLiteral( "operator" ) ).toString() ) },
            { QStringLiteral( "human_only" ), step.value( QStringLiteral( "human_only" ) ) },
            { QStringLiteral( "title" ),
              step.value( QStringLiteral( "title" ) ).toString(
                  step.value( QStringLiteral( "title_zh" ) ).toString() ) },
        } );
    }
    return QJsonObject{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.teaching.recipe_compile_view/1" ) },
        { QStringLiteral( "lab_id" ), labSpec.value( QStringLiteral( "id" ) ) },
        { QStringLiteral( "read_only" ), true },
        { QStringLiteral( "note" ),
          QStringLiteral( "Recipe/pipeline is a projection; LabSpec is authoring truth." ) },
        { QStringLiteral( "steps" ), stepOps },
        { QStringLiteral( "pipeline" ), pipelineDoc },
    };
}

} // namespace sicnu::teaching_admin
