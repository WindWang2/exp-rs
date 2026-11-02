// src/agent_ops/repair_approval.cpp
#include "agent_ops/repair_approval.h"

#include "repair_planner/repair_sha256.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace sicnu::agent_ops {
namespace {

std::atomic<long long> gInstanceCounter{0};

std::string intToString(long long value)
{
    return std::to_string(value);
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 (FIPS 198-1) over an in-repo SHA-256. The module stays
// Qt-free by design (headless closed-loop tests), and the repo's shared
// digest helper only exposes hex output — a MAC needs the raw digest bytes
// as the inner hash's input to the outer block, so the standard 32-byte
// core is implemented here once, privately (same algorithm family as
// repair_planner/repair_sha256.cpp; that file is another track's leaf and
// is not modified).
// ---------------------------------------------------------------------------
struct Sha256Core
{
    std::array<uint32_t, 8> state{};
    uint64_t totalBits = 0;
    std::array<unsigned char, 64> buffer{};
    std::size_t buffered = 0;

    static uint32_t rotr(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

    Sha256Core()
    {
        // FIPS 180-4 §5.3.3 initial hash values.
        state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    }

    void processBlock(const unsigned char *p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void absorb(const unsigned char *data, std::size_t len)
    {
        while (len > 0)
        {
            const std::size_t take = std::min(len, std::size_t{64} - buffered);
            std::memcpy(buffer.data() + buffered, data, take);
            buffered += take;
            data += take;
            len -= take;
            if (buffered == 64)
            {
                processBlock(buffer.data());
                buffered = 0;
            }
        }
    }

    void update(const unsigned char *data, std::size_t len)
    {
        totalBits += static_cast<uint64_t>(len) * 8;
        absorb(data, len);
    }

    void final(unsigned char out[32])
    {
        const uint64_t bits = totalBits;
        // FIPS 180-4 §5.1.1 padding — the pad byte, zeros and the 64-bit
        // length NEVER count toward the message length being encoded.
        const unsigned char pad = 0x80;
        absorb(&pad, 1);
        const unsigned char zero = 0;
        while (buffered != 56)
            absorb(&zero, 1);
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i)
            lenBytes[i] = static_cast<unsigned char>((bits >> (56 - 8 * i)) & 0xff);
        std::memcpy(buffer.data() + buffered, lenBytes, 8);
        processBlock(buffer.data());
        buffered = 0;
        for (int i = 0; i < 8; ++i)
        {
            out[i * 4] = static_cast<unsigned char>((state[i] >> 24) & 0xff);
            out[i * 4 + 1] = static_cast<unsigned char>((state[i] >> 16) & 0xff);
            out[i * 4 + 2] = static_cast<unsigned char>((state[i] >> 8) & 0xff);
            out[i * 4 + 3] = static_cast<unsigned char>(state[i] & 0xff);
        }
    }
};

void sha256(const unsigned char *data, std::size_t len, unsigned char out[32])
{
    Sha256Core core;
    core.update(data, len);
    core.final(out);
}

std::array<unsigned char, 32> hmacSha256(const std::string &key, const std::string &message)
{
    // FIPS 198-1: keys longer than the block size are hashed first; shorter
    // keys are zero-padded. Block size 64, output 32.
    std::array<unsigned char, 64> kblock{};
    if (key.size() > 64)
    {
        unsigned char hashed[32];
        sha256(reinterpret_cast<const unsigned char *>(key.data()), key.size(), hashed);
        std::memcpy(kblock.data(), hashed, 32);
    }
    else
    {
        std::memcpy(kblock.data(), key.data(), key.size());
    }
    std::array<unsigned char, 64> ipad{}, opad{};
    for (int i = 0; i < 64; ++i)
    {
        ipad[i] = static_cast<unsigned char>(kblock[i] ^ 0x36);
        opad[i] = static_cast<unsigned char>(kblock[i] ^ 0x5c);
    }
    // inner = SHA256(ipad || message)
    Sha256Core inner;
    inner.update(ipad.data(), 64);
    inner.update(reinterpret_cast<const unsigned char *>(message.data()), message.size());
    unsigned char innerDigest[32];
    inner.final(innerDigest);
    // outer = SHA256(opad || inner)
    Sha256Core outer;
    outer.update(opad.data(), 64);
    outer.update(innerDigest, 32);
    std::array<unsigned char, 32> mac{};
    outer.final(mac.data());
    return mac;
}

std::string hexHalf(const std::vector<unsigned char> &bytes)
{
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes)
    {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xf]);
    }
    return out;
}

/// The canonical binding string the MAC covers. "v2|" separates this MAC
/// domain from the legacy unsigned digest domain (a v1 token has no nonce
/// and fails shape validation, so cross-domain confusion is impossible).
std::string canonicalBinding(const Json::Value &tokenDoc)
{
    return std::string("v2|") + tokenDoc["findings_digest"].asString() + "|" +
           intToString(tokenDoc["coordinator_id"].asInt64()) + "|" +
           intToString(tokenDoc["issued_at_ms"].asInt64()) + "|" +
           intToString(tokenDoc["expires_at_ms"].asInt64()) + "|" +
           tokenDoc["nonce"].asString();
}

std::string approvalMac(const Json::Value &tokenDoc, const std::string &secretHex)
{
    const std::array<unsigned char, 32> mac = hmacSha256(secretHex, canonicalBinding(tokenDoc));
    std::string hex = hexHalf(std::vector<unsigned char>(mac.begin(), mac.end()));
    hex.resize(16);
    return hex;
}

bool isHex(const std::string &text, std::size_t expectChars)
{
    if (text.size() != expectChars)
        return false;
    for (char c : text)
    {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex)
            return false;
    }
    return true;
}

std::string randomHex(std::size_t bytes)
{
    static std::random_device source; // Linux: getrandom(2)/dev/urandom
    std::string hex;
    hex.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i)
    {
        const unsigned char b = static_cast<unsigned char>(source());
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        hex.append(buf, 2);
    }
    return hex;
}

} // namespace

long long nextRepairApprovalInstanceId()
{
    return gInstanceCounter.fetch_add(1) + 1;
}

std::string generateRepairApprovalSecretHex()
{
    return randomHex(32);
}

Json::Value mintRepairApprovalToken(const std::string &findingsDigest,
                                    long long coordinatorId, long long nowMs,
                                    long long ttlMs, const std::string &secretHex)
{
    // Fail closed: an unusable argument mints nothing, never a bare
    // "approved" marker without binding, expiry or authenticity (#1733).
    if (findingsDigest.empty() || coordinatorId <= 0 || nowMs <= 0 || ttlMs <= 0)
        return Json::Value();
    if (ttlMs > kRepairApprovalMaxTtlMs)
        return Json::Value(); // unbounded approvals are refused at the source
    if (secretHex.size() < kRepairApprovalMinSecretHex)
        return Json::Value();
    if (nowMs > std::numeric_limits<long long>::max() - ttlMs)
        return Json::Value(); // window arithmetic would overflow
    Json::Value doc(Json::objectValue);
    doc["kind"] = kRepairApprovalKind;
    doc["schema_version"] = kRepairApprovalSchema;
    doc["findings_digest"] = findingsDigest;
    doc["coordinator_id"] = static_cast<Json::Int64>(coordinatorId);
    doc["issued_at_ms"] = static_cast<Json::Int64>(nowMs);
    doc["expires_at_ms"] = static_cast<Json::Int64>(nowMs + ttlMs);
    doc["nonce"] = randomHex(8);
    doc["digest"] = approvalMac(doc, secretHex);
    return doc;
}

const char *validateRepairApprovalTokenShape(const Json::Value &tokenDoc)
{
    if (!tokenDoc.isObject() || !tokenDoc["kind"].isString() ||
        tokenDoc["kind"].asString() != kRepairApprovalKind ||
        !tokenDoc["schema_version"].isString() ||
        tokenDoc["schema_version"].asString() != kRepairApprovalSchema ||
        !tokenDoc["findings_digest"].isString() || !tokenDoc["coordinator_id"].isInt64() ||
        !tokenDoc["issued_at_ms"].isInt64() || !tokenDoc["expires_at_ms"].isInt64() ||
        !tokenDoc["nonce"].isString() || !tokenDoc["digest"].isString() ||
        !isHex(tokenDoc["digest"].asString(), 16) || !isHex(tokenDoc["nonce"].asString(), 16))
        return approval_check::kMalformed;
    const long long issued = tokenDoc["issued_at_ms"].asInt64();
    const long long expires = tokenDoc["expires_at_ms"].asInt64();
    if (issued <= 0 || expires <= issued)
        return approval_check::kMalformed;
    if (expires - issued > kRepairApprovalMaxTtlMs)
        return approval_check::kTtlExcessive;
    return approval_check::kOk;
}

const char *verifyRepairApprovalToken(const Json::Value &tokenDoc,
                                      const std::string &findingsDigest,
                                      long long coordinatorId, long long nowMs,
                                      const std::string &secretHex)
{
    const char *shape = validateRepairApprovalTokenShape(tokenDoc);
    if (std::string(shape) != approval_check::kOk)
        return shape;

    if (tokenDoc["digest"].asString() != approvalMac(tokenDoc, secretHex))
        return approval_check::kTampered;

    if (tokenDoc["findings_digest"].asString() != findingsDigest)
        return approval_check::kWrongPlan;

    if (tokenDoc["coordinator_id"].asInt64() != coordinatorId)
        return approval_check::kWrongCoordinator;

    // No usable clock means expiry cannot be proven false: the token is
    // treated as expired (fail closed), never as "still valid".
    if (nowMs <= 0 || nowMs > tokenDoc["expires_at_ms"].asInt64())
        return approval_check::kExpired;

    return approval_check::kOk;
}

} // namespace sicnu::agent_ops
