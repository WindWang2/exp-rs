# Licenses & Provenance — `data/spectral/`

This document is the license and provenance roll-up for the built-in spectral
library. The data contract test (`tests/test_spectral_library_data.cpp`)
asserts zero drift between this file and `library.json` in both directions:
every shipped entry id must appear in the entry table, and every entry-table
row must reference a shipped entry.

## License of the shipped data

**CC0-1.0 (public domain dedication).** All spectra in
`data/spectral/library.json` are original synthetic parametric models authored
for the rs-studio project (D12) and are dedicated to the public domain
worldwide under CC0 1.0 (legal text embedded at the bottom of this file for
offline distribution; see also
<https://creativecommons.org/publicdomain/zero/1.0/>). No attribution is
required; the `citation` fields exist for scientific transparency, not legal
obligation. The sensor band centres/FWHM in `sensors.json` are nominal public
facts from the official Landsat, Sentinel-2 and GF user guides.

The library is data-only and independent of any application code licensing.

## Why every spectrum is synthetic (license-first policy)

Track policy (GOAL default #1, "宁缺毋滥"): only redistributable material
ships. Rather than adjudicating the redistribution terms of third-party
measured libraries (ASTER/ECOSTRESS, JPL; academic collections with unclear
terms), **all entries are physics/textbook-grounded synthetic models**
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

These works ground the *magnitudes* (the literature values the parametric
models reproduce); none of their measured data is copied or redistributed.

| Family | Model magnitudes grounded in |
| --- | --- |
| water (all) | Kou, Labrie & Chylek (1993) Appl. Opt. 32:3531-3540; Pope & Fry (1997) Appl. Opt. 36:8710-8723; Mobley (1994) *Light and Water* |
| water (turbid/eutrophic) | Ritchie, Zimba & Everitt (2003) Photogramm. Eng. Remote Sens. 69:695-704; Kutser (2004) Limnol. Oceanogr. 49:1821; Dekker (1993) — inland water quality spectra |
| vegetation / crops | Knipling (1970) Remote Sens. Environ. 1:155-159; Guyot & Baret (1988) — red edge; Jacquemoud & Baret (1990) Remote Sens. Environ. 34:75-91 (PROSPECT leaf optics, SAIL canopy family); Carter (1994) Int. J. Remote Sens. 15:697-704; Ceccato et al. (2001) Int. J. Remote Sens. 22:2973; Thenkabail et al. (2000) Remote Sens. Environ. 71:158-182; Xiao et al. (2005) Remote Sens. Environ. 95:359-372 |
| senescent vegetation | Elvidge (1990) Int. J. Remote Sens. 11:1775-1795 (cellulose/lignin 2100 nm); Asner (1998) Remote Sens. Environ. 64:234-253 |
| soil | Stoner & Baumgardner (1981) SSSAJ 45:1161-1165; Ben-Dor, Irons & Epema (1999), in *Remote Sensing for the Earth Sciences* (Wiley) |
| impervious surfaces | Herold, Gardner & Roberts (2003) IEEE Trans. Geosci. Remote Sens. 41:1907-1917 (urban spectral library) |
| bare rock / sand | Hunt & Salisbury (1970) Modern Geology 1:283-300 / AFCRL report series; Clark et al. (2007) USGS Digital Spectral Library 6 (splib06a) |
| snow / ice | Warren (1982) Rev. Geophys. 20:67-89; Warren & Brandt (2008) J. Geophys. Res. 113:D14220 |
| cloud | Nakajima & King (1990) J. Atmos. Sci. 47:1878-1897; Platnick et al. (2003) IEEE Trans. Geosci. Remote Sens. 41:459-473 |
| shadow | Li & Strahler (1992) IEEE Trans. Geosci. Remote Sens. 30:276-285; Schaepman-Strub et al. (2006) Remote Sens. Environ. 103:27-42 |
| burned area | Pereira (1999) IEEE Trans. Geosci. Remote Sens. 37:217-226; Roy, Lewis & Justice (2002) Remote Sens. Environ. 83:263-286 |

## Adding measured entries (future path)

Measured spectra may join the library only when their redistribution terms are
unambiguous (e.g. USGS Spectral Library Version 7 — Kokaly et al. (2017),
DOI 10.5066/F7RR1WDJ — U.S. Government work, public domain). Such entries
must carry their own `source` / `license` / `citation`, a row here, and
`synthetic: false`. Anything with unclear terms is re-synthesized instead of
shipped. Acceptable license values are constrained by the schema enum
(`CC0-1.0`, `Public Domain`, `CC-BY-4.0`, `CC-BY-SA-4.0`).

## CC0 1.0 Universal — legal text

Creative Commons Legal Code — CC0 1.0 Universal

CREATIVE COMMONS CORPORATION IS NOT A LAW FIRM AND DOES NOT PROVIDE LEGAL
SERVICES. DISTRIBUTION OF THIS DOCUMENT DOES NOT CREATE AN ATTORNEY-CLIENT
RELATIONSHIP. CREATIVE COMMONS PROVIDES THIS INFORMATION ON AN "AS-IS" BASIS.
CREATIVE COMMONS MAKES NO WARRANTIES REGARDING THE USE OF THIS DOCUMENT OR THE
INFORMATION OR WORKS PROVIDED HEREUNDER, AND DISCLAIMS LIABILITY FOR DAMAGES
RESULTING FROM THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS PROVIDED
HEREUNDER.

Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator and
subsequent holder(s) (each and all, an "owner") of an original work of
authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for the
purpose of contributing to a commons of creative, cultural and scientific
works ("Commons") that the public can reliably and without fear of later
claims of infringement build upon, modify, incorporate in other works, reuse
and redistribute as freely as possible in any form whatsoever and for any
purposes, including without limitation commercial purposes. These owners may
contribute to the Commons to promote the ideal of a free culture and the
further production of creative, cultural and scientific works, or to gain
reputation or greater distribution for their Work in part through the use and
efforts of others.

For these and/or other purposes and motivations, and without any expectation
of additional consideration or compensation, the person associating CC0 with
a Work (the "Affirmer"), to the extent that he or she is an owner of
Copyright and Related Rights in the Work, voluntarily elects to apply CC0 to
the Work and publicly distribute the Work under its terms, with knowledge of
his or her Copyright and Related Rights in the Work and the meaning and
intended legal effect of CC0 on those rights.

1. Copyright and Related Rights. A Work made available under CC0 may be
protected by copyright and related or neighboring rights ("Copyright and
Related Rights"). Copyright and Related Rights include, but are not limited
to, the following: (i) the right to reproduce, adapt, distribute, perform,
display, communicate, and translate a Work; (ii) moral rights retained by the
original author(s) and/or performer(s); (iii) publicity and privacy rights
pertaining to a person's image or likeness depicted in a Work; (iv) rights
protecting against unfair competition in regards to a Work, subject to the
limitations in paragraph 4(a), below; (v) rights protecting the extraction,
dissemination, use and reuse of data in a Work; (vi) database rights (such as
those arising under Directive 96/9/EC of the European Parliament and of the
Council of 11 March 1996 on the legal protection of databases, and under any
national implementation thereof, including any amended or successor version
of such directive); and (vii) other similar, equivalent or corresponding
rights throughout the world based on applicable law or treaty, and any
national implementations thereof.

2. Waiver. To the greatest extent permitted by, but not in contravention of,
applicable law, Affirmer hereby overtly, fully, permanently, irrevocably and
unconditionally waives, abandons, and surrenders all of Affirmer's Copyright
and Related Rights and associated claims and causes of action, whether now
known or unknown (including existing as well as future claims and causes of
action), in the Work (i) in all territories worldwide, (ii) for the maximum
duration provided by applicable law or treaty (including future time
extensions), (iii) in any current or future medium and for any number of
copies, and (iv) for any purpose whatsoever, including without limitation
commercial, advertising or promotional purposes (the "Waiver"). Affirmer
makes the Waiver for the benefit of each member of the public at large and to
the detriment of Affirmer's heirs and successors, fully intending that such
Waiver shall not be subject to revocation, rescission, cancellation,
termination, or any other legal or equitable action to disrupt the quiet
enjoyment of the Work by the public as contemplated by Affirmer's express
Statement of Purpose.

3. Public License Fallback. Should any part of the Waiver for any reason be
judged legally invalid or ineffective under applicable law, then the Waiver
shall be preserved to the maximum extent permitted taking into account
Affirmer's express Statement of Purpose. In addition, to the extent the
Waiver is so judged Affirmer hereby grants to each affected person a
royalty-free, non transferable, non sublicensable, non exclusive,
irrevocable and unconditional license to exercise Affirmer's Copyright and
Related Rights in the Work (i) in all territories worldwide, (ii) for the
maximum duration provided by applicable law or treaty (including future time
extensions), (iii) in any current or future medium and for any number of
copies, and (iv) for any purpose whatsoever, including without limitation
commercial, advertising or promotional purposes (the "License"). The License
shall be deemed effective as of the date CC0 was applied by Affirmer to the
Work. Should any part of the License for any reason be judged legally invalid
or ineffective under applicable law, such partial invalidity or
ineffectiveness shall not invalidate the remainder of the License, and in
such case Affirmer hereby affirms that he or she will not (i) exercise any of
his or her remaining Copyright and Related Rights in the Work or (ii) assert
any associated claims and causes of action with respect to the Work, in
either case contrary to Affirmer's express Statement of Purpose.

4. Limitations and Disclaimers. (a) No trademark or patent rights held by
Affirmer are waived, abandoned, surrendered, licensed or otherwise affected
by this document. (b) Affirmer offers the Work as-is and makes no
representations or warranties of any kind concerning the Work, express,
implied, statutory or otherwise, including without limitation warranties of
title, merchantability, fitness for a particular purpose, non infringement,
or the absence of latent or other defects, accuracy, or the present or
absence of errors, whether or not discoverable, all to the greatest extent
permissible under applicable law. (c) Affirmer disclaims responsibility for
clearing rights of other persons that may apply to the Work or any use
thereof, including without limitation any person's Copyright and Related
Rights in the Work. Further, Affirmer disclaims responsibility for obtaining
any necessary consents, permissions or other rights required for any use of
the Work. (d) Affirmer understands and acknowledges that Creative Commons is
not a party to this document and has no duty or obligation with respect to
this CC0 or use of the Work.
