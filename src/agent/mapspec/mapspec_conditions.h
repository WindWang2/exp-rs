// src/agent/mapspec/mapspec_conditions.h
#pragma once

//
// Bounded declarative condition expressions (Platform 5.0, Milestone F).
//
// MapSpec v3 items may carry `visible_if` / `content_if` and page entries a
// `page_if` condition. Conditions are *not* code: a whitespace-tolerant
// expression grammar with comparisons, boolean and/or, `has(path)` and
// dotted-path operands, resolved against a materialized context object the
// caller builds (bindings, resolved parameters, result artifact counts).
//
// Grammar (bounded):
//   expr    := andExpr ( "or" andExpr )*
//   andExpr := cmp ( "and" cmp )*
//   cmp     := operand OP operand | "(" expr ")"
//   OP      := "==" | "!=" | ">=" | "<=" | ">" | "<"
//   operand := number | "quoted string" | true | false | has(path) | path
//   path    := [a-z_][a-z0-9_]*( [._-][a-z0-9_]+ )*
//
// Bounds: expression length <= 256 chars, <= 64 tokens, parse depth <= 8.
// Unknown paths are evaluation ERRORS (never silently false) so contradictions
// surface in preflight instead of disappearing.
//

#include <json/json.h>

#include <set>
#include <string>
#include <vector>

namespace sicnu::agent::mapspec {

/// Maximum serialized length of one condition expression.
inline constexpr size_t kConditionMaxLength = 256;

/// Syntax-only validation. Returns false with one problem per entry
/// appended on malformed input.
bool validateConditionSyntax( const std::string &expr, std::vector<std::string> *problems );

/// Dotted paths the expression reads (has() and bare paths). Used by
/// validation to reject unknown context keys before evaluation.
std::set<std::string> conditionPaths( const std::string &expr );

/// Evaluates `expr` against a context object (dotted-path lookup; array
/// indexing is intentionally unsupported). On success sets *value and
/// returns true; on failure returns false with *error describing the
/// problem (unknown path, type mismatch, …).
bool evaluateCondition( const std::string &expr, const Json::Value &context, bool *value,
                        std::string *error );

} // namespace sicnu::agent::mapspec
