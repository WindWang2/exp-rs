// src/agent/tool_catalog/surface_redaction.h
//
// Surface-boundary redaction: credential- and secret-shaped substrings are
// removed from protocol text (MCP tool error/log text, CLI batch result
// index) before the text reaches a client. Applied greedily — a false
// positive (over-redaction) is tolerated, a false negative is not.
//
// Explicitly NOT redacted: file paths. Paths are the business data of this
// product (artifact references); exposure is governed by the workspace
// sandbox (SICNU_MCP_WORKSPACE), not by text scrubbing.
#pragma once

#include <QString>

#include <string>

namespace sicnu::agent::tool_catalog::redaction {

/// Redacts credential-shaped substrings in @p text:
///  - PEM private-key blocks → "[REDACTED PEM]";
///  - "Authorization: Bearer …" / bare "Bearer <token>" → redacted token;
///  - "key|token|secret|password…=value" and "key: value" assignments →
///    "[REDACTED]" values;
///  - scheme://user:password@host connection URLs → redacted password.
/// Returns the redacted text (input is otherwise unchanged).
QString redact( const QString &text );

/// std::string convenience overload (UTF-8).
std::string redactText( const std::string &text );

} // namespace sicnu::agent::tool_catalog::redaction
