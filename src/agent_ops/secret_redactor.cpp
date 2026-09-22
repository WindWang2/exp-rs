// src/agent_ops/secret_redactor.cpp
#include "agent_ops/secret_redactor.h"

#include <algorithm>
#include <cctype>

namespace sicnu::agent_ops {
namespace {

std::string lower(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool keyLooksSecret(const std::string &key)
{
    const std::string k = lower(key);
    static const char *needles[] = {
        "password", "passwd", "secret", "token", "api_key", "apikey",
        "authorization", "auth_header", "private_key", "access_key",
        "refresh_token", "client_secret", "bearer", "credential",
    };
    for (const char *n : needles)
    {
        if (k.find(n) != std::string::npos)
            return true;
    }
    return false;
}

Json::Value redactValue(const Json::Value &in, std::size_t maxChars, bool *anyTruncated,
                        const std::string &parentKey, bool preserveFault)
{
    if (in.isObject())
    {
        Json::Value out(Json::objectValue);
        for (const auto &name : in.getMemberNames())
        {
            if (keyLooksSecret(name))
            {
                out[name] = "[REDACTED]";
                continue;
            }
            // Preserve injected-fault markers exactly (agentbench recovery_quality).
            if (preserveFault && name == "fault")
            {
                out[name] = in[name];
                continue;
            }
            out[name] = redactValue(in[name], maxChars, anyTruncated, name,
                                    /*preserveFault=*/true);
        }
        return out;
    }
    if (in.isArray())
    {
        Json::Value out(Json::arrayValue);
        for (const auto &el : in)
            out.append(redactValue(el, maxChars, anyTruncated, parentKey, preserveFault));
        return out;
    }
    if (in.isString())
    {
        const std::string s = in.asString();
        if (s.size() > maxChars)
        {
            if (anyTruncated)
                *anyTruncated = true;
            Json::Value wrapped(Json::objectValue);
            wrapped["truncated"] = true;
            wrapped["original_chars"] = static_cast<Json::UInt64>(s.size());
            wrapped["value"] = s.substr(0, maxChars);
            return wrapped;
        }
        return in;
    }
    return in;
}

} // namespace

bool isSecretKey(const std::string &key)
{
    return keyLooksSecret(key);
}

Json::Value redactAndBound(const Json::Value &in, std::size_t maxPayloadChars, bool *anyTruncated)
{
    if (anyTruncated)
        *anyTruncated = false;
    return redactValue(in, maxPayloadChars, anyTruncated, "", true);
}

} // namespace sicnu::agent_ops
