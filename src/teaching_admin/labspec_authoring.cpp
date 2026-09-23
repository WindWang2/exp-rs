#include "labspec_authoring.h"

#include <QJsonArray>

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
                                  const QJsonObject &operatorParamSchemas )
{
    ValidationResult r;
    const QString schema = spec.value( QStringLiteral( "schema" ) ).toString();
    const bool hasSpecVersion = spec.contains( QStringLiteral( "spec_version" ) );
    if ( schema.isEmpty() && !hasSpecVersion )
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema" ),
                    QStringLiteral( "LabSpec must declare schema or spec_version" ) );

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
