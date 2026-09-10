// src/agent/cartography/typography.cpp
#include "typography.h"

#include <algorithm>
#include <cmath>

namespace sicnu::agent::cartography {

namespace {

// CJK fullwidth ranges (modeled after the estimator's "CJK fullwidth" class:
// ideographs, kana, Hangul syllables, fullwidth forms).
bool inRange( char32_t cp, char32_t lo, char32_t hi )
{
  return cp >= lo && cp <= hi;
}

} // namespace

bool isFullwidthCodepoint( char32_t cp )
{
  return inRange( cp, 0x1100, 0x115F )    // Hangul Jamo
         || inRange( cp, 0x2E80, 0x303E ) // CJK radicals, punctuation
         || inRange( cp, 0x3041, 0x33FF ) // kana + CJK symbols
         || inRange( cp, 0x3400, 0x4DBF ) // CJK ext A
         || inRange( cp, 0x4E00, 0x9FFF ) // CJK unified
         || inRange( cp, 0xA000, 0xA4CF ) // Yi
         || inRange( cp, 0xAC00, 0xD7A3 ) // Hangul syllables
         || inRange( cp, 0xF900, 0xFAFF ) // CJK compat ideographs
         || inRange( cp, 0xFE30, 0xFE4F ) // CJK compat forms
         || inRange( cp, 0xFF00, 0xFF60 ) // fullwidth forms
         || inRange( cp, 0xFFE0, 0xFFE6 )
         || inRange( cp, 0x20000, 0x2FFFD ) // ext B..
         || inRange( cp, 0x30000, 0x3FFFD );
}

bool isClosingPunctuation( char32_t cp )
{
  switch ( cp )
  {
    case 0x3001: // 、
    case 0x3002: // 。
    case 0x300D: // 」
    case 0x300F: // 』
    case 0x3011: // 】
    case 0x3015: // 〔 reversed — closing angle
    case 0x3017: // 〗
    case 0x3019: // 〙
    case 0x301B: // 〛
    case 0xFF01: // ！
    case 0xFF09: // ）
    case 0xFF0C: // ，
    case 0xFF1A: // ：
    case 0xFF1B: // ；
    case 0xFF1F: // ？
    case 0xFF5D: // ｝
      return true;
    default:
      return cp == '!' || cp == ',' || cp == '.' || cp == ':' || cp == ';' || cp == '?' ||
             cp == ')' || cp == ']' || cp == '}' || cp == '"' || cp == '\'';
  }
}

bool isOpeningPunctuation( char32_t cp )
{
  switch ( cp )
  {
    case 0x300C: // 「
    case 0x300E: // 『
    case 0x3010: // 【
    case 0x3014: // 〔
    case 0x3016: // 〖
    case 0x3018: // 〘
    case 0x301A: // 〚
    case 0xFF08: // （
    case 0xFF5B: // ｛
      return true;
    default:
      return cp == '(' || cp == '[' || cp == '{' || cp == '\'' || cp == '"';
  }
}

double codepointAdvanceEm( char32_t cp )
{
  if ( isFullwidthCodepoint( cp ) )
    return 1.0;
  if ( cp == ' ' )
    return 0.35;
  return 0.55;
}

char32_t decodeUtf8( const std::string &text, size_t &pos )
{
  if ( pos >= text.size() )
    return 0;
  const unsigned char lead = static_cast<unsigned char>( text[pos] );
  if ( lead < 0x80 )
  {
    ++pos;
    return lead;
  }
  int length = 0;
  char32_t cp = 0;
  if ( ( lead & 0xE0 ) == 0xC0 )
  {
    length = 2;
    cp = lead & 0x1F;
  }
  else if ( ( lead & 0xF0 ) == 0xE0 )
  {
    length = 3;
    cp = lead & 0x0F;
  }
  else if ( ( lead & 0xF8 ) == 0xF0 )
  {
    length = 4;
    cp = lead & 0x07;
  }
  else
  {
    ++pos; // stray continuation or invalid lead byte
    return 0xFFFD;
  }
  if ( pos + length > text.size() )
  {
    ++pos;
    return 0xFFFD;
  }
  for ( int i = 1; i < length; ++i )
  {
    const unsigned char cont = static_cast<unsigned char>( text[pos + i] );
    if ( ( cont & 0xC0 ) != 0x80 )
    {
      ++pos; // invalid sequence: degrade on the lead byte only
      return 0xFFFD;
    }
    cp = ( cp << 6 ) | ( cont & 0x3F );
  }
  pos += length;
  return cp;
}

double measureLineMm( const std::string &text, double sizePt )
{
  double em = 0.0;
  for ( size_t i = 0; i < text.size(); )
  {
    em += codepointAdvanceEm( decodeUtf8( text, i ) );
  }
  return em * sizePt * kMmPerPoint;
}

namespace {

/// Split on hard \n (bounded by kMaxWrappedLines paragraphs).
std::vector<std::string> splitHardLines( const std::string &text )
{
  std::vector<std::string> paragraphs;
  std::string current;
  for ( const char c : text )
  {
    if ( c == '\n' )
    {
      paragraphs.push_back( current );
      current.clear();
      if ( static_cast<int>( paragraphs.size() ) >= kMaxWrappedLines )
        return paragraphs;
    }
    else
    {
      current.push_back( c );
    }
  }
  paragraphs.push_back( current );
  return paragraphs;
}

/// Codepoint-level segmentation positions of a paragraph: every breakable
/// gap index (0 < gap < length). Latin word runs stay unbroken; CJK glyph
/// gaps are breakable unless kinsoku forbids the pair.
std::vector<size_t> breakableGaps( const std::vector<char32_t> &cps )
{
  std::vector<size_t> gaps;
  const int n = static_cast<int>( cps.size() );
  int i = 0;
  while ( i < n )
  {
    if ( cps[i] == ' ' )
    {
      // Breakable after whitespace (greedy word wrap consumes the space).
      gaps.push_back( i + 1 );
      ++i;
      continue;
    }
    if ( isFullwidthCodepoint( cps[i] ) || static_cast<unsigned>( cps[i] ) > 0x2E80 )
    {
      // CJK-ish glyph: the following gap is breakable unless the next
      // glyph is forbidden at line start or this one is forbidden at line
      // end (kinsoku).
      // A gap after glyph i is forbidden when glyph i is an opening mark
      // (the line must not end on it) or glyph i+1 is a closing mark (the
      // line must not end before it) — kinsoku.
      if ( i + 1 < n && !isOpeningPunctuation( cps[i] ) && !isClosingPunctuation( cps[i + 1] ) )
        gaps.push_back( i + 1 );
      ++i;
      continue;
    }
    // Latin word: skip to the word end; the gap after the word (before the
    // next space) is breakable.
    while ( i < n && cps[i] != ' ' && !isFullwidthCodepoint( cps[i] ) &&
            static_cast<unsigned>( cps[i] ) <= 0x2E80 )
      ++i;
    if ( i < n && cps[i] != ' ' )
    {
      // Word runs into CJK: gap between the two is breakable.
      gaps.push_back( i );
    }
    else if ( i < n )
    {
      gaps.push_back( i );
    }
  }
  std::sort( gaps.begin(), gaps.end() );
  gaps.erase( std::unique( gaps.begin(), gaps.end() ), gaps.end() );
  return gaps;
}

/// Greedy wrap of ONE paragraph (no \n) to maxWidthMm at sizePt.
std::vector<std::string> wrapParagraph( const std::string &text, double maxWidthMm,
                                        double sizePt, bool *hitBudget )
{
  std::vector<std::string> lines;
  if ( text.empty() )
  {
    lines.push_back( text );
    return lines;
  }
  std::vector<char32_t> cps;
  for ( size_t i = 0; i < text.size(); )
    cps.push_back( decodeUtf8( text, i ) );
  const std::vector<size_t> gaps = breakableGaps( cps );

  const auto encode = [ &cps ]( int begin, int end ) {
    std::string out;
    for ( int i = begin; i < end; ++i )
    {
      if ( cps[i] <= 0x7F )
        out.push_back( static_cast<char>( cps[i] ) );
      else if ( cps[i] <= 0x7FF )
      {
        out.push_back( static_cast<char>( 0xC0 | ( cps[i] >> 6 ) ) );
        out.push_back( static_cast<char>( 0x80 | ( cps[i] & 0x3F ) ) );
      }
      else if ( cps[i] <= 0xFFFF )
      {
        out.push_back( static_cast<char>( 0xE0 | ( cps[i] >> 12 ) ) );
        out.push_back( static_cast<char>( 0x80 | ( ( cps[i] >> 6 ) & 0x3F ) ) );
        out.push_back( static_cast<char>( 0x80 | ( cps[i] & 0x3F ) ) );
      }
      else
      {
        out.push_back( static_cast<char>( 0xF0 | ( cps[i] >> 18 ) ) );
        out.push_back( static_cast<char>( 0x80 | ( ( cps[i] >> 12 ) & 0x3F ) ) );
        out.push_back( static_cast<char>( 0x80 | ( ( cps[i] >> 6 ) & 0x3F ) ) );
        out.push_back( static_cast<char>( 0x80 | ( cps[i] & 0x3F ) ) );
      }
    }
    return out;
  };

  int begin = 0;
  size_t gapIndex = 0;
  while ( begin < static_cast<int>( cps.size() ) )
  {
    if ( static_cast<int>( lines.size() ) >= kMaxWrappedLines )
    {
      if ( hitBudget )
        *hitBudget = true;
      return lines;
    }
    // Advance the gap cursor past the line begin.
    while ( gapIndex < gaps.size() && gaps[gapIndex] <= static_cast<size_t>( begin ) )
      ++gapIndex;
    // Whole remainder fits: emit it as the final line (the greedy gap scan
    // only sees gaps BEFORE the end and would over-split the tail).
    if ( measureLineMm( encode( begin, static_cast<int>( cps.size() ) ), sizePt ) <= maxWidthMm )
    {
      lines.push_back( encode( begin, static_cast<int>( cps.size() ) ) );
      return lines;
    }
    // Greedy: take the farthest breakable gap that still fits.
    int best = -1;
    size_t cursor = gapIndex;
    while ( cursor < gaps.size() )
    {
      const int end = static_cast<int>( gaps[cursor] );
      // Trim a single trailing space at the break point (word wrap).
      int trimEnd = end;
      while ( trimEnd > begin + 1 && cps[trimEnd - 1] == ' ' )
        --trimEnd;
      if ( measureLineMm( encode( begin, trimEnd ), sizePt ) <= maxWidthMm )
      {
        best = cursor;
        ++cursor;
      }
      else
        break;
    }
    if ( best >= 0 )
    {
      const int end = static_cast<int>( gaps[best] );
      int trimEnd = end;
      while ( trimEnd > begin + 1 && cps[trimEnd - 1] == ' ' )
        --trimEnd;
      lines.push_back( encode( begin, trimEnd ) );
      begin = end;
      gapIndex = best + 1;
    }
    else
    {
      // No breakable gap fits: hard-break at the glyph that overflows (or
      // take the whole rest when it fits on the line).
      int end = begin + 1;
      while ( end < static_cast<int>( cps.size() ) &&
              measureLineMm( encode( begin, end ), sizePt ) <= maxWidthMm )
        ++end;
      if ( end == begin + 1 && end < static_cast<int>( cps.size() ) &&
           measureLineMm( encode( begin, end ), sizePt ) > maxWidthMm && lines.empty() )
      {
        // Single glyph wider than the box: still emit it (degrade honestly).
      }
      lines.push_back( encode( begin, end ) );
      begin = end;
      gapIndex = 0;
    }
  }
  if ( lines.empty() )
    lines.push_back( text );
  return lines;
}

} // namespace

std::vector<std::string> wrapTextMm( const std::string &text, double maxWidthMm, double sizePt )
{
  std::vector<std::string> lines;
  bool hitBudget = false;
  for ( const std::string &paragraph : splitHardLines( text ) )
  {
    if ( static_cast<int>( lines.size() ) >= kMaxWrappedLines )
    {
      hitBudget = true;
      break;
    }
    for ( std::string &line : wrapParagraph( paragraph, maxWidthMm, sizePt, &hitBudget ) )
      lines.push_back( std::move( line ) );
  }
  ( void )hitBudget;
  return lines;
}

bool isTextFitPolicy( const std::string &policy )
{
  return policy == "none" || policy == "ellipsis" || policy == "shrink_to_fit" ||
         policy == "overflow_report";
}

namespace {

double widestLineMm( const std::vector<std::string> &lines, double sizePt )
{
  double widest = 0.0;
  for ( const std::string &line : lines )
    widest = std::max( widest, measureLineMm( line, sizePt ) );
  return widest;
}

double lineHeightMm( double sizePt, double factor )
{
  return sizePt * kMmPerPoint * ( factor > 0 ? factor : kDefaultLineHeightFactor );
}

} // namespace

TextFitReport fitTextIntoBox( const TextFitRequest &request )
{
  TextFitReport report;
  report.policyApplied = isTextFitPolicy( request.policy ) ? request.policy : "overflow_report";
  const double usableWidth = std::max( 0.0, request.boxWidthMm - 2.0 * request.paddingMm );
  const double usableHeight = std::max( 0.0, request.boxHeightMm - 2.0 * request.paddingMm );
  const double declaredPt =
    request.fontPt > 0 ? request.fontPt : 9.0;

  // shrink_to_fit: largest font in [min, maxFont] whose wrapped layout fits.
  if ( report.policyApplied == "shrink_to_fit" )
  {
    const double minPt = std::min( request.fontPtMin > 0 ? request.fontPtMin : declaredPt,
                                   declaredPt );
    const double maxFont = request.fontPtMax > 0 ? std::max( request.fontPtMax, declaredPt )
                                                 : declaredPt;
    double chosen = minPt;
    bool fits = false;
    double lo = minPt;
    double hi = maxFont;
    for ( int i = 0; i < kMaxFitIterations; ++i )
    {
      const double mid = 0.5 * ( lo + hi );
      const std::vector<std::string> lines =
        wrapTextMm( request.text, usableWidth, mid );
      const double height = lines.size() * lineHeightMm( mid, request.lineHeightFactor );
      if ( widestLineMm( lines, mid ) <= usableWidth + 1e-9 && height <= usableHeight + 1e-9 )
      {
        chosen = mid;
        fits = true;
        lo = mid; // try larger
      }
      else
      {
        hi = mid;
      }
    }
    // Also accept the declared size itself when it fits outright.
    {
      const std::vector<std::string> lines = wrapTextMm( request.text, usableWidth, declaredPt );
      const double height = lines.size() * lineHeightMm( declaredPt, request.lineHeightFactor );
      if ( widestLineMm( lines, declaredPt ) <= usableWidth + 1e-9 &&
           height <= usableHeight + 1e-9 )
      {
        chosen = declaredPt;
        fits = true;
      }
    }
    report.fontPt = fits ? chosen : minPt;
    report.fontPolicy = fits ? "shrunk" : "floor";
    report.lines = wrapTextMm( request.text, usableWidth, report.fontPt );
    report.usedWidthMm = widestLineMm( report.lines, report.fontPt );
    report.usedHeightMm = report.lines.size() * lineHeightMm( report.fontPt, request.lineHeightFactor );
    report.fits = fits;
    if ( !fits )
    {
      report.overflowWidthMm = std::max( 0.0, report.usedWidthMm - usableWidth );
      report.overflowHeightMm = std::max( 0.0, report.usedHeightMm - usableHeight );
      report.diagnostics.push_back(
        "shrink_to_fit reached the font floor (" + std::to_string( minPt ) +
        " pt); the text still overflows the box" );
    }
    else if ( report.fontPt < declaredPt - 1e-9 )
    {
      report.diagnostics.push_back( "font shrunk from " + std::to_string( declaredPt ) +
                                    " pt to " + std::to_string( report.fontPt ) +
                                    " pt to fit the box" );
    }
    return report;
  }

  // Declared-font policies: wrap at fontPt.
  report.fontPt = declaredPt;
  report.fontPolicy = "declared";
  report.lines = wrapTextMm( request.text, usableWidth, declaredPt );
  report.usedWidthMm = widestLineMm( report.lines, declaredPt );
  report.usedHeightMm = report.lines.size() * lineHeightMm( declaredPt, request.lineHeightFactor );

  const bool widthFits = report.usedWidthMm <= usableWidth + 1e-9;
  const bool heightFits = report.usedHeightMm <= usableHeight + 1e-9;

  if ( report.policyApplied == "ellipsis" && ( !widthFits || !heightFits ) )
  {
    // Keep the lines that fit, cut the last one with U+2026. At least one
    // line is always kept (a fully invisible text would be a silent drop).
    while ( report.lines.size() > 1 &&
            report.lines.size() * lineHeightMm( declaredPt, request.lineHeightFactor ) >
              usableHeight + 1e-9 )
      report.lines.pop_back();
    std::string &last = report.lines.back();
    while ( !last.empty() &&
            measureLineMm( last + "\xE2\x80\xA6", declaredPt ) > usableWidth + 1e-9 )
      last.pop_back();
    // pop_back may split a UTF-8 sequence — re-decode cleanly.
    while ( !last.empty() )
    {
      const unsigned char tail = static_cast<unsigned char>( last.back() );
      if ( tail >= 0x80 && tail < 0xC0 )
        last.pop_back();
      else
        break;
    }
    if ( !last.empty() )
      last += "\xE2\x80\xA6";
    else
    {
      // The box cannot represent any part of the text, not even one glyph
      // plus an ellipsis — degrade to the ellipsis alone and report the
      // residual overflow honestly (never emit an invisible text).
      last = "\xE2\x80\xA6";
      report.diagnostics.push_back(
        "box smaller than one glyph; only the ellipsis marker is representable" );
    }
    report.truncated = true;
    report.usedWidthMm = widestLineMm( report.lines, declaredPt );
    report.usedHeightMm = report.lines.size() * lineHeightMm( declaredPt, request.lineHeightFactor );
    report.fits = report.usedWidthMm <= usableWidth + 1e-9 &&
                  report.usedHeightMm <= usableHeight + 1e-9;
    report.diagnostics.push_back(
      "text truncated with an ellipsis to fit the box (policy ellipsis)" );
    return report;
  }

  report.fits = widthFits && heightFits;
  report.overflowWidthMm = std::max( 0.0, report.usedWidthMm - usableWidth );
  report.overflowHeightMm = std::max( 0.0, report.usedHeightMm - usableHeight );
  if ( !report.fits )
  {
    report.diagnostics.push_back(
      "text overflows the box by " + std::to_string( report.overflowWidthMm ) + "×" +
      std::to_string( report.overflowHeightMm ) + " mm at " + std::to_string( declaredPt ) +
      " pt (policy " + report.policyApplied + "); nothing is hidden" );
  }
  return report;
}

Json::Value textFitReportToJson( const TextFitReport &report )
{
  Json::Value out( Json::objectValue );
  out["fits"] = report.fits;
  out["font_pt"] = report.fontPt;
  out["font_policy"] = report.fontPolicy;
  out["used_width_mm"] = report.usedWidthMm;
  out["used_height_mm"] = report.usedHeightMm;
  out["overflow_width_mm"] = report.overflowWidthMm;
  out["overflow_height_mm"] = report.overflowHeightMm;
  out["truncated"] = report.truncated;
  out["policy"] = report.policyApplied;
  Json::Value lines( Json::arrayValue );
  for ( const std::string &line : report.lines )
    lines.append( line );
  out["lines"] = lines;
  Json::Value diagnostics( Json::arrayValue );
  for ( const std::string &diagnostic : report.diagnostics )
    diagnostics.append( diagnostic );
  out["diagnostics"] = diagnostics;
  return out;
}

} // namespace sicnu::agent::cartography
