// src/agent/tool_catalog/surface_redaction.cpp

#include "surface_redaction.h"

#include <QRegularExpression>

namespace sicnu::agent::tool_catalog::redaction {

namespace {

// Each rule owns one shape. Order matters only for readability; the pass is
// a single sweep per rule over the whole text.
struct Rule
{
    QRegularExpression pattern;
    QString replacement;
};

const QList<Rule> &rules()
{
    static const QList<Rule> kRules = {
        // PEM private key blocks (multi-line).
        { QRegularExpression( QStringLiteral(
              "-----BEGIN [A-Z ]*PRIVATE KEY-----[\\s\\S]*?-----END [A-Z ]*PRIVATE KEY-----") ),
          QStringLiteral( "[REDACTED PEM]" ) },
        // Authorization headers.
        { QRegularExpression( QStringLiteral(
              "(?i)authorization\\s*:\\s*\\S+.*") ),
          QStringLiteral( "Authorization: [REDACTED]" ) },
        // Bare bearer/JWT tokens.
        { QRegularExpression( QStringLiteral(
              "(?i)\\bbearer\\s+[A-Za-z0-9._\\-]+") ),
          QStringLiteral( "Bearer [REDACTED]" ) },
        // scheme://user:password@host connection URLs.
        { QRegularExpression( QStringLiteral(
              "\\b[a-zA-Z][a-zA-Z0-9+.-]*://[^:/\\s@]+:([^@\\s/]+)@") ),
          QStringLiteral( "[REDACTED-URL]@") },
        // Sensitive-name assignments: api_key=…, password: …, secret-token=…
        { QRegularExpression( QStringLiteral(
              "(?i)\\b(api[_-]?key|apikey|access[_-]?key|secret[_-]?key|secret|password|passwd|pwd|token|client[_-]?secret)\\b(\\s*[=:]\\s*)(\"?[A-Za-z0-9._+/{\\-=]+\"?)") ),
          QStringLiteral( "\\1\\2[REDACTED]" ) },
    };
    return kRules;
}

} // namespace

QString redact( const QString &text )
{
    QString result = text;
    const QList<Rule> &kRules = rules();
    for ( const Rule &rule : kRules )
        result.replace( rule.pattern, rule.replacement );
    return result;
}

std::string redactText( const std::string &text )
{
    return redact( QString::fromStdString( text ) ).toStdString();
}

} // namespace sicnu::agent::tool_catalog::redaction
