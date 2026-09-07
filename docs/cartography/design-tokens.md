# Design Token Guide (Cartography Design System 4.0)

Design tokens are the single authoritative source of style decisions for
MapSpec cartography: typography, spacing, colors, palettes, furniture
metrics and chart defaults. Token sets are versioned JSON documents; QGIS
stays the rendering engine — tokens only parameterize how
`MapSpecCompiler` creates layout items (ADR 0130).

## Shipped token sets

| id | medium variants | use |
|----|-----------------|-----|
| `scientific-light` | `print` (default), `screen` | Default light scientific style for print-quality products. |
| `scientific-dark` | `screen` (base), `print` variant (experimental whitening) | Slide decks, dashboards, agent previews. |

Token families: `typography` (font family, `cjk_fallbacks`, named styles
`title`/`subtitle`/`section`/`body`/`caption`/`annotation`/`source_note`/
`figure_label`/`chart`), `spacing`, `lines`, `colors`, `palettes`
(colorblind-safe Okabe-Ito qualitative; ColorBrewer sequential/diverging),
`furniture`, `chart`, and `variants` per medium.

## Resolution

```
resolveTokenSet(spec):
  base        = TokenSetRegistry[spec.style.token_set] or scientific-light
  effective   = deep_merge(base, base.variants[spec.style.medium or "print"])
  effective   = deep_merge(effective, spec.style.overrides)
```

Resolution is total: unknown ids fall back to the default set; the same
input always produces the same output. `variants` is consumed during
resolution and removed from the effective document; `resolved.token_set`
and `resolved.medium` record what was applied.

## Precedence chain

When the compiler materializes a property, the first source that defines it
wins:

1. item explicit fields, including template slot `content` drafts
   (`instantiateTemplate` merges content into the item before component
   resolution, so a slot draft outranks generic component styling)
2. component variant parameters (`source_component: {id, variant}`)
3. component `defaults` (and parameters, for item-shaped fields)
4. token set
5. compiler built-in fallback (last-resort constants)

## How tokens reach QGIS properties

| Token | QGIS layout property |
|-------|----------------------|
| `typography.styles.<style>.size_pt` | `QgsLayoutItemLabel::font()` point size |
| `typography.styles.<style>.weight` | `QFont::setBold` |
| `typography.styles.<style>.color` (name → `colors.*`) | `QgsLayoutItemLabel::setFontColor` |
| `typography.font_family` + `cjk_fallbacks` | `QFont::setFamily` + `QFont::insertSubstitutions` (graceful CJK degradation) |
| `chart.font_pt`, `chart.palette`, `chart.show_grid` | inline chart painter options |
| `palettes.<ramp>` | colorbar gradient stops (`colors` array), matrix chart fill ramp |
| `colors.text` | chart `text_color`, colorbar label color |
| `spacing.margin_mm` | composition solver anchor margin + repair margin box |

## Authoring a token set

Create `data/cartography/tokens/<id>.json`:

```json
{
  "id": "my-style",
  "version": 1,
  "description": "Corporate style",
  "typography": { "font_family": "Arial",
                  "cjk_fallbacks": ["Noto Sans CJK SC"],
                  "styles": { "title": { "size_pt": 18, "weight": "bold" } } },
  "spacing": { "margin_mm": 15, "gutter_mm": 5 },
  "colors": { "text": "#1a1a1a", "accent": "#0f62fe" },
  "palettes": { "qualitative": ["#0f62fe", "#42be65", "#ff7eb6"] },
  "variants": { "screen": { "typography": { "styles": { "title": { "size_pt": 20 } } } } }
}
```

Validation rules (`validateTokenSet`): string `id`, integer `version`,
positive `size_pt` per style, `weight` ∈ {normal, bold}, hex `colors`
values, palette arrays of hex colors, non-negative `spacing`, object
`variants`. Registration through `TokenSetRegistry::registerTokenSet`
rejects malformed documents with a reason.

## Agent surface

- `cartography:list_token_sets` — compact catalog (id, version, mediums).
- `cartography:get_token_set` — effective tokens for `{token_set?, medium?,
  overrides?}`.

## Testing

`tests/test_cartography_tokens.cpp` pins registry loading (with embedded
fallback), resolution/merge semantics, typed accessor fallbacks, font
substitution registration, and compiler consumption (label fonts follow the
token set; explicit item fields win; the screen variant raises sizes).
