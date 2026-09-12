// lab_report_writers.cpp — see lab_report_writers.h.
#include "lab_report_writers.h"

#include "lab_report.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::experiment
{

namespace
{

Diagnostic invalidSchema( const QString &what )
{
    return Diagnostic{ QStringLiteral( "lab.report_invalid_schema" ), what,
                       sicnu::dataset::DiagnosticSeverity::Error };
}

/// Compact single-line JSON for embedding a value inside prose tables.
QString compactJson( const QJsonValue &value )
{
    switch ( value.type() )
    {
        case QJsonValue::Object:
            return QString::fromUtf8(
                QJsonDocument( value.toObject() ).toJson( QJsonDocument::Compact ) );
        case QJsonValue::Array:
            return QString::fromUtf8(
                QJsonDocument( value.toArray() ).toJson( QJsonDocument::Compact ) );
        case QJsonValue::String:
            return value.toString();
        case QJsonValue::Undefined:
        case QJsonValue::Null:
            return QStringLiteral( "null" );
        default:
            return value.toVariant().toString();
    }
}

QString mdEscape( const QString &text )
{
    QString out = text;
    out.replace( QLatin1Char( '|' ), QStringLiteral( "\\|" ) );
    out.replace( QLatin1Char( '\n' ), QStringLiteral( " " ) );
    return out;
}

QString htmlEscape( const QString &text )
{
    QString out = text;
    out.replace( QLatin1Char( '&' ), QStringLiteral( "&amp;" ) );
    out.replace( QLatin1Char( '<' ), QStringLiteral( "&lt;" ) );
    out.replace( QLatin1Char( '>' ), QStringLiteral( "&gt;" ) );
    out.replace( QLatin1Char( '"' ), QStringLiteral( "&quot;" ) );
    return out;
}

// --- shared readbacks ---------------------------------------------------------

QJsonObject headerOf( const QJsonObject &document )
{
    return document.value( QStringLiteral( "header" ) ).toObject();
}

QJsonArray stepsOf( const QJsonObject &document )
{
    return document.value( QStringLiteral( "steps" ) ).toArray();
}

QJsonArray runsOf( const QJsonObject &document )
{
    return document.value( QStringLiteral( "runs" ) ).toArray();
}

} // namespace

// --- Markdown -----------------------------------------------------------------

Result<QString> labReportMarkdown( const QJsonObject &document )
{
    const auto validated = LabReportBuilder::validate( document );
    if ( !validated )
        return Result<QString>::failure( validated.diagnostics() );

    const QJsonObject header = headerOf( document );
    QString md;
    md += QStringLiteral( "# 实验报告 %1\n\n" ).arg( header.value( "reportId" ).toString() );
    md += QStringLiteral( "- schema: `%1`\n" )
              .arg( document.value( "schema" ).toString() );
    md += QStringLiteral( "- lab: %1 (%2)\n" )
              .arg( header.value( "labName" ).toString(), header.value( "labId" ).toString() );
    md += QStringLiteral( "- student: %1 · session: %2\n" )
              .arg( header.value( "student" ).toString(), header.value( "session" ).toString() );
    md += QStringLiteral( "- generated: %1 · software: %2 · git: %3\n\n" )
              .arg( header.value( "generatedAtUtc" ).toString(),
                    header.value( "softwareRevision" ).toString(),
                    header.value( "gitSha" ).toString() );
    const QString objective = header.value( "objective" ).toString();
    if ( !objective.isEmpty() )
        md += QStringLiteral( "> %1\n\n" ).arg( objective );

    md += QStringLiteral( "## 实验运行 (runs)\n\n" );
    for ( const QJsonValue &value : runsOf( document ) )
    {
        const QJsonObject run = value.toObject();
        md += QStringLiteral( "### %1 — %2\n\n" )
                  .arg( run.value( "runId" ).toString(), run.value( "status" ).toString() );
        md += QStringLiteral( "- algorithm: %1 · executionRef: `%2`\n" )
                  .arg( run.value( "algorithmId" ).toString(),
                        run.value( "executionRef" ).toString() );
        md += QStringLiteral( "- window: %1 → %2\n" )
                  .arg( run.value( "startedAtUtc" ).toString(),
                        run.value( "finishedAtUtc" ).toString() );
        md += QStringLiteral( "- configHash: `%1`\n- executionFingerprint: `%2`\n- "
                              "resultFingerprint: `%3`\n" )
                  .arg( run.value( "configHash" ).toString(),
                        run.value( "executionFingerprint" ).toString(),
                        run.value( "resultFingerprint" ).toString() );
        const QJsonObject pins = QJsonObject{
            { QStringLiteral( "datasetVersionId" ),
              run.value( QStringLiteral( "datasetVersionId" ) ) },
            { QStringLiteral( "datasetFingerprint" ),
              run.value( QStringLiteral( "datasetFingerprint" ) ) },
            { QStringLiteral( "splitManifestId" ),
              run.value( QStringLiteral( "splitManifestId" ) ) },
            { QStringLiteral( "splitFingerprint" ),
              run.value( QStringLiteral( "splitFingerprint" ) ) },
            { QStringLiteral( "modelId" ), run.value( QStringLiteral( "modelId" ) ) },
            { QStringLiteral( "seed" ), run.value( QStringLiteral( "seed" ) ) },
        };
        md += QStringLiteral( "- pins: %1\n" ).arg( compactJson( QJsonValue( pins ) ) );
        for ( const QJsonValue &artifact : run.value( "artifacts" ).toArray() )
        {
            const QJsonObject entry = artifact.toObject();
            md += QStringLiteral( "- artifact: `%1` (role %2, size %3, digest `%4`)\n" )
                      .arg( entry.value( "path" ).toString(),
                            entry.value( "role" ).toString( QStringLiteral( "-" ) ),
                            QString::number( entry.value( "sizeBytes" ).toDouble( -1 ), 'f', 0 ),
                            entry.value( "digest" ).toString( QStringLiteral( "-" ) ) );
        }
        md += QLatin1Char( '\n' );
    }

    md += QStringLiteral( "## 操作步骤 (steps)\n\n" );
    md += QStringLiteral( "| # | operator | success | started | durationMs | attributed run | "
                          "quality |\n|---|---|---|---|---|---|---|\n" );
    for ( const QJsonValue &value : stepsOf( document ) )
    {
        const QJsonObject step = value.toObject();
        const QJsonObject attribution = step.value( "attribution" ).toObject();
        md += QStringLiteral( "| %1 | %2 | %3 | %4 | %5 | %6 | %7 |\n" )
                  .arg( QString::number( step.value( "index" ).toInt() ),
                        mdEscape( step.value( "operator" ).toString() ),
                        step.value( "success" ).toBool() ? QStringLiteral( "✔" )
                                                          : QStringLiteral( "✘" ),
                        step.value( "startedAtIso" ).toString(),
                        QString::number( step.value( "durationMs" ).toDouble(), 'f', 1 ),
                        attribution.value( "runId" ).toString( QStringLiteral( "—" ) ),
                        attribution.value( "quality" ).toString() );
    }
    md += QLatin1Char( '\n' );
    for ( const QJsonValue &value : stepsOf( document ) )
    {
        const QJsonObject step = value.toObject();
        md += QStringLiteral( "<details><summary>step %1 — %2</summary>\n\n" )
                  .arg( QString::number( step.value( "index" ).toInt() ),
                        mdEscape( step.value( "operator" ).toString() ) );
        md += QStringLiteral( "```json\n%1\n```\n\n" )
                  .arg( compactJson( step.value( QStringLiteral( "params" ) ) ) );
        const QJsonValue result = step.value( QStringLiteral( "result" ) );
        if ( !result.isNull() && result.type() != QJsonValue::Undefined )
            md += QStringLiteral( "```json\n%1\n```\n\n" ).arg( compactJson( result ) );
        md += QStringLiteral( "</details>\n\n" );
    }

    md += QStringLiteral( "## 统计 (statistics)\n\n" );
    const QJsonArray statistics = document.value( QStringLiteral( "statistics" ) ).toArray();
    if ( statistics.isEmpty() )
        md += QStringLiteral( "_none_\n\n" );
    for ( const QJsonValue &value : statistics )
        md += QStringLiteral( "```json\n%1\n```\n\n" ).arg( compactJson( value ) );

    md += QStringLiteral( "## 成绩 (grade)\n\n" );
    const QJsonObject grade = document.value( QStringLiteral( "grade" ) ).toObject();
    if ( grade.value( "status" ).toString() == QLatin1String( "recorded" ) )
    {
        md += QStringLiteral( "recorded — ref `%1`\n\n" )
                  .arg( grade.value( "gradingRef" ).toString() );
        md += QStringLiteral( "```json\n%1\n```\n\n" )
                  .arg( compactJson( grade.value( QStringLiteral( "inline" ) ) ) );
    }
    else
    {
        md += QStringLiteral( "**unavailable** — %1\n\n" )
                  .arg( grade.value( "reason" ).toString() );
    }

    md += QStringLiteral( "## 溯源 (lineage)\n\n" );
    const QJsonObject lineage = document.value( QStringLiteral( "lineage" ) ).toObject();
    md += QStringLiteral( "- start: %1:`%2` · existence: %3\n\n" )
              .arg( lineage.value( "startKind" ).toString(),
                    lineage.value( "startId" ).toString(),
                    lineage.value( "existence" ).toString() );
    for ( const QJsonValue &value : lineage.value( "edges" ).toArray() )
    {
        const QJsonObject edge = value.toObject();
        md += QStringLiteral( "- `%1:%2` --(%3)--> `%4:%5`\n" )
                  .arg( edge.value( "from_kind" ).toString(), edge.value( "from_id" ).toString(),
                        edge.value( "edge" ).toString(), edge.value( "to_kind" ).toString(),
                        edge.value( "to_id" ).toString() );
    }
    md += QLatin1Char( '\n' );

    md += QStringLiteral( "## 环境回放 (replay)\n\n" );
    const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
    md += QStringLiteral( "- level: **%1**\n" ).arg( replay.value( "level" ).toString() );
    for ( const QJsonValue &value : replay.value( "blockers" ).toArray() )
        md += QStringLiteral( "- blocker: %1\n" ).arg( value.toString() );
    for ( const QJsonValue &value : replay.value( "checks" ).toArray() )
    {
        const QJsonObject check = value.toObject();
        md += QStringLiteral( "- %1: %2 %3\n" )
                  .arg( check.value( "dependency" ).toString(),
                        check.value( "status" ).toString(),
                        check.value( "detail" ).toString() );
    }
    md += QLatin1Char( '\n' );

    md += QStringLiteral( "## 环境 (environment)\n\n" );
    const QJsonObject environment = document.value( QStringLiteral( "environment" ) ).toObject();
    md += QStringLiteral( "```json\n%1\n```\n\n" )
              .arg( compactJson( environment.value( QStringLiteral( "fields" ) ) ) );

    md += QStringLiteral( "## 缩略图 (thumbnails)\n\n" );
    for ( const QJsonValue &value :
          document.value( QStringLiteral( "thumbnails" ) ).toArray() )
    {
        const QJsonObject thumbnail = value.toObject();
        if ( thumbnail.contains( QStringLiteral( "renderError" ) ) )
        {
            md += QStringLiteral( "- %1: render failed — %2\n" )
                      .arg( thumbnail.value( "sourcePath" ).toString(),
                            thumbnail.value( "renderError" ).toString() );
            continue;
        }
        md += QStringLiteral( "![%1](%2)\n\n" )
                  .arg( mdEscape( thumbnail.value( "sourcePath" ).toString() ),
                        thumbnail.value( "dataUrl" ).toString() );
    }

    for ( const QJsonValue &value : document.value( QStringLiteral( "warnings" ) ).toArray() )
        md += QStringLiteral( "> warning: %1\n" ).arg( value.toString() );

    return Result<QString>::success( md );
}

// --- HTML -----------------------------------------------------------------------

Result<QString> labReportHtml( const QJsonObject &document )
{
    const auto validated = LabReportBuilder::validate( document );
    if ( !validated )
        return Result<QString>::failure( validated.diagnostics() );

    const QJsonObject header = headerOf( document );
    QString html;
    html += QStringLiteral( "<!DOCTYPE html>\n<html lang=\"zh\">\n<head>\n<meta charset=\"utf-8\">\n" );
    html += QStringLiteral( "<title>实验报告 %1</title>\n" )
                .arg( htmlEscape( header.value( "reportId" ).toString() ) );
    // Single self-contained file: inline A4-friendly print CSS, no external
    // resources, monospace details, page-break hints between runs.
    html += QStringLiteral(
        "<style>\n"
        "@page { size: A4; margin: 18mm 16mm; }\n"
        "body { font-family: 'Noto Sans CJK SC', 'PingFang SC', 'Microsoft YaHei', sans-serif;"
        " font-size: 11pt; color: #1a1a1a; max-width: 178mm; margin: 0 auto; }\n"
        "h1 { font-size: 17pt; border-bottom: 2px solid #333; padding-bottom: 4pt; }\n"
        "h2 { font-size: 13pt; margin-top: 14pt; border-bottom: 1px solid #bbb; }\n"
        "h3 { font-size: 11.5pt; margin-bottom: 2pt; }\n"
        "table { border-collapse: collapse; width: 100%; font-size: 9.5pt; }\n"
        "th, td { border: 0.5pt solid #999; padding: 2pt 4pt; text-align: left;"
        " word-break: break-all; }\n"
        "th { background: #efefef; }\n"
        "code, pre { font-family: 'JetBrains Mono', monospace; font-size: 8.5pt; }\n"
        "pre { background: #f6f6f6; border: 0.5pt solid #ddd; padding: 4pt;"
        " white-space: pre-wrap; word-break: break-all; }\n"
        ".kv { margin: 1pt 0; }\n"
        ".label { color: #555; }\n"
        ".blocker { color: #8a1f11; font-weight: 600; }\n"
        ".ok { color: #14532d; }\n"
        ".unavailable { color: #7a5b00; }\n"
        ".run { break-inside: avoid; }\n"
        "img.thumb { max-width: 45mm; max-height: 45mm; border: 0.5pt solid #999;"
        " margin: 2pt; }\n"
        ".thumbfig { display: inline-block; text-align: center; font-size: 8pt;"
        " break-inside: avoid; }\n"
        "@media print { .pagebreak { break-before: page; } }\n"
        "</style>\n</head>\n<body>\n" );

    html += QStringLiteral( "<h1>实验报告 %1</h1>\n" )
                .arg( htmlEscape( header.value( "reportId" ).toString() ) );
    html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">课程/实验：</span>%1 (%2)</div>\n" )
                .arg( htmlEscape( header.value( "labName" ).toString() ),
                      htmlEscape( header.value( "labId" ).toString() ) );
    html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">学生：</span>%1"
                            "　<span class=\"label\">session：</span>%2</div>\n" )
                .arg( htmlEscape( header.value( "student" ).toString() ),
                      htmlEscape( header.value( "session" ).toString() ) );
    html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">生成时间：</span>%1"
                            "　<span class=\"label\">软件版本：</span>%2"
                            "　<span class=\"label\">git：</span><code>%3</code></div>\n" )
                .arg( htmlEscape( header.value( "generatedAtUtc" ).toString() ),
                      htmlEscape( header.value( "softwareRevision" ).toString() ),
                      htmlEscape( header.value( "gitSha" ).toString() ) );
    const QString objective = header.value( "objective" ).toString();
    if ( !objective.isEmpty() )
        html += QStringLiteral( "<p><em>%1</em></p>\n" ).arg( htmlEscape( objective ) );

    html += QStringLiteral( "<h2>1. 实验运行</h2>\n" );
    for ( const QJsonValue &value : runsOf( document ) )
    {
        const QJsonObject run = value.toObject();
        html += QStringLiteral( "<div class=\"run\"><h3>%1 · %2</h3>\n" )
                    .arg( htmlEscape( run.value( "runId" ).toString() ),
                          htmlEscape( run.value( "status" ).toString() ) );
        html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">算法：</span>%1"
                                "　<span class=\"label\">执行引用：</span><code>%2</code></div>\n" )
                    .arg( htmlEscape( run.value( "algorithmId" ).toString() ),
                          htmlEscape( run.value( "executionRef" ).toString() ) );
        html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">时间窗：</span>%1 → %2</div>\n" )
                    .arg( htmlEscape( run.value( "startedAtUtc" ).toString() ),
                          htmlEscape( run.value( "finishedAtUtc" ).toString() ) );
        html += QStringLiteral(
                    "<div class=\"kv\"><span class=\"label\">指纹：</span>"
                    "config <code>%1</code> · execution <code>%2</code> · result <code>%3</code></div>\n" )
                    .arg( htmlEscape( run.value( "configHash" ).toString() ),
                          htmlEscape( run.value( "executionFingerprint" ).toString() ),
                          htmlEscape( run.value( "resultFingerprint" ).toString() ) );
        // Identity pins (parity with the Markdown rendering: the same facts
        // in every format).
        html += QStringLiteral(
                    "<div class=\"kv\"><span class=\"label\">pins：</span>"
                    "dataset <code>%1</code> (fp <code>%2</code>) · split <code>%3</code>"
                    " (fp <code>%4</code>) · model <code>%5</code> · seed %6</div>\n" )
                    .arg( htmlEscape(
                              run.value( QStringLiteral( "datasetVersionId" ) )
                                  .toString( QStringLiteral( "-" ) ) ),
                          htmlEscape(
                              run.value( QStringLiteral( "datasetFingerprint" ) )
                                  .toString( QStringLiteral( "-" ) ) ),
                          htmlEscape(
                              run.value( QStringLiteral( "splitManifestId" ) )
                                  .toString( QStringLiteral( "-" ) ) ),
                          htmlEscape(
                              run.value( QStringLiteral( "splitFingerprint" ) )
                                  .toString( QStringLiteral( "-" ) ) ),
                          htmlEscape(
                              run.value( QStringLiteral( "modelId" ) ).toString( QStringLiteral( "-" ) ) ),
                          QString::number( run.value( QStringLiteral( "seed" ) ).toDouble(),
                                           'f', 0 ) );
        const QJsonArray artifacts = run.value( "artifacts" ).toArray();
        if ( !artifacts.isEmpty() )
        {
            html += QStringLiteral( "<table><tr><th>产物</th><th>role</th><th>size</th>"
                                    "<th>digest</th></tr>\n" );
            for ( const QJsonValue &artifact : artifacts )
            {
                const QJsonObject entry = artifact.toObject();
                html += QStringLiteral( "<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>\n" )
                            .arg( htmlEscape( entry.value( "path" ).toString() ),
                                  htmlEscape(
                                      entry.value( "role" ).toString( QStringLiteral( "-" ) ) ),
                                  QString::number( entry.value( "sizeBytes" ).toDouble( -1 ),
                                                   'f', 0 ),
                                  htmlEscape(
                                      entry.value( "digest" ).toString( QStringLiteral( "-" ) ) ) );
            }
            html += QStringLiteral( "</table>\n" );
        }
        html += QStringLiteral( "</div>\n" );
    }

    html += QStringLiteral( "<h2>2. 操作步骤</h2>\n" );
    html += QStringLiteral( "<table><tr><th>#</th><th>operator</th><th>成功</th><th>开始</th>"
                            "<th>耗时(ms)</th><th>归属 run</th><th>归属质量</th></tr>\n" );
    for ( const QJsonValue &value : stepsOf( document ) )
    {
        const QJsonObject step = value.toObject();
        const QJsonObject attribution = step.value( "attribution" ).toObject();
        html += QStringLiteral(
                    "<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td>"
                    "<td>%7</td></tr>\n" )
                    .arg( QString::number( step.value( "index" ).toInt() ),
                          htmlEscape( step.value( "operator" ).toString() ),
                          step.value( "success" ).toBool() ? QStringLiteral( "✔" )
                                                            : QStringLiteral( "✘" ),
                          htmlEscape( step.value( "startedAtIso" ).toString() ),
                          QString::number( step.value( "durationMs" ).toDouble(), 'f', 1 ),
                          htmlEscape(
                              attribution.value( "runId" ).toString( QStringLiteral( "—" ) ) ),
                          htmlEscape( attribution.value( "quality" ).toString() ) );
    }
    html += QStringLiteral( "</table>\n" );
    html += QStringLiteral( "<p class=\"label\">归属策略：%1 — 步骤记录不携带 run 标识，"
                            "按时间窗归属，见 ADR 0146。</p>\n" )
                .arg( htmlEscape( QString::fromUtf8( kLabStepAttributionPolicy ) ) );
    for ( const QJsonValue &value : stepsOf( document ) )
    {
        const QJsonObject step = value.toObject();
        html += QStringLiteral( "<details><summary>步骤 %1 参数/结果</summary>\n" )
                    .arg( QString::number( step.value( "index" ).toInt() ) );
        html += QStringLiteral( "<pre>%1</pre>\n" )
                    .arg( htmlEscape( compactJson( step.value( QStringLiteral( "params" ) ) ) ) );
        const QJsonValue result = step.value( QStringLiteral( "result" ) );
        if ( !result.isNull() && result.type() != QJsonValue::Undefined )
            html += QStringLiteral( "<pre>%1</pre>\n" ).arg( htmlEscape( compactJson( result ) ) );
        html += QStringLiteral( "</details>\n" );
    }

    html += QStringLiteral( "<h2>3. 统计</h2>\n" );
    const QJsonArray statistics = document.value( QStringLiteral( "statistics" ) ).toArray();
    if ( statistics.isEmpty() )
        html += QStringLiteral( "<p>（无）</p>\n" );
    for ( const QJsonValue &value : statistics )
        html += QStringLiteral( "<pre>%1</pre>\n" ).arg( htmlEscape( compactJson( value ) ) );

    html += QStringLiteral( "<h2>4. 成绩</h2>\n" );
    const QJsonObject grade = document.value( QStringLiteral( "grade" ) ).toObject();
    if ( grade.value( "status" ).toString() == QLatin1String( "recorded" ) )
    {
        html += QStringLiteral( "<p class=\"ok\">已评（ref <code>%1</code>）</p>\n" )
                    .arg( htmlEscape( grade.value( "gradingRef" ).toString() ) );
        html += QStringLiteral( "<pre>%1</pre>\n" )
                    .arg( htmlEscape( compactJson( grade.value( QStringLiteral( "inline" ) ) ) ) );
    }
    else
    {
        html += QStringLiteral( "<p class=\"unavailable\">未评 — %1</p>\n" )
                    .arg( htmlEscape( grade.value( "reason" ).toString() ) );
    }

    html += QStringLiteral( "<h2>5. 溯源链</h2>\n" );
    const QJsonObject lineage = document.value( QStringLiteral( "lineage" ) ).toObject();
    html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">起点：</span>"
                            "<code>%1:%2</code>　<span class=\"label\">存在性：</span>%3</div>\n" )
                .arg( htmlEscape( lineage.value( "startKind" ).toString() ),
                      htmlEscape( lineage.value( "startId" ).toString() ),
                      htmlEscape( lineage.value( "existence" ).toString() ) );
    html += QStringLiteral( "<ul>\n" );
    for ( const QJsonValue &value : lineage.value( "edges" ).toArray() )
    {
        const QJsonObject edge = value.toObject();
        html += QStringLiteral( "<li><code>%1:%2</code> --(%3)--&gt; <code>%4:%5</code></li>\n" )
                    .arg( htmlEscape( edge.value( "from_kind" ).toString() ),
                          htmlEscape( edge.value( "from_id" ).toString() ),
                          htmlEscape( edge.value( "edge" ).toString() ),
                          htmlEscape( edge.value( "to_kind" ).toString() ),
                          htmlEscape( edge.value( "to_id" ).toString() ) );
    }
    html += QStringLiteral( "</ul>\n" );

    html += QStringLiteral( "<h2>6. 可回放性</h2>\n" );
    const QJsonObject replay = document.value( QStringLiteral( "replay" ) ).toObject();
    const QString level = replay.value( "level" ).toString();
    html += QStringLiteral( "<div class=\"kv\"><span class=\"label\">级别：</span><strong>%1"
                            "</strong></div>\n" )
                .arg( htmlEscape( level ) );
    for ( const QJsonValue &value : replay.value( "blockers" ).toArray() )
        html += QStringLiteral( "<div class=\"blocker blocker-line\">阻碍：%1</div>\n" )
                    .arg( htmlEscape( value.toString() ) );
    html += QStringLiteral( "<table><tr><th>依赖</th><th>状态</th><th>说明</th></tr>\n" );
    for ( const QJsonValue &value : replay.value( "checks" ).toArray() )
    {
        const QJsonObject check = value.toObject();
        html += QStringLiteral( "<tr><td>%1</td><td>%2</td><td>%3</td></tr>\n" )
                    .arg( htmlEscape( check.value( "dependency" ).toString() ),
                          htmlEscape( check.value( "status" ).toString() ),
                          htmlEscape( check.value( "detail" ).toString() ) );
    }
    html += QStringLiteral( "</table>\n" );

    html += QStringLiteral( "<h2>7. 环境信息</h2>\n" );
    const QJsonObject environment = document.value( QStringLiteral( "environment" ) ).toObject();
    html += QStringLiteral( "<pre>%1</pre>\n" )
                .arg( htmlEscape( compactJson( environment.value( QStringLiteral( "fields" ) ) ) ) );

    html += QStringLiteral( "<h2>8. 输出缩略图</h2>\n" );
    for ( const QJsonValue &value :
          document.value( QStringLiteral( "thumbnails" ) ).toArray() )
    {
        const QJsonObject thumbnail = value.toObject();
        if ( thumbnail.contains( QStringLiteral( "renderError" ) ) )
        {
            html += QStringLiteral( "<div class=\"unavailable\">%1：渲染失败 — %2</div>\n" )
                        .arg( htmlEscape( thumbnail.value( "sourcePath" ).toString() ),
                              htmlEscape( thumbnail.value( "renderError" ).toString() ) );
            continue;
        }
        html += QStringLiteral(
                    "<div class=\"thumbfig\"><img class=\"thumb\" alt=\"%1\" src=\"%2\">"
                    "<br>%3</div>\n" )
                    .arg( htmlEscape( thumbnail.value( "sourcePath" ).toString() ),
                          thumbnail.value( "dataUrl" ).toString(),
                          htmlEscape( thumbnail.value( "sourcePath" ).toString() ) );
    }

    const QJsonArray warnings = document.value( QStringLiteral( "warnings" ) ).toArray();
    if ( !warnings.isEmpty() )
    {
        html += QStringLiteral( "<h2>附注</h2>\n<ul>\n" );
        for ( const QJsonValue &value : warnings )
            html += QStringLiteral( "<li>%1</li>\n" ).arg( htmlEscape( value.toString() ) );
        html += QStringLiteral( "</ul>\n" );
    }

    html += QStringLiteral( "</body>\n</html>\n" );
    return Result<QString>::success( html );
}

// --- file writing ---------------------------------------------------------------

Result<QStringList> writeLabReportFiles( const QJsonObject &document, const QString &basePath )
{
    // Validate FIRST: an invalid document never reaches the disk.
    const auto validated = LabReportBuilder::validate( document );
    if ( !validated )
        return Result<QStringList>::failure( validated.diagnostics() );

    const QString json = QString::fromUtf8(
        QJsonDocument( document ).toJson( QJsonDocument::Indented ) );
    auto markdown = labReportMarkdown( document );
    if ( !markdown )
        return Result<QStringList>::failure( markdown.diagnostics() );
    auto html = labReportHtml( document );
    if ( !html )
        return Result<QStringList>::failure( html.diagnostics() );

    const QStringList targets{
        basePath + QStringLiteral( ".json" ),
        basePath + QStringLiteral( ".md" ),
        basePath + QStringLiteral( ".html" ),
    };
    const QStringList payloads{ json, markdown.value(), html.value() };

    QStringList written;
    for ( int i = 0; i < targets.size(); ++i )
    {
        QFile file( targets.at( i ) );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        {
            return Result<QStringList>::failure( Diagnostic{
                QStringLiteral( "lab.report_write_failed" ),
                QStringLiteral( "cannot write %1: %2" )
                    .arg( targets.at( i ), file.errorString() ),
                sicnu::dataset::DiagnosticSeverity::Error } );
        }
        file.write( payloads.at( i ).toUtf8() );
        file.close();
        written << targets.at( i );
    }
    return Result<QStringList>::success( written );
}

} // namespace sicnu::experiment
