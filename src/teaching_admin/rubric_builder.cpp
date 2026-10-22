#include "rubric_builder.h"

#include "grader/grader_error.h"
#include "grader/grader_json.h"
#include "grader/grader_types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace sicnu::teaching_admin {

QSet<QString> supportedLabRuleKinds()
{
    return {
        QStringLiteral( "range" ),
        QStringLiteral( "mean_sigma" ),
        QStringLiteral( "gain_invariance" ),
        QStringLiteral( "nodata_ratio" ),
        QStringLiteral( "histogram_shape" ),
        QStringLiteral( "classification_kappa" ),
        QStringLiteral( "confusion_marginals" ),
        QStringLiteral( "change_area_interval" ),
        QStringLiteral( "crs_grid" ),
        QStringLiteral( "zone_stats" ),
        QStringLiteral( "band_layout" ),
        QStringLiteral( "spatial_agreement" ),
        QStringLiteral( "series_separation" ),
        QStringLiteral( "spectral_signature" ),
        QStringLiteral( "file_check" ),
        QStringLiteral( "provenance" ),
    };
}

QSet<QString> supportedProcessCriterionKinds()
{
    return {
        QStringLiteral( "stage" ),
        QStringLiteral( "metric" ),
        QStringLiteral( "fact" ),
        QStringLiteral( "answer" ),
    };
}

WeightSumCheck checkWeightSum( const QJsonArray &weightedItems, const QString &weightKey, double expected,
                               double eps )
{
    WeightSumCheck c;
    c.expected = expected;
    for ( const auto &v : weightedItems )
        c.sum += v.toObject().value( weightKey ).toDouble( 0.0 );
    c.matchesExpected = std::fabs( c.sum - expected ) <= eps;
    return c;
}

ValidationResult validateLabRules( const QJsonObject &rules )
{
    ValidationResult r;
    if ( rules.value( QStringLiteral( "schema_version" ) ).toString() != QLatin1String( "sicnu.lab.rules/1" ) )
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema_version" ),
                    QStringLiteral( "must be sicnu.lab.rules/1" ) );
    if ( rules.value( QStringLiteral( "lab_id" ) ).toString().isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "lab_id" ), QStringLiteral( "required" ) );
    if ( rules.value( QStringLiteral( "title" ) ).toString().isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "title" ), QStringLiteral( "required" ) );

    const QJsonObject artifact = rules.value( QStringLiteral( "artifact" ) ).toObject();
    const QString kind = artifact.value( QStringLiteral( "kind" ) ).toString();
    if ( kind != QLatin1String( "raster" ) && kind != QLatin1String( "file" ) )
        r.addError( QStringLiteral( "malformed_grading_rule" ), QStringLiteral( "artifact.kind" ),
                    QStringLiteral( "artifact.kind must be raster|file" ) );

    const QJsonArray assertions = rules.value( QStringLiteral( "assertions" ) ).toArray();
    if ( assertions.isEmpty() )
        r.addError( QStringLiteral( "missing_field" ), QStringLiteral( "assertions" ),
                    QStringLiteral( "assertions must be non-empty" ) );

    const QSet<QString> kinds = supportedLabRuleKinds();
    QSet<QString> ids;
    double weightSum = 0.0;
    for ( int i = 0; i < assertions.size(); ++i )
    {
        const QJsonObject a = assertions.at( i ).toObject();
        const QString path = QStringLiteral( "assertions[%1]" ).arg( i );
        const QString id = a.value( QStringLiteral( "id" ) ).toString();
        if ( id.isEmpty() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".id" ),
                        QStringLiteral( "assertion id required" ) );
        if ( ids.contains( id ) )
            r.addError( QStringLiteral( "duplicate_assertion_id" ), path + QStringLiteral( ".id" ),
                        QStringLiteral( "duplicate assertion id" ) );
        ids.insert( id );
        const QString ak = a.value( QStringLiteral( "kind" ) ).toString();
        if ( !kinds.contains( ak ) )
            r.addError( QStringLiteral( "unsupported_assertion_kind" ), path + QStringLiteral( ".kind" ),
                        QStringLiteral( "kind not supported by grader: " ) + ak );
        if ( !a.contains( QStringLiteral( "params" ) ) || !a.value( QStringLiteral( "params" ) ).isObject() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".params" ),
                        QStringLiteral( "params object required" ) );
        if ( !a.contains( QStringLiteral( "weight" ) ) )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight required" ) );
        // Weight arithmetic (R16-C/T06 WP3): per-item weights must be
        // finite and non-negative. A negative deduction weight cancels
        // against a bloated partner and passes an aggregate-only check,
        // and the grader would compute scores from nonsense arithmetic.
        // Type first (round-1 review): toDouble(0.0) coerced a non-numeric
        // weight ("0.5x") to a legal 0.0 — the value must be a JSON number.
        const QJsonValue weightValue = a.value( QStringLiteral( "weight" ) );
        if ( !weightValue.isDouble() )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight must be a number" ) );
        const double weight = weightValue.toDouble( 0.0 );
        // R18-E D5: align with the grading authority
        // (OutputVerifier::loadLabRules): each weight in (0,100] — a zero
        // weight passes an aggregate check here and fails the WHOLE class
        // at batch time with exit 2 (every row error).
        if ( !std::isfinite( weight ) || weight <= 0.0 || weight > 100.0 )
            r.addError( QStringLiteral( "malformed_grading_rule" ), path + QStringLiteral( ".weight" ),
                        QStringLiteral( "weight must lie in (0,100] — the grader refuses anything else at batch time" ) );
        // severity, when PRESENT, is the grader's closed vocabulary — the
        // authority (output_verifier) refuses an empty or unknown spelling
        // at load time, so a present-but-invalid member must fail preflight
        // too, not just a non-empty one (round-1 review P1-3: "" used to
        // sail through here and fail the whole class at batch time).
        if ( a.contains( QStringLiteral( "severity" ) ) )
        {
            const QJsonValue severityValue = a.value( QStringLiteral( "severity" ) );
            const bool legal = severityValue.isString()
                               && ( severityValue.toString() == QLatin1String( "normal" )
                                    || severityValue.toString() == QLatin1String( "blocking" ) );
            if ( !legal )
                r.addError( QStringLiteral( "malformed_grading_rule" ),
                            path + QStringLiteral( ".severity" ),
                            QStringLiteral( "severity must be the string normal|blocking when present" ) );
        }
        weightSum += weight;
        // Evidence / metric vocabulary soft checks
        if ( a.contains( QStringLiteral( "evidence" ) )
             && a.value( QStringLiteral( "evidence" ) ).toString().isEmpty() )
            r.addWarning( QStringLiteral( "missing_evidence_ref" ), path + QStringLiteral( ".evidence" ),
                          QStringLiteral( "empty evidence ref" ) );
    }

    // R18-E D5: the grader requires weights to sum to EXACTLY 100
    // (output_verifier.cpp, ±1e-9) — "positive" here blessed rule files the
    // batch run refuses wholesale. Preflight and the admin dock both call
    // this validator, so this is where the mismatch has to die.
    // Non-finite sums (overflow to inf, or a NaN member) are refused with
    // the same error.
    if ( assertions.size() > 0
         && ( !std::isfinite( weightSum ) || std::fabs( weightSum - 100.0 ) > 1e-9 ) )
        r.addError( QStringLiteral( "weight_mismatch" ), QStringLiteral( "assertions" ),
                    QStringLiteral( "assertion weights must sum to exactly 100 (the grader's contract), got %1" )
                        .arg( std::isfinite( weightSum ) ? weightSum : 0.0 ) );

    // passing_score, when declared, is the grader's [0,100] band.
    if ( rules.contains( QStringLiteral( "passing_score" ) ) )
    {
        const QJsonValue passing = rules.value( QStringLiteral( "passing_score" ) );
        if ( !passing.isDouble()
             || passing.toDouble() < 0.0 || passing.toDouble() > 100.0 )
            r.addError( QStringLiteral( "malformed_grading_rule" ),
                        QStringLiteral( "passing_score" ),
                        QStringLiteral( "passing_score must be a number in [0,100]" ) );
    }

    return r;
}

ValidationResult validateProcessRubric( const QJsonObject &rubric )
{
    // #1652-1 (R20-05): this validator used to grade a FLAT criteria[]
    // dialect (top-level criteria + weight sum 1.0 + metricKey/answerKey)
    // that the grading engine never consumes — sicnu.grader.rubric/1 is
    // dimensions[].criteria[] with maxPoints and weight==sum(maxPoints)
    // (grader_types.cpp). A rubric the engine ACCEPTS was reported invalid
    // here (and by release_preflight, which gates on this verdict), while a
    // rubric this validator accepted was refused by the engine. The admin
    // console now delegates to the engine's own parser — ONE rubric truth,
    // the same discipline as the lab-pack validator (no second pack truth).
    ValidationResult r;
    if ( rubric.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( "sicnu.grader.rubric/1" ) )
    {
        r.addError( QStringLiteral( "schema_mismatch" ), QStringLiteral( "schema" ),
                    QStringLiteral( "must be sicnu.grader.rubric/1" ) );
        return r;
    }

    // The engine reads the bytes, not the Qt value tree: serialize and hand
    // the text to the grader's own strict reader, so what is validated here
    // is exactly what `lab --grade` would parse.
    const QByteArray bytes = QJsonDocument( rubric ).toJson( QJsonDocument::Compact );
    const std::string text( bytes.constData(), static_cast<std::size_t>( bytes.size() ) );

    sicnu::grader::GraderError error;
    const auto doc = sicnu::grader::parseJsonStrict( text, error );
    if ( !doc )
    {
        r.addError( QStringLiteral( "malformed_grading_rule" ),
                    QString::fromStdString( error.path ),
                    QString::fromUtf8( "invalid rubric JSON: %1" )
                        .arg( QString::fromStdString( error.message ) ) );
        return r;
    }

    const auto parsed = sicnu::grader::GradingRubric::fromJson( *doc, error );
    if ( !parsed )
    {
        // The engine's document-level refusal vocabulary maps 1:1 onto the
        // admin console's two error codes; the message keeps the engine's
        // exact wording + path so the teacher can locate the offending member.
        const QString code = error.code == sicnu::grader::GraderErrorCode::SchemaVersionUnsupported
                                 ? QStringLiteral( "schema_mismatch" )
                                 : QStringLiteral( "malformed_grading_rule" );
        r.addError( code, QString::fromStdString( error.path ),
                    QString::fromUtf8( "grader refused this rubric: %1" )
                        .arg( QString::fromStdString( error.message ) ) );
        return r;
    }

    return r;
}

} // namespace sicnu::teaching_admin
