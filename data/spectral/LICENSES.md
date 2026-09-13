# Licenses & Provenance — `data/spectral/`

This document is the license and provenance roll-up for the built-in spectral
library. The data contract test (`tests/test_spectral_library_data.cpp`)
asserts zero drift between this file and `library.json`: every shipped entry
id must appear here, and every entry must carry the license below.

## License of the shipped data

**CC0-1.0 (public domain dedication).** All spectra in
`data/spectral/library.json` are original synthetic parametric models authored
for the rs-studio project (D12) and are dedicated to the public domain
worldwide under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
No attribution is required; the `citation` fields exist for scientific
transparency, not legal obligation. The sensor band centres/FWHM in
`sensors.json` are nominal public facts from the official Landsat, Sentinel-2
and GF user guides.

The library is data-only and independent of any application code licensing.

## Why every spectrum is synthetic (license-first policy)

Track policy (GOAL default #1, "宁缺毋滥"): only redistributable material
ships. Rather than adjudicating the redistribution terms of third-party
measured libraries (ASTER/ECOSTRESS, JPL; academic collections with
unclear terms), **all entries are physics/textbook-grounded synthetic models**
generated deterministically by `tools/generate_library.py` — the script is
the derivation of record and each entry carries `synthetic: true` plus a
`derivation` string naming the model and its parameters.

Every entry therefore has:

- `source`: the generating model (`generate_library.py::<function>`);
- `license`: `CC0-1.0`;
- `citation`: dedication statement + the physical literature the model
  magnitudes are grounded in (see grounding table below).

## Entry index (25 entries, generated from library.json)

| Entry | Material | Subclass | Model |
| --- | --- | --- | --- |
| `water-clear-deep` | water | oligotrophic, deep | `m_water_clear` |
| `water-clear-mesotrophic` | water | mesotrophic, clear | `m_water_clear_productive` |
| `water-turbid-sediment` | water | turbid, sediment-dominated | `m_water_turbid` |
| `water-eutrophic-algae` | water | turbid, algal bloom | `m_water_eutrophic` |
| `vegetation-healthy-canopy` | vegetation | healthy, green canopy | `m_vegetation_healthy` |
| `vegetation-stressed-canopy` | vegetation | stressed canopy | `m_vegetation_stressed` |
| `vegetation-dry-grass` | vegetation | senesced, non-photosynthetic | `m_vegetation_dry` |
| `soil-loam-dry` | soil | dry loam, low organic matter | `m_soil_dry` |
| `soil-loam-moist` | soil | moist loam | `m_soil_moist` |
| `soil-organic-dark` | soil | organic-rich, dark | `m_soil_organic` |
| `imperv-asphalt-new` | impervious_surface | asphalt, new | `m_asphalt` |
| `imperv-concrete-aged` | impervious_surface | concrete, aged, bright | `m_concrete` |
| `imperv-roof-dark` | impervious_surface | bitumen roof, dark | `m_roof_dark` |
| `rock-granite-bare` | bare_rock | felsic, granite | `m_granite` |
| `rock-basalt-bare` | bare_rock | mafic, basalt | `m_basalt` |
| `sand-quartz-dry` | sand | dry dune, quartz | `m_sand_dry` |
| `sand-beach-wet` | sand | wet intertidal | `m_sand_wet` |
| `snow-fresh` | snow | fresh, fine-grained | `m_snow_fresh` |
| `ice-lake` | ice | lake ice | `m_ice_lake` |
| `cloud-water-thick` | cloud | optically thick water cloud | `m_cloud_thick` |
| `shadow-terrain-cast` | shadow | terrain/cast, skylight-dominated | `m_shadow` |
| `crop-wheat-green` | cropland | wheat, green canopy | `m_crop_wheat` |
| `crop-corn-green` | cropland | corn, green canopy | `m_crop_corn` |
| `crop-rice-paddy` | cropland | rice, paddy | `m_crop_rice` |
| `burn-recent-char` | burned_area | recent fire, char/ash | `m_burn_recent` |

## Physical grounding per material family

| Family | Model magnitudes grounded in |
| --- | --- |
| water (all) | Kou, Labrie & Chylek (1993) Appl. Opt. 32:3531; Pope & Fry (1997) Appl. Opt. 36:8710; Mobley (1994) *Light and Water* |
| water (turbid) | Ritchie, Zimba & Everitt (2003) Photogramm. Eng. Remote Sens. 69:55; Kutser (2004) Limnol. Oceanogr. |
| vegetation / crops | Knipling (1970) Remote Sens. Environ. 1:127; Guyot & Baret (1988); Jacquemoud & Baret (1990) Remote Sens. Environ. 34:251 (PROSAIL family); Carter (1994) Int. J. Remote Sens. 15:2743; Thenkabail et al. (2000) Remote Sens. Rev. 18:169; Xiao et al. (2005) Remote Sens. Environ. 95:359 |
| senescent vegetation | Elvidge (1990) Remote Sens. Environ. 33:55 (cellulose 2100 nm); Asner (1998) Prog. Phys. Geogr. 22:144 |
| soil | Stoner & Baumgardner (1981) SSSAJ 45:1161; Ben-Dor, Irons & Epema (1999), in *Remote Sensing for the Earth Sciences* |
| impervious surfaces | Herold, Gardner & Roberts (2003) Remote Sens. Environ. 86:173 (urban spectral library) |
| bare rock / sand | Hunt & Salisbury (1970) AFCRL 70-0043; Clark et al. (2007) USGS Digital Spectral Library 6 |
| snow / ice | Warren (1982) Rev. Geophys. 20:67; Warren & Brandt (2008) J. Geophys. Res. 113:D14220 |
| cloud | Nakajima & King (1990) J. Atmos. Sci. 47:1874; Platnick et al. (2003) IEEE TGRS 41:2182 |
| shadow | Li & Strahler (1992) IEEE TGRS 30:276; Schaepman-Strub et al. (2006) Remote Sens. Environ. 103:27 |
| burned area | Pereira et al. (1999); Roy, Lewis & Justice (2002) Remote Sens. Environ. 82:432 |

These works ground the *magnitudes* (the literature values the parametric
models reproduce); none of their measured data is copied or redistributed.

## Adding measured entries (future path)

Measured spectra may join the library only when their redistribution terms are
unambiguous (e.g. USGS Spectral Library Version 7 — Kokaly et al. (2017),
DOI 10.5066/F7RR1WDJ — U.S. Government work, public domain). Such entries
must carry their own `source` / `license` / `citation`, a row here, and
`synthetic: false`. Anything with unclear terms is re-synthesized instead of
shipped.
