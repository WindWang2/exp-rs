// src/agent_ops/secret_redactor.h
#pragma once

#include <json/json.h>
#include <string>

namespace sicnu::agent_ops {

/// Keys whose values are replaced with "[REDACTED]" (case-insensitive match
/// on the key name). Passwords, tokens, API keys, Authorization headers, etc.
bool isSecretKey(const std::string &key);

/// Deep-copy `in`, redact secret keys, truncate oversized string payloads
/// (sets sibling flags truncated=true, original_chars=N). Preserves
/// `payload.fault` markers untouched for agentbench recovery_quality.
Json::Value redactAndBound(const Json::Value &in, std::size_t maxPayloadChars,
                            bool *anyTruncated = nullptr);

} // namespace sicnu::agent_ops
