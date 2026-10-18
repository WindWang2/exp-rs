// grader_json.cpp — deterministic JSON helpers (canonical leaf form).
#include "grader_json.h"

#include <algorithm>
#include <cstddef>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace sicnu::grader {

namespace {

void escapeStringInto( std::string &out, const std::string &text )
{
    out.push_back( '"' );
    for ( const char ch : text ) {
        switch ( ch ) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if ( static_cast<unsigned char>( ch ) < 0x20 ) {
                char buf[8];
                std::snprintf( buf, sizeof( buf ), "\\u%04x", static_cast<unsigned>( static_cast<unsigned char>( ch ) ) );
                out += buf;
            } else {
                out.push_back( ch );
            }
        }
    }
    out.push_back( '"' );
}

/// Depth ceiling for the canonical walk. Documents arriving through
/// parseJsonStrict are already bounded by the reader's PINNED stackLimit
/// (jsoncpp throws AT the limit, so text nesting tops out at 255; the
/// pin is set there explicitly because jsoncpp's default is
/// version-dependent — 1000 on 1.9.5/1.9.6, 256 on 1.9.8 — and Ubuntu
/// 24.04, the CI image, ships 1.9.5, where an unpinned reader would
/// accept 513–1000-deep text that this walk would then refuse), but
/// the engine is also a programmatic API — value-object authors can hand
/// grade() an evidence bundle whose facts nest arbitrarily deep, and the
/// unbudgeted recursion crashed with a stack overflow (SIGSEGV around
/// 100k levels). 512 is double the parse ceiling, keeping every
/// parse-legal document canonicalizable while staying far from any
/// stack danger; over-budget depth is a typed refusal through the
/// existing GraderError channel.
constexpr int kMaxCanonicalDepth = 512;

bool appendCanonical( const Json::Value &value, std::string &out, GraderError &error,
                      const std::string &path, int depth );

bool appendNumber( const Json::Value &value, std::string &out, GraderError &error, const std::string &path )
{
    const double number = value.asDouble();
    if ( !std::isfinite( number ) ) {
        error = makeError( GraderErrorCode::Internal, "non-finite number cannot be canonicalized", path );
        return false;
    }
    auto spelling = canonicalNumber( number, error );
    if ( !spelling ) {
        if ( error.path.empty() )
            error.path = path;
        return false;
    }
    out += *spelling;
    return true;
}

bool appendCanonical( const Json::Value &value, std::string &out, GraderError &error,
                      const std::string &path, int depth )
{
    if ( depth > kMaxCanonicalDepth ) {
        error = makeError( GraderErrorCode::SchemaShapeInvalid,
                           "canonical json exceeded the depth budget (512 nesting levels)", path );
        return false;
    }
    switch ( value.type() ) {
    case Json::nullValue:
        out += "null";
        return true;
    case Json::booleanValue:
        out += value.asBool() ? "true" : "false";
        return true;
    case Json::intValue:
    case Json::uintValue:
    case Json::realValue:
        return appendNumber( value, out, error, path );
    case Json::stringValue:
        escapeStringInto( out, value.asString() );
        return true;
    case Json::arrayValue: {
        out.push_back( '[' );
        bool first = true;
        for ( const auto &element : value ) {
            if ( !first )
                out.push_back( ',' );
            first = false;
            if ( !appendCanonical( element, out, error, path, depth + 1 ) )
                return false;
        }
        out.push_back( ']' );
        return true;
    }
    case Json::objectValue: {
        // Deterministic member order: sorted by key (code-unit order).
        std::map<std::string, const Json::Value *> sorted;
        for ( const auto &key : value.getMemberNames() )
            sorted.emplace( key, &value[key] );
        out.push_back( '{' );
        bool first = true;
        for ( const auto &entry : sorted ) {
            if ( !first )
                out.push_back( ',' );
            first = false;
            escapeStringInto( out, entry.first );
            out.push_back( ':' );
            if ( !appendCanonical( *entry.second, out, error,
                                   path.empty() ? entry.first : path + "." + entry.first, depth + 1 ) )
                return false;
        }
        out.push_back( '}' );
        return true;
    }
    }
    error = makeError( GraderErrorCode::Internal, "unknown JSON value type", path );
    return false;
}

/// True when the text carries a JSON comment (`//` or `/*` outside a string
/// literal). jsoncpp's own `allowComments = false` is not a dependable guard:
/// jsoncpp 1.9.5 strips comment tokens inside objects/arrays regardless of
/// the setting (upstream fixed that in 1.9.6 via
/// OurReader::readTokenSkippingComments), and Ubuntu 24.04 — the CI image —
/// ships exactly that 1.9.5, so a commented grading document would parse
/// there while a 1.9.6+ libjsoncpp refuses it. The refusal is therefore the
/// grader's own and version-independent. A bare `/` is never a legal JSON
/// token, so anything else the scanner rejects falls through to the reader's
/// own syntax error.
bool hasJsonComment( const std::string &text )
{
    bool inString = false;
    bool escaped = false;
    for ( std::size_t index = 0; index < text.size(); ++index ) {
        const char ch = text[index];
        if ( inString ) {
            if ( escaped ) {
                escaped = false;
            } else if ( ch == '\\' ) {
                escaped = true;
            } else if ( ch == '"' ) {
                inString = false;
            }
            continue;
        }
        if ( ch == '"' ) {
            inString = true;
            escaped = false;
            continue;
        }
        if ( ch != '/' )
            continue;
        if ( index + 1 < text.size() && ( text[index + 1] == '/' || text[index + 1] == '*' ) )
            return true;
    }
    return false;
}

} // namespace

std::optional<std::string> canonicalNumber( double value, GraderError &error )
{
    if ( !std::isfinite( value ) ) {
        error = makeError( GraderErrorCode::Internal, "non-finite number cannot be canonicalized" );
        return std::nullopt;
    }
    if ( value == 0.0 )
        return std::string( "0" ); // also normalizes -0.0

    // std::to_chars is locale-INDEPENDENT by specification: snprintf/strtod
    // would follow LC_NUMERIC and emit ",'-separated canonical text (and
    // therefore divergent digests) under a non-C numeric locale.
    char buf[64];
    if ( value == std::floor( value ) && std::fabs( value ) < 9.007199254740992e15 ) {
        // Integral within int64-safe range: print without a decimal point.
        const auto result = std::to_chars( buf, buf + sizeof( buf ), value, std::chars_format::fixed, 0 );
        if ( result.ec == std::errc() )
            return std::string( buf, result.ptr );
    } else {
        // Shortest form that round-trips back to the same double.
        const auto result = std::to_chars( buf, buf + sizeof( buf ), value );
        if ( result.ec == std::errc() )
            return std::string( buf, result.ptr );
    }
    error = makeError( GraderErrorCode::Internal, "number failed round-trip canonicalization" );
    return std::nullopt;
}

std::optional<std::string> canonicalizeJson( const Json::Value &value, GraderError &error )
{
    std::string out;
    if ( !appendCanonical( value, out, error, {}, 1 ) )
        return std::nullopt;
    return out;
}

std::optional<Json::Value> parseJsonStrict( const std::string &text, GraderError &error )
{
    // Comments are refused here, not delegated to the reader: jsoncpp's
    // allowComments = false only bites on 1.9.6+, and the linked libjsoncpp
    // must not move the doctrine (see hasJsonComment).
    if ( hasJsonComment( text ) ) {
        error = makeError( GraderErrorCode::InvalidJson,
                           "invalid JSON: comments are not allowed in grading documents" );
        return std::nullopt;
    }
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["failIfExtra"] = true;
    builder["strictRoot"] = false;
    builder["allowComments"] = false;
    // Pinned, not inherited: the jsoncpp default stackLimit is
    // version-dependent (1000 on 1.9.5/1.9.6 — the Ubuntu 24.04 CI image's
    // 1.9.5 among them — 256 on 1.9.8). Pinning keeps the parse ceiling
    // (jsoncpp throws AT the limit, so the effective max nesting is 255)
    // below the canonical walk's kMaxCanonicalDepth (512) on every
    // linked version, so a document this reader accepts never fails
    // canonicalization for depth alone.
    builder["stackLimit"] = 256;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    Json::Value doc;
    std::string parseErrors;
    // Same safe-reader discipline as guidance_store / plugin_manifest: the
    // reader can THROW (e.g. "Exceeded stackLimit" on a nesting bomb), and a
    // grading document is untrusted content — the refusal stays typed.
    try
    {
        if ( !reader->parse( text.data(), text.data() + text.size(), &doc, &parseErrors ) ) {
            error = makeError( GraderErrorCode::InvalidJson, "invalid JSON: " + parseErrors );
            return std::nullopt;
        }
    }
    catch ( const Json::Exception &exception )
    {
        error = makeError( GraderErrorCode::InvalidJson, std::string( "invalid JSON: " ) + exception.what() );
        return std::nullopt;
    }
    return doc;
}

} // namespace sicnu::grader
