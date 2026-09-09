// src/agent/cartography/style_spec.h
#pragma once

//
// StyleSpec — declarative symbology knowledge (Platform 5.0, Milestone E).
//
// A StyleSpec describes *how a dataset should be displayed* (renderer family,
// class palettes, stretches, labels, scale visibility) without ever becoming
// a second rendering engine: application goes through the existing QGIS
// renderer primitives (symbology domain), and QGIS stays the single
// rendering truth. StyleSpecs are knowledge assets under
// data/cartography/styles/*.json consumed by solutions, templates and the
// style:* tools.
//
// Document shape (schema_version "1.0", kind "style_spec"):
//   { id, version, description, applies_to: "raster"|"vector",
//     token_set_ref?,                       // token set used for token: resolution
//     raster?: { renderertype: singleband_gray|singleband_pseudocolor|
//                paletted|multiband_color, band?, gamma?, opacity?,
//                nodata?: {transparent}, resampling?,
//                stretch?: {type: minmax|percentile|stddev, percentile?, stddev?, min?, max?},
//                classification?: { mode: discrete|continuous,
//                    ramp?: "token:palettes.X"|name,
//                    classes?: [{min,max,label,color}],   // color may be "token:colors.X"
//                    colorbar?: {label_min,label_max} } },
//     vector?: { renderertype: simple|categorized|graduated|rule_based,
//                field?, categories?: [{value,label,color,symbol?}],
//                ramp?, classes?, symbols?: {width_mm?},
//                labels?: {enabled, field?, size_pt?, color?, halo_mm?, priority?, scale_min?, scale_max?},
//                scaledenominator?: {min?, max?}, opacity?, blend_mode? },
//     semantics?: ["flood", …] }
//
// Any string value may carry a `token:<dotted-path>` reference resolved
// against the referenced token set at apply time (Milestone E closes the
// 4.0 gap where `token:` strings leaked through unparsed). Unknown token
// paths are validation/apply-time errors — never silent literals.
//

#include <json/json.h>

#include <QMap>
#include <QMutex>
#include <QString>

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

/// Closed raster renderer vocabulary a StyleSpec may declare.
bool isRasterRendererType( const std::string &type );
/// Closed vector renderer vocabulary a StyleSpec may declare.
bool isVectorRendererType( const std::string &type );

/// True when `value` is a well-formed token reference ("token:dotted.path").
bool isTokenReference( const std::string &value );
/// Dotted path of a token reference ("token:palettes.diverging" ->
/// "palettes.diverging"); empty when `value` is not a reference.
std::string tokenReferencePath( const std::string &value );

/// Maximum transitive hops when resolving token references (issue #815:
/// tokens may alias other tokens, e.g. "token:colors.primary" ->
/// "token:colors.accent" -> "#2c7fb8").
inline constexpr int kMaxTokenHops = 8;

/// Resolves `value` transitively while it stays a "token:<path>" string
/// reference into `tokens`. Cycles and over-deep chains are appended to
/// `problems` and the ORIGINAL value is returned verbatim — a resolution
/// failure never silently substitutes a wrong value. Pure.
Json::Value resolveTokenReferenceChain( const Json::Value &tokens, const Json::Value &value,
                                        std::vector<std::string> &problems );

//
// Platform 6.0 (Milestone E): semantic applicability.
//
// A StyleSpec may declare the data it is semantically FOR:
//   applicability: {
//     value_domain?: {min: number, max: number},  // e.g. NDVI [-1,1], probability [0,1]
//     band_count?: {min?: int, max?: int},        // e.g. multiband_color needs >= 3
//     modalities?: ["optical","sar",...],
//     semantics?: ["ndvi", ...]                    // additional domain tags
//   }
// Application refuses — with an explicit problem — when the target data
// contradicts the declaration. A semantically wrong renderer is never
// silently substituted.
//

/// Validates the `applicability` block shape. Empty = valid.
std::vector<std::string> validateStyleApplicability( const Json::Value &styleSpec );

/// Checks a style's applicability against dataset metadata:
///   {kind?: "raster"|"vector", band_count?, modality?,
///    value_min?, value_max?, semantics?: [...]}
/// Returns human-readable problems; empty means applicable. Pure.
std::vector<std::string> checkStyleApplicability( const Json::Value &styleSpec,
                                                  const Json::Value &dataset );

/// Structural validation of a StyleSpec document (envelope, applies_to,
/// renderer vocabulary, class/category entries, numeric ranges, bounded
/// array sizes). Empty returned vector = valid.
std::vector<std::string> validateStyleSpec( const Json::Value &doc );

/// Resolves every "token:*" reference in `styleSpec` against `tokens`
/// (typically resolveTokenSet(...)). Unresolvable paths are reported in the
/// returned problem list and left verbatim (never guessed). Pure: returns a
/// resolved deep copy; `styleSpec` is not modified.
Json::Value resolveStyleTokens( const Json::Value &styleSpec, const Json::Value &tokens,
                                std::vector<std::string> *problems = nullptr );

/// Compact, bounded summary for search results / token-budget responses:
/// {id, version, applies_to, renderer, semantics, description}.
Json::Value compactStyleSummary( const Json::Value &styleSpec );

class StyleRegistry
{
  public:
    static StyleRegistry &instance();

    /// Directory override: `dir` may be the cartography root (scans
    /// <dir>/styles/*.json) or a direct styles directory. Reloads lazily.
    void setDirectory( const QString &dir );
    QString directory() const;

    /// All style specs as a JSON array (id-ordered).
    Json::Value styles() const;

    Json::Value find( const QString &id ) const; ///< Null when unknown.

    /// Registers one style spec programmatically (validated).
    bool registerStyle( Json::Value doc, QString *error = nullptr );

    /// Problems recorded while loading (invalid documents skipped).
    QStringList loadProblems() const;

    void reload();

  private:
    StyleRegistry() = default;
    void ensureLoadedLocked() const;
    void loadEmbeddedDefaults() const;

    mutable QMutex mMutex;
    mutable bool mLoaded = false;
    QString mDirectory;
    mutable QMap<QString, Json::Value> mStyles; // id -> document
    mutable QStringList mLoadProblems;
};

} // namespace sicnu::agent::cartography
