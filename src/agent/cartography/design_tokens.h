// src/agent/cartography/design_tokens.h
#pragma once

//
// Design token layer (Design System 4.0, Milestone A / ADR 0130).
//
// A token set is a versioned JSON document of named design decisions
// (typography hierarchy, spacing, line weights, semantic colors, palettes,
// furniture metrics, chart defaults) consumed by components, templates, the
// MapSpec compiler and the chart/colorbar renderers. It is deliberately NOT
// a CSS-like styling language: a token set only parameterizes how QGIS
// layout items are created — QgsPrintLayout stays the rendering truth.
//
// Document shape (data/cartography/tokens/<id>.json):
//   { id: "scientific-light", version: 1, description: "…",
//     typography: { font_family, cjk_fallbacks: [], styles: { title: {size_pt, weight}, … } },
//     spacing: { margin_mm, gutter_mm, furniture_gap_mm, padding_mm },
//     lines: { hairline_mm, frame_mm, grid_mm },
//     colors: { text, text_secondary, background, panel, frame, grid,
//               water, accent, success, warning, error, uncertainty },
//     palettes: { qualitative: [], sequential: [], diverging: [], … },
//     furniture: { density, north_arrow_size_mm, scale_bar_height_mm,
//                  legend_line_height_mm, inset_size_fraction },
//     chart: { font_pt, palette, show_grid, width_px, height_px },
//     variants: { screen: { …deep-merged subset… }, print: { … } } }
//
// Resolution (resolveTokenSet): spec.style.token_set (default
// "scientific-light") → base document → deep-merge variants[medium]
// (spec.style.medium, default "print"; the "variants" member itself is
// dropped) → deep-merge spec.style.overrides. Deterministic and pure.
//

#include <json/json.h>

#include <QMap>
#include <QMutex>
#include <QString>

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

/// Id of the token set used when a spec does not name one.
extern const char *const kDefaultTokenSetId;

/// Validation of a token-set document (id/version, typography styles,
/// color hex values, palette arrays, positive spacing, variant shapes).
/// Empty returned vector = valid.
std::vector<std::string> validateTokenSet( const Json::Value &doc );

class TokenSetRegistry
{
  public:
    static TokenSetRegistry &instance();

    /// Directory override (scans <dir>/tokens/*.json or <dir>/*.json);
    /// reloads lazily.
    void setDirectory( const QString &dir );
    QString directory() const;

    /// All token sets (full documents) as a JSON array.
    Json::Value tokenSets() const;

    /// Base document for `id`; null when unknown (callers fall back to the
    /// default set rather than failing composition).
    Json::Value find( const QString &id ) const;

    /// Registers one token-set document programmatically (validated).
    bool registerTokenSet( Json::Value doc, QString *error = nullptr );

    void reload();

  private:
    TokenSetRegistry() = default;
    void ensureLoadedLocked() const;
    void loadEmbeddedDefaults() const;

    mutable QMutex mMutex;
    mutable bool mLoaded = false;
    QString mDirectory;
    mutable QMap<QString, Json::Value> mTokenSets; // id -> document
};

/// Deep merge: objects merge recursively; arrays and atoms are replaced by
/// the overlay. Pure (inputs are not modified).
Json::Value mergeTokenValues( const Json::Value &base, const Json::Value &overlay );

/// Effective token set for a MapSpec (or a bare `style` object):
/// registry set (or default) → variants[medium] merged → style.overrides
/// merged. Never returns null: unknown ids resolve to the default set.
Json::Value resolveTokenSet( const Json::Value &specOrStyle );

// --- typed accessors (documented fallbacks; never throw) -------------------

Json::Value tokenValue( const Json::Value &tokens, const std::string &dottedPath );
std::string tokenString( const Json::Value &tokens, const std::string &dottedPath,
                         const std::string &fallback = std::string() );
double tokenNumber( const Json::Value &tokens, const std::string &dottedPath, double fallback );
bool tokenBool( const Json::Value &tokens, const std::string &dottedPath, bool fallback );

/// Palette `name` as a hex-color array; falls back to "qualitative", then
/// to an empty array.
Json::Value tokenPalette( const Json::Value &tokens, const std::string &name );

/// Typography style entry ("title", "body", …) as {size_pt, weight}; falls
/// back to the "body" style, then {size_pt: fallbackPt, weight: "normal"}.
Json::Value tokenTextStyle( const Json::Value &tokens, const std::string &style,
                            double fallbackPt = 9.0 );

/// Registers Qt font substitutions so the token set's `font_family` degrades
/// gracefully for CJK glyphs through `cjk_fallbacks`. Safe to call repeatedly.
void applyTokenFontFallbacks( const Json::Value &tokens );

} // namespace sicnu::agent::cartography
