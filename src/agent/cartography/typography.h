// src/agent/cartography/typography.h
#pragma once

//
// Typography engine (Cartography Platform 7.0, package D).
//
// Deterministic, platform-independent text measurement and layout for map
// furniture. The shipped preflight used a single-line width estimator only
// (documented limitation since 5.0): no wrap simulation, no per-line model,
// no truncation policy — the most common automatic-cartography failures
// (clipped titles, overflowed legends, broken CJK line breaks) were invisible
// until QGIS rendered the layout.
//
// This module owns the text model going forward. It keeps the SAME width
// classes as the shipped estimator (fullwidth glyphs one em, spaces 0.35 em,
// everything else 0.55 em; 1 pt = 0.3528 mm) so results stay comparable and
// no font files need to ship, and adds what the estimator lacked:
//
//   * UTF-8 aware measurement (per codepoint, not per byte);
//   * explicit multi-line model (hard \n breaks + greedy word wrap);
//   * CJK line-break rules (no break before closing punctuation, none after
//     opening punctuation, Latin words stay unbroken);
//   * declared truncation policies (none | ellipsis | shrink_to_fit |
//     overflow_report) — overflow is never silent;
//   * bounded shrink-to-fit search over a font range;
//   * a structured fit report consumed by preflight rules (tiny text,
//     clipped title, overflow diagnostics) and by tooling.
//
// Determinism contract: identical inputs produce byte-identical reports on
// every platform. No Qt, no font database, no locale.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

/// Millimeters per point (typographic point definition).
inline constexpr double kMmPerPoint = 0.3528;

/// Default line-height factor (leading) for multi-line furniture text.
inline constexpr double kDefaultLineHeightFactor = 1.25;

/// Maximum wrapped lines the engine will produce from one paragraph — the
/// hard stop that keeps pathological inputs bounded (the rest is reported as
/// truncated, never silently dropped).
inline constexpr int kMaxWrappedLines = 64;

/// Bounded shrink-to-fit binary search iterations (precision ≈ (max-min)/2^k).
inline constexpr int kMaxFitIterations = 12;

/// True when `cp` is a CJK ideograph / fullwidth punctuation codepoint that
/// renders one em wide in the estimator's width classes.
bool isFullwidthCodepoint( char32_t cp );

/// True when `cp` is closing punctuation a line must not END before (CJK
/// kinsoku rules:。，、！？…」』）】》；:" etc.).
bool isClosingPunctuation( char32_t cp );
/// True when `cp` is opening punctuation a line must not START with (CJK
/// kinsoku rules:（「『【 etc.).
bool isOpeningPunctuation( char32_t cp );

/// Width of one codepoint in em units under the deterministic width classes.
double codepointAdvanceEm( char32_t cp );

/// Decodes one UTF-8 codepoint starting at `pos`; advances `pos`. Invalid
/// bytes decode as U+FFFD (replacement char) — measurement never fails or
/// reorders, it degrades deterministically.
char32_t decodeUtf8( const std::string &text, size_t &pos );

/// Width of `text` (single line, no wrapping; explicit \n are NOT special)
/// at `sizePt` in millimeters.
double measureLineMm( const std::string &text, double sizePt );

/// Greedy word wrap to `maxWidthMm` at `sizePt`. Hard \n breaks are honored.
/// CJK text may break between any two fullwidth glyphs except where kinsoku
/// rules forbid; Latin words are never broken mid-word. Returns at most
/// kMaxWrappedLines lines; input producing more is wrapped to that budget
/// (the caller sees truncated=true in the fit report).
std::vector<std::string> wrapTextMm( const std::string &text, double maxWidthMm, double sizePt );

/// Same wrap, reporting whether the kMaxWrappedLines budget truncated the
/// output (consumed by fitTextIntoBox to flag truncation honestly).
std::vector<std::string> wrapTextMmBudgeted( const std::string &text, double maxWidthMm,
                                             double sizePt, bool *lineBudgetHit );

/// Same wrap under a declared line-end break policy (see isTextBreakPolicy);
/// an unknown policy resolves to "none".
std::vector<std::string> wrapTextMmBudgeted( const std::string &text, double maxWidthMm,
                                             double sizePt, bool *lineBudgetHit,
                                             const std::string &breakPolicy );

//
// Platform 8.0 (Typography 2.0): declared CJK line-end composition.
//
// A text item may declare `font.break_policy` governing line-FINAL fullwidth
// closing punctuation (。，」etc. — the U+2026 ellipsis is NOT in the
// fullwidth class and is never compressed):
//   "none"      (default) — the full advance counts; exactly the 7.0 model;
//   "halfwidth" — the line-final fullwidth closing punctuation measures half
//                 its advance (CJK halfwidth compression): more glyphs fit
//                 per line and the wrap/fit reports measure the compressed
//                 line end.
// Mid-line punctuation always measures full width. The model stays
// platform-independent (no font database; byte-identical outputs).
//
inline constexpr double kHalfwidthEndFactor = 0.5;

/// True when `policy` is a known break policy ("none"|"halfwidth").
bool isTextBreakPolicy( const std::string &policy );

/// Declared truncation policy for a text item (MapSpec v4 may carry
/// `text: {policy: ...}`; the default keeps the 6.0 behavior).
///   none            — wrap at the declared font; overflow reported, nothing hidden
///   ellipsis        — shrink nothing; the last visible line is cut with U+2026
///   shrink_to_fit   — largest font in [min_pt, max_pt] whose wrap fits the box
///   overflow_report — wrap at the declared font and report overflow explicitly
bool isTextFitPolicy( const std::string &policy );

struct TextFitRequest
{
    std::string text;
    double boxWidthMm = 0.0;   ///< usable content box
    double boxHeightMm = 0.0;
    double fontPt = 9.0;       ///< declared font size
    double fontPtMin = 6.0;    ///< shrink_to_fit lower bound
    double fontPtMax = 0.0;    ///< 0 = fontPt (no growth)
    double lineHeightFactor = kDefaultLineHeightFactor;
    std::string policy = "overflow_report";
    double paddingMm = 0.0;    ///< subtracted from the box on both axes
    std::string breakPolicy = "none"; ///< Platform 8.0 line-end composition
};

/// Structured result of one fit evaluation. Every field is deterministic.
struct TextFitReport
{
    bool fits = true;              ///< final layout fits the box
    double fontPt = 0.0;           ///< font the layout was computed with
    std::vector<std::string> lines;
    double usedWidthMm = 0.0;      ///< widest line actually used
    double usedHeightMm = 0.0;     ///< lines × sizePt × kMmPerPoint × leading
    double overflowWidthMm = 0.0;  ///< > 0 when the widest line exceeds the box
    double overflowHeightMm = 0.0;
    bool truncated = false;        ///< content dropped (line budget or ellipsis)
    std::string policyApplied;     ///< resolved policy
    std::string fontPolicy;        ///< "declared" | "shrunk"
    std::string breakPolicyApplied = "none"; ///< resolved line-end composition
    std::vector<std::string> diagnostics; ///< human-readable, bounded
};

/// Lays `request.text` out into the box under the declared policy. Pure.
TextFitReport fitTextIntoBox( const TextFitRequest &request );

/// Compact JSON projection for tool responses / preflight issue payloads.
Json::Value textFitReportToJson( const TextFitReport &report );

} // namespace sicnu::agent::cartography
