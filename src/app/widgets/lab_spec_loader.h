// lab_spec_loader.h — LabSpec (declarative lab specification) loading and validation
//
// A LabSpec is one JSON file under data/labs/ describing a guided lab exercise:
// teaching metadata, data prerequisites, operator-bound steps ({operator_id,
// params} — executable headlessly through JobRequest exactly like any dialog)
// or UI-verb steps ({action} — a slot invoked on the main window), optional
// grading reference and thinking questions. Normative contract:
// data/schemas/labspec.schema.json. Docs are generated from these files by
// scripts/gen_lab_docs.py — the JSON is the single source of truth.
//
// This loader is deliberately dependency-light (QtCore + jsoncpp, no widget /
// main-window / processing-registry coupling) so tests can exercise it without
// the app shell. All failures are typed (LabSpecError); callers must surface
// them — falling back to built-in content is forbidden (ADR 0146).
#pragma once

#include <json/json.h>

#include <functional>

#include <QList>
#include <QString>
#include <QStringList>

namespace lab {

/// One step of a guided lab.
struct LabStep
{
    QString title;          ///< English step title.
    QString titleZh;        ///< Chinese step title (中文标题).
    QString descriptionZh;  ///< Chinese teaching body (中文说明).
    QString operatorId;     ///< "rs:*" / "opencv:*" registry id; empty for non-operator steps.
    Json::Value params;     ///< Operator parameters (object); only meaningful with operatorId.
    QString action;         ///< Main-window slot name for UI-verb steps; empty otherwise.
    QString teachingNote;   ///< Background theory the student should take away.
    QString completionHint; ///< How the student knows the step is done.

    bool hasOperator() const { return !operatorId.isEmpty(); }
    bool isManual() const { return operatorId.isEmpty() && action.isEmpty(); }
};

/// A data set the student must load before starting the lab.
struct LabDataRef
{
    QString path; ///< Project-root relative path, e.g. "data/samples/landsat_sample.tif".
    QString note; ///< Optional human-readable description.
};

/// A complete lab specification parsed from one <id>.lab.json file.
struct LabSpec
{
    QString id;                 ///< Canonical id, equals the file stem ("lab02_spectral_analysis").
    QString title;              ///< English display title.
    QString titleZh;            ///< Chinese display title (中文标题).
    QString objective;          ///< Teaching objective, doubles as the list description.
    QList<LabDataRef> prerequisites;
    QList<LabStep> steps;
    QString gradingPipeline;    ///< Project-root relative path; empty when the lab is not graded.
    QStringList thinkingQuestions;

    int stepCount() const { return steps.size(); }
};

/// A typed load/validation failure. `reason` is a stable, self-contained
/// English diagnostic (file/line/lab id context included by toString());
/// localization belongs to the presenting layer.
struct LabSpecError
{
    QString path;   ///< File the error relates to ("<dir>/<file>" or the directory itself).
    QString labId;  ///< Lab id when known; empty when the file could not be identified.
    QString reason; ///< Stable diagnostic, e.g. "step 2: params requires operator_id".
    int line = 0;   ///< 1-based JSON position when known; 0 = n/a.

    QString toString() const;
};

/// Result of loading a lab directory. `errors` non-empty means the caller must
/// surface them; the successfully parsed labs are still returned so a partial
/// directory degrades explicitly rather than silently.
struct LabLoadResult
{
    QList<LabSpec> labs;
    QList<LabSpecError> errors;

    bool ok() const { return errors.isEmpty(); }
};

/// Parse and validate a single LabSpec file. On failure returns a default
/// LabSpec and fills `error`; on success `error->reason` is empty. Mirrors
/// data/schemas/labspec.schema.json: unknown keys rejected, required keys
/// enforced, operator_id/action mutual exclusion, params requires operator_id,
/// id must equal the file stem.
LabSpec loadLabSpecFile( const QString &path, LabSpecError *error = nullptr );

/// Load every *.lab.json in `dir` in file-name order (deterministic). Duplicate
/// lab ids are an error. An unreadable/missing directory is an error with
/// `path` set to `dir`.
LabLoadResult loadLabSpecsFromDir( const QString &dir );

/// Default lab directory, resolved like the rest of the shipped data
/// (SICNU_DATA_DIR override, then project-root walk-up): "<root>/data/labs".
QString defaultLabDirectory();

/// How a param string value is used by the operator, driving path resolution:
/// `data/...` values are runtime inputs, `outputs/...` values are per-lab
/// products. Unrelated strings (expressions, enum values) never match.
enum class PathRole { Input, Output };

/// Rewrites relative `data/...` and `outputs/...` string values in `params`
/// through `resolve` (recursively, covering nested objects and arrays such as
/// rs:mosaic "inputs"). Values without a recognised prefix pass through
/// unchanged. Pure: the loader never touches the filesystem; the caller
/// (widget at submit time, or a test) supplies the resolver.
Json::Value resolveLabParamPaths( const Json::Value &params,
                                  const QString &labId,
                                  const std::function<QString( const QString &relativePath, PathRole role )> &resolve );

/// Stable list rendering used by both the widget error surfacing and tests.
QStringList errorStrings( const LabLoadResult &result );

} // namespace lab
