// studio_session.cpp — see studio_session.h / studio_session_io.h.
#include "experiment_studio/studio_session.h"

#include "experiment_studio/studio_session_io.h"
#include "experiment_studio/studio_types.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QPair>

#include <utility>

namespace sicnu::experiment_studio
{

QJsonObject StudioSessionState::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "study_id" ), studyId );
    o.insert( QStringLiteral( "experiment_id" ), experimentId );
    o.insert( QStringLiteral( "algorithm_id" ), algorithmId );
    o.insert( QStringLiteral( "active_tab" ), activeTab );
    QJsonArray sel;
    for ( const QString &id : selectedPointIds )
        sel.append( id );
    o.insert( QStringLiteral( "selected_point_ids" ), sel );
    o.insert( QStringLiteral( "reference_run_id" ), referenceRunId );
    o.insert( QStringLiteral( "student_run_id" ), studentRunId );
    o.insert( QStringLiteral( "fault_scenario_id" ), faultScenarioId );
    o.insert( QStringLiteral( "last_export_path" ), lastExportPath );
    o.insert( QStringLiteral( "live_store_path" ), liveStorePath );
    o.insert( QStringLiteral( "report_store_path" ), reportStorePath );
    o.insert( QStringLiteral( "designer_store_path" ), designerStorePath );
    o.insert( QStringLiteral( "submitted_designer_vm" ), submittedDesignerVm );
    // OPTIONAL identity fields are omitted while empty: a refs-only session
    // document (R18/R29 "capsule body never rides the session" contract)
    // must stay free of digest-bearing keys when nothing was submitted.
    if ( !submittedSpecDigest.isEmpty() )
        o.insert( QStringLiteral( "submitted_spec_digest" ), submittedSpecDigest );
    o.insert( QStringLiteral( "submitted_store_path" ), submittedStorePath );
    if ( !submissionJson.isEmpty() )
        o.insert( QStringLiteral( "submission" ), submissionJson );
    QJsonObject pol;
    pol.insert( QStringLiteral( "max_points" ), static_cast<double>( policy.maxPoints ) );
    pol.insert( QStringLiteral( "max_in_flight" ), policy.maxInFlight );
    pol.insert( QStringLiteral( "max_disk_bytes" ), static_cast<double>( policy.maxDiskBytes ) );
    pol.insert( QStringLiteral( "max_per_run_deadline_ms" ),
                static_cast<double>( policy.maxPerRunDeadlineMs ) );
    pol.insert( QStringLiteral( "spatial_preview_max_samples" ), policy.spatialPreviewMaxSamples );
    o.insert( QStringLiteral( "policy" ), pol );
    o.insert( QStringLiteral( "designer_draft" ), designerDraft );
    o.insert( QStringLiteral( "last_study_report" ), lastStudyReport );
    return o;
}

StudioSessionState StudioSessionState::fromJson( const QJsonObject &json )
{
    // Fail-closed schema gate (R18-E T202): the reader used to ignore the
    // marker entirely, so a foreign or future session document imported as
    // if it were current. A refused document imports NOTHING.
    StudioSessionState s;
    const QString schema = json.value( QStringLiteral( "schema" ) ).toString();
    if ( schema != QString::fromUtf8( kStudioSessionSchema ) )
        return s; // ok stays false

    auto stringField = [&json, &s]( const QString &key, QString *out ) {
        const QJsonValue v = json.value( key );
        if ( v.isUndefined() || v.isNull() )
            return; // legacy/optional field: keep the default
        if ( !v.isString() )
        {
            s.issues.append( QStringLiteral( "experiment_studio.session_bad_string:%1" ).arg( key ) );
            return;
        }
        *out = v.toString();
    };
    stringField( QStringLiteral( "study_id" ), &s.studyId );
    stringField( QStringLiteral( "experiment_id" ), &s.experimentId );
    stringField( QStringLiteral( "algorithm_id" ), &s.algorithmId );
    stringField( QStringLiteral( "active_tab" ), &s.activeTab );
    stringField( QStringLiteral( "reference_run_id" ), &s.referenceRunId );
    stringField( QStringLiteral( "student_run_id" ), &s.studentRunId );
    stringField( QStringLiteral( "fault_scenario_id" ), &s.faultScenarioId );
    stringField( QStringLiteral( "last_export_path" ), &s.lastExportPath );
    stringField( QStringLiteral( "live_store_path" ), &s.liveStorePath );
    stringField( QStringLiteral( "report_store_path" ), &s.reportStorePath );
    stringField( QStringLiteral( "designer_store_path" ), &s.designerStorePath );
    stringField( QStringLiteral( "submitted_spec_digest" ), &s.submittedSpecDigest );
    stringField( QStringLiteral( "submitted_store_path" ), &s.submittedStorePath );

    const QJsonValue selValue = json.value( QStringLiteral( "selected_point_ids" ) );
    if ( !selValue.isUndefined() && !selValue.isNull() )
    {
        if ( !selValue.isArray() )
            s.issues.append( QStringLiteral( "experiment_studio.session_bad_array:selected_point_ids" ) );
        else
        {
            const QJsonArray sel = selValue.toArray();
            for ( const QJsonValue &v : sel )
            {
                if ( !v.isString() )
                {
                    s.issues.append(
                        QStringLiteral( "experiment_studio.session_bad_point_id:non-string element" ) );
                    break;
                }
                s.selectedPointIds.append( v.toString() );
            }
        }
    }

    const QPair<QString, QJsonObject *> objectFields[] = {
        { QStringLiteral( "designer_draft" ), &s.designerDraft },
        { QStringLiteral( "last_study_report" ), &s.lastStudyReport },
        { QStringLiteral( "submitted_designer_vm" ), &s.submittedDesignerVm },
        { QStringLiteral( "submission" ), &s.submissionJson },
    };
    for ( const auto &field : objectFields )
    {
        const QJsonValue v = json.value( field.first );
        if ( v.isUndefined() || v.isNull() )
            continue;
        if ( !v.isObject() )
        {
            s.issues.append(
                QStringLiteral( "experiment_studio.session_bad_object:%1" ).arg( field.first ) );
            continue;
        }
        *field.second = v.toObject();
    }

    const QJsonValue polValue = json.value( QStringLiteral( "policy" ) );
    if ( !polValue.isUndefined() && !polValue.isNull() )
    {
        if ( !polValue.isObject() )
        {
            s.issues.append( QStringLiteral( "experiment_studio.session_bad_object:policy" ) );
        }
        else
        {
            const QJsonObject pol = polValue.toObject();
            auto readInt64 = [&pol, &s]( const QString &key, qint64 minValue, qint64 maxValue,
                                        qint64 *out ) {
                const QJsonValue v = pol.value( key );
                if ( v.isUndefined() || v.isNull() )
                    return; // absent OR null: keep the declared default
                QString why;
                if ( !studioSessionInt64Value( v, minValue, maxValue, out, &why ) )
                    s.issues.append(
                        QStringLiteral( "experiment_studio.session_bad_policy:%1 (%2)" ).arg( key, why ) );
            };
            auto readInt = [&pol, &s]( const QString &key, int minValue, int maxValue, int *out ) {
                const QJsonValue v = pol.value( key );
                if ( v.isUndefined() || v.isNull() )
                    return;
                QString why;
                if ( !studioSessionIntValue( v, minValue, maxValue, out, &why ) )
                    s.issues.append(
                        QStringLiteral( "experiment_studio.session_bad_policy:%1 (%2)" ).arg( key, why ) );
            };
            readInt64( QStringLiteral( "max_points" ), 1, 1000000, &s.policy.maxPoints );
            readInt( QStringLiteral( "max_in_flight" ), 1, 64, &s.policy.maxInFlight );
            readInt64( QStringLiteral( "max_disk_bytes" ), 1, 1LL << 40, &s.policy.maxDiskBytes );
            readInt64( QStringLiteral( "max_per_run_deadline_ms" ), 1, 1LL << 40,
                       &s.policy.maxPerRunDeadlineMs );
            readInt( QStringLiteral( "spatial_preview_max_samples" ), 1, 10000000,
                     &s.policy.spatialPreviewMaxSamples );
        }
    }

    if ( !s.issues.isEmpty() )
    {
        // Fail closed on STATE (no partial adoption) but KEEP the diagnostics
        // so the caller can name the exact field that was refused.
        StudioSessionState refused;
        refused.issues = std::move( s.issues );
        return refused;
    }
    s.ok = true;
    s.schema = schema;
    return s;
}

} // namespace sicnu::experiment_studio
