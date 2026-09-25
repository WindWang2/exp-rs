// src/verify_adapters/bounded_io.h — internal shared IO discipline for the
// real provider adapters. NOT public API.
//
// Every adapter reads caller-supplied paths, so the same hostile-content
// rules apply everywhere: bounded reads (a file that grew past the cap
// between stat and read must not buffer unbounded), bounded JSON parser
// recursion, and UTF-8 path decoding that does not route through a code
// page (the sdk path_policy precedent — std::filesystem's std::string
// constructor throws lazily on MSVC for text without an ANSI mapping).
#pragma once

#include <json/json.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace sicnu::verify_adapters
{

/// Shared document cap — the workflow checkpoint discipline (a probe reads
/// small JSON sidecars; anything past the cap is planted or corrupt).
/// Mirrors kMaxCheckpointDocumentBytes (src/workflow/workflow_limits.h);
/// duplicated because that header drags QtGlobal in and this layer must
/// stay Qt-free.
inline constexpr std::uint64_t kMaxAdapterDocumentBytes = 16ull * 1024ull * 1024ull;

/// Typed read failure. Callers map these onto their own public statuses;
/// the string detail is evidence, never parsed back.
enum class IoFailure
{
    None,
    StatFailed,   ///< cannot even answer existence
    Missing,      ///< nothing at the path
    NotRegular,   ///< exists but is a directory / special file
    OpenFailed,
    ReadError,
    OverCap
};

struct IoResult
{
    IoFailure failure = IoFailure::None;
    std::string text; ///< file content on success
    std::string detail;
};

/// Reads @p path fully but never buffers more than @p capBytes.
IoResult readFileBounded( const std::string &path, std::uint64_t capBytes );

/// Bounded parse of already-read text (jsoncpp CharReader, explicit
/// stackLimit — sidecars and checkpoints are untrusted content). Nullopt on
/// any parse failure.
std::optional<Json::Value> parseJsonBounded( const std::string &text, std::string &error );

/// UTF-8 text -> platform path, the sdk/exprs/path_policy decoding contract.
std::filesystem::path pathFromUtf8( const std::string &text );

/// Existence answered through the same UTF-8 decoding contract (false also
/// when the stat itself fails — callers needing the distinction own it).
bool pathExists( const std::string &path );

} // namespace sicnu::verify_adapters
