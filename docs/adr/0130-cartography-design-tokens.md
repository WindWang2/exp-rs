# ADR 0130: Cartography Design Tokens & Component/Template Schema v2

- Status: Accepted (Design System 4.0, Milestones A/B)
- Context: MapSpec (ADR 0127) shipped a seed library of 10 component
  descriptors and 8 templates. Typography (title 18 pt, notes 7 pt), colors
  and strings were hard-coded in the compiler and tool implementations, the
  component registry was catalog-only (descriptors were never consulted by
  the compiler), and the seed schema's variant/compatibility fields were
  inert metadata — there were no item-shaped defaults, data-binding
  declarations, validation hints, or token resolution. An autonomous planner
  needs one authoritative style source, not per-item guesses.

## Decision

1. **Versioned design-token sets** (`data/cartography/tokens/*.json`,
   `TokenSetRegistry`): named families — typography hierarchy with CJK
   fallback chains, spacing/margins/gutters, line weights, semantic and
   status colors, qualitative/sequential/diverging palettes (colorblind-safe
   defaults: Okabe-Ito, ColorBrewer), furniture metrics, chart defaults —
   plus `variants` for delivery media (`print`, `screen`). Dark mode ships
   only as `scientific-dark` for screen delivery; print-dark is documented
   as experimental (paper is reflective; journals reject it).
2. **Resolution is total and pure**: `resolveTokenSet(spec)` =
   registry set (default `scientific-light`) → deep-merge
   `variants[style.medium]` → deep-merge `style.overrides`. Unknown ids fall
   back to the default set. Same input → same output, platform-independent.
3. **Token→QGIS mapping is applied by the compiler only** (`MapSpecCompiler`
   resolves tokens once per compile): label font size/weight/color/family
   onto `QgsLayoutItemLabel` fonts, chart font/palette/grid onto the painter
   renderer, colorbar ramps onto token palette stops. Tokens never introduce
   a second rendering path — QgsPrintLayout stays the rendering truth.
4. **Precedence chain** (pinned by tests): item fields — including slot
   `content` drafts, which instantiateTemplate merges into the item before
   component resolution — > component variant parameters > component
   defaults > token set > compiler built-in fallback.
5. **Component schema v2**: `version`, `variants[]` (id + parameter/default
   overrides), `defaults` (item-shaped fields merged into referencing
   items), `data_bindings[]` (what must be bound, descriptive), `validation`
   hints, `compatibility` (map/result types, CRS requirements). `variants`
   replace near-duplicate descriptor files; one descriptor carries its style
   family (`scale-bar/single` has five QGIS renderer style variants).
6. **Component references are load-bearing**: a MapSpec item's
   `source_component` (id string or `{id, variant}`) resolves through
   `applyComponentDefaults` at compile time; unknown references surface as a
   preflight finding (`MAP_UNKNOWN_COMPONENT`) and are stripped by repair.

## Consequences

- The compiler stopped hard-coding style: changing the shipped token set
  restyles every component/template that does not override it.
- Templates declare slots with `content` drafts instead of relying on C++
  per-collection defaults (which were deleted from `instantiateTemplate`).
- CJK text degrades gracefully: token `cjk_fallbacks` register Qt font
  substitutions; the token text-width estimator (quality module) is
  platform-independent arithmetic, not font metrics.
- `cartography:list_token_sets` / `cartography:get_token_set` expose the
  token catalog to agents; `buildCatalogIndex()` generates the machine index
  behind the gallery docs and the docs-drift test.
