// lab_report_writers.h — renderings of the ONE `sicnu.labreport.v1`
// document: deterministic Markdown and a self-contained printable HTML file
// (A4-friendly CSS, inline data URLs, no external resources — the
// teacher-facing artifact). Both refuse documents that fail
// LabReportBuilder::validate: no format ever emits more (or less) than the
// JSON carries.
#pragma once

#include "../data/data_result.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment
{

/// Markdown rendering of a validated lab report document.
sicnu::data::Result<QString> labReportMarkdown( const QJsonObject &document );

/// Self-contained printable HTML rendering of a validated lab report.
sicnu::data::Result<QString> labReportHtml( const QJsonObject &document );

/// Writes the three format siblings for one base path (no extension):
/// `<base>.json` (the schema document, indented), `<base>.md`,
/// `<base>.html`. All-or-nothing per file; the returned list carries the
/// paths actually written. Typed failure on the first write error, and the
/// document is validated BEFORE anything touches the disk.
sicnu::data::Result<QStringList> writeLabReportFiles( const QJsonObject &document,
                                                      const QString &basePath );

} // namespace sicnu::experiment
