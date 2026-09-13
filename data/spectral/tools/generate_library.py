#!/usr/bin/env python3
"""Generate the built-in rs-studio spectral library (data/spectral/library.json).

D12 deliverable. Every spectrum is a physics/textbook-grounded *synthetic*
parametric model (GOAL default #1: license first, better missing than dubious).
The models reproduce the canonical magnitudes taught in remote sensing:

  - clear water: near-zero reflectance collapsing across red -> NIR
    (pure-water absorption rises steeply beyond 700 nm; Kou et al. 1993,
    Pope & Fry 1997; Mobley 1994),
  - turbid / eutrophic water: suspended-sediment and algal-scatter signatures
    (green peak, chlorophyll absorption dips at 440/675 nm, elevated NIR),
  - vegetation: green peak ~0.06, red-edge rise 690-745 nm, NIR plateau
    ~0.44, leaf-water dips at 1400/1930 nm, SWIR humps (PROSAIL-family canopy
    behaviour; Guyot & Baret 1988; Jacquemoud & Baret 1990),
  - senescent vegetation (NPV): cellulose/lignin absorption near 2100 nm
    (Elvidge 1990),
  - soils: monotonically rising baseline, moisture-suppressed albedo with
    deep 1400/1930 nm bands (Stoner & Baumgardner 1981; Ben-Dor et al. 1999),
  - impervious surfaces: dark asphalt / bright concrete / bitumen roof
    canonical urban curves (Herold, Gardner & Roberts 2003),
  - quartz sand: reststrahlen features near 2200 nm (Hunt & Salisbury 1970),
  - snow / ice: high visible albedo with ice absorption at 1030/1260/1500/
    1930/2250 nm (Warren 1982; Warren & Brandt 2008),
  - water clouds: bright, flat visible/NIR with liquid-water dips near
    1450/1940 nm (Nakajima & King 1990 family),
  - shadow: dim, blue-tilted skylight-dominated spectrum,
  - burned area: very dark, flat char spectrum with a weak ash bump.

The script is the derivation of record: each entry's `derivation` names this
file and its parameters; `citation` cites the physical literature grounding
the magnitudes. Re-run with no arguments to regenerate library.json
(deterministic, stdlib only, no network). A --check-only mode validates the
models without writing.
"""

import argparse
import json
import math
import sys
from pathlib import Path

WAVELENGTH_MIN = 400.0
WAVELENGTH_MAX = 2500.0
WAVELENGTH_STEP = 5.0

LICENSE = "CC0-1.0"
GENERATOR = "data/spectral/tools/generate_library.py"

TAXONOMY = [
    "bare_rock", "burned_area", "cloud", "cropland", "ice", "impervious_surface",
    "sand", "shadow", "snow", "soil", "vegetation", "water",
]


# ---------------------------------------------------------------------------
# Model building blocks
# ---------------------------------------------------------------------------

def grid():
    n = int(round((WAVELENGTH_MAX - WAVELENGTH_MIN) / WAVELENGTH_STEP)) + 1
    return [WAVELENGTH_MIN + i * WAVELENGTH_STEP for i in range(n)]


def gauss(x, mu, sigma):
    return math.exp(-((x - mu) ** 2) / (2.0 * sigma * sigma))


def smooth(x, x0, x1):
    """Smoothstep 0 -> 1 across [x0, x1] (clamped)."""
    t = max(0.0, min(1.0, (x - x0) / (x1 - x0)))
    return t * t * (3.0 - 2.0 * t)


def vapor(base, x, d1400, d1900, c1400=1400.0, c1900=1930.0):
    """Multiplicative atmospheric/leaf-water absorption bands."""
    return base * (1.0 - d1400 * gauss(x, c1400, 45.0)) * (1.0 - d1900 * gauss(x, c1900, 60.0))


def shade(value):
    return max(0.0, min(1.0, value))


# ---------------------------------------------------------------------------
# Material models (surface/canopy reflectance, teaching-grade magnitudes)
# ---------------------------------------------------------------------------

def m_water_clear(x):
    r = 0.05 * math.exp(-1.5 * (x - 400.0) / 300.0)
    if x > 720.0:
        r *= math.exp(-(x - 720.0) / 90.0)
    return shade(max(r, 0.0003))


def m_water_clear_productive(x):
    # Mesotrophic lake: slightly greener and brighter than oligotrophic water,
    # filling the intra-class gap between clear-deep and eutrophic water.
    r = 0.042 * math.exp(-1.15 * (x - 400.0) / 300.0)
    if x > 740.0:
        r *= math.exp(-(x - 740.0) / 110.0)
    return shade(max(r, 0.0006))


def m_water_turbid(x):
    base = 0.055 + 0.055 * gauss(x, 580.0, 240.0) + 0.025 * gauss(x, 680.0, 150.0)
    if x > 730.0:
        base *= math.exp(-(x - 730.0) / 110.0)
    return shade(max(vapor(base, x, 0.70, 0.80), 0.001))


def m_water_eutrophic(x):
    r = (0.025
         + 0.075 * gauss(x, 565.0, 70.0)
         + 0.035 * gauss(x, 710.0, 45.0)
         - 0.055 * gauss(x, 675.0, 16.0)
         - 0.035 * gauss(x, 440.0, 25.0))
    if x > 760.0:
        r *= math.exp(-(x - 760.0) / 130.0)
    return shade(max(r, 0.0005))


def _veg_common(x, r_vis, nir, swir1, swir2, d1400, d1900, edge_x0=714.0, edge_k=0.22):
    r_edge = 0.022 + (nir - 0.022) * (1.0 / (1.0 + math.exp(-edge_k * (x - edge_x0))))
    r_nir = nir * (1.0 - 0.05 * gauss(x, 1150.0, 320.0))
    r_swir = (swir1 * gauss(x, 1680.0, 180.0) + swir2 * gauss(x, 2210.0, 160.0)
              + 0.03 * math.exp(-max(0.0, x - 1500.0) / 900.0))
    w1 = smooth(x, 690.0, 745.0)
    w2 = smooth(x, 1180.0, 1360.0)
    r = r_vis * (1.0 - w1) + r_nir * w1 * (1.0 - w2) + r_swir * w2
    return shade(vapor(r, x, d1400, d1900))


def m_vegetation_healthy(x):
    r_vis = 0.028 + 0.032 * gauss(x, 550.0, 55.0) - 0.011 * gauss(x, 450.0, 40.0) \
        - 0.012 * gauss(x, 670.0, 25.0)
    return _veg_common(x, r_vis, nir=0.44, swir1=0.30, swir2=0.16, d1400=0.72, d1900=0.80)


def m_vegetation_stressed(x):
    r_vis = 0.070 + 0.022 * gauss(x, 555.0, 60.0) - 0.024 * gauss(x, 670.0, 30.0) \
        - 0.006 * gauss(x, 450.0, 40.0)
    return _veg_common(x, r_vis, nir=0.36, swir1=0.33, swir2=0.21, d1400=0.62, d1900=0.72)


def m_vegetation_dry(x):
    r_vis = 0.16 + 0.06 * gauss(x, 620.0, 120.0)
    r_nir = 0.35 * (1.0 - 0.03 * gauss(x, 1150.0, 320.0))
    r_swir = (0.28 + 0.10 * gauss(x, 1680.0, 200.0) - 0.05 * gauss(x, 2100.0, 60.0)
              - 0.04 * gauss(x, 2450.0, 150.0))
    w1 = smooth(x, 700.0, 780.0)
    w2 = smooth(x, 1220.0, 1420.0)
    r = r_vis * (1.0 - w1) + r_nir * w1 * (1.0 - w2) + r_swir * w2
    return shade(vapor(r, x, 0.45, 0.55))


def m_soil_dry(x):
    r = 0.13 + 0.10 * smooth(x, 400.0, 800.0) + 0.07 * smooth(x, 800.0, 1300.0)
    r -= 0.02 * gauss(x, 880.0, 60.0)  # weak ferric iron absorption
    r += 0.045 * smooth(x, 1500.0, 1750.0) - 0.03 * smooth(x, 2050.0, 2400.0)
    r -= 0.02 * gauss(x, 2200.0, 60.0)  # clay/OH feature
    return shade(vapor(r, x, 0.35, 0.45))


def m_soil_moist(x):
    r = (0.10 + 0.075 * smooth(x, 400.0, 800.0) + 0.05 * smooth(x, 800.0, 1300.0)
         + 0.03 * smooth(x, 1500.0, 1750.0) - 0.02 * smooth(x, 2050.0, 2400.0)
         - 0.015 * gauss(x, 880.0, 60.0) - 0.015 * gauss(x, 2200.0, 60.0))
    return shade(vapor(r, x, 0.72, 0.80))


def m_soil_organic(x):
    r = (0.07 + 0.055 * smooth(x, 400.0, 900.0) + 0.025 * smooth(x, 900.0, 1400.0)
         + 0.02 * smooth(x, 1500.0, 1750.0) - 0.015 * smooth(x, 2050.0, 2400.0)
         - 0.01 * gauss(x, 2200.0, 60.0))
    return shade(vapor(r, x, 0.55, 0.65))


def m_asphalt(x):
    r = 0.06 + 0.035 * smooth(x, 400.0, 900.0) + 0.025 * smooth(x, 900.0, 1500.0)
    r -= 0.015 * smooth(x, 2000.0, 2400.0)
    return shade(vapor(r, x, 0.30, 0.40))


def m_concrete(x):
    r = (0.35 + 0.05 * smooth(x, 400.0, 600.0) - 0.02 * smooth(x, 600.0, 700.0)
         + 0.04 * smooth(x, 700.0, 1000.0) - 0.09 * smooth(x, 1500.0, 2400.0))
    r -= 0.015 * gauss(x, 2200.0, 80.0)
    return shade(vapor(r, x, 0.35, 0.45))


def m_roof_dark(x):
    r = 0.08 + 0.05 * smooth(x, 400.0, 1000.0) + 0.045 * smooth(x, 1000.0, 1600.0)
    r -= 0.05 * smooth(x, 1900.0, 2400.0)
    return shade(vapor(r, x, 0.40, 0.50))


def m_granite(x):
    r = (0.25 + 0.10 * smooth(x, 400.0, 900.0) + 0.06 * smooth(x, 900.0, 1400.0)
         - 0.05 * smooth(x, 2100.0, 2400.0) - 0.025 * gauss(x, 2200.0, 70.0))
    return shade(vapor(r, x, 0.35, 0.45))


def m_basalt(x):
    r = (0.05 + 0.045 * smooth(x, 400.0, 850.0)
         - 0.025 * gauss(x, 1050.0, 150.0)  # broad Fe2+ absorption
         + 0.012 * smooth(x, 1300.0, 1700.0) - 0.02 * smooth(x, 2000.0, 2400.0))
    return shade(vapor(r, x, 0.35, 0.45))


def m_sand_dry(x):
    r = (0.25 + 0.13 * smooth(x, 400.0, 800.0) + 0.08 * smooth(x, 800.0, 1300.0)
         + 0.05 * smooth(x, 1500.0, 1700.0)
         - 0.05 * gauss(x, 2200.0, 45.0)   # quartz reststrahlen doublet
         - 0.03 * gauss(x, 2330.0, 45.0)
         - 0.03 * smooth(x, 2350.0, 2500.0))
    return shade(vapor(r, x, 0.30, 0.40))


def m_sand_wet(x):
    r = (0.16 + 0.08 * smooth(x, 400.0, 800.0) + 0.04 * smooth(x, 800.0, 1300.0)
         + 0.02 * smooth(x, 1500.0, 1700.0)
         - 0.02 * gauss(x, 2200.0, 45.0) - 0.02 * smooth(x, 2300.0, 2500.0))
    return shade(vapor(r, x, 0.80, 0.88))


def m_snow_fresh(x):
    r = 0.96 - 0.10 * smooth(x, 550.0, 900.0) - 0.85 * smooth(x, 900.0, 1350.0)
    # ice absorption bands sharpen the decline
    r -= 0.06 * gauss(x, 1030.0, 30.0)
    r -= 0.04 * gauss(x, 1260.0, 40.0)
    r = max(r, 0.02)
    r *= (1.0 - 0.55 * gauss(x, 1500.0, 55.0)) * (1.0 - 0.70 * gauss(x, 1930.0, 65.0))
    r -= 0.015 * gauss(x, 2250.0, 60.0)
    return shade(max(r, 0.002))


def m_ice_lake(x):
    r = 0.72 + 0.06 * smooth(x, 400.0, 600.0) - 0.68 * smooth(x, 700.0, 1250.0)
    r -= 0.10 * gauss(x, 1030.0, 30.0)
    r = max(r, 0.015)
    r *= (1.0 - 0.55 * gauss(x, 1500.0, 55.0)) * (1.0 - 0.70 * gauss(x, 1930.0, 65.0))
    r -= 0.012 * gauss(x, 2250.0, 60.0)
    return shade(max(r, 0.002))


def m_cloud_thick(x):
    r = 0.90 + 0.03 * smooth(x, 400.0, 700.0)
    r *= (1.0 - 0.35 * gauss(x, 1450.0, 90.0))   # liquid-water absorption
    r *= (1.0 - 0.55 * gauss(x, 1940.0, 110.0))
    r -= 0.30 * smooth(x, 2200.0, 2500.0)
    r *= (1.0 - 0.15 * gauss(x, 1200.0, 90.0))
    return shade(max(r, 0.02))


def m_shadow(x):
    # Shadow is identified by magnitude, not shape: near-spectrally-flat dark
    # spectrum (diffuse skylight over a mixed background).
    r = 0.040 - 0.010 * smooth(x, 400.0, 2500.0)
    return shade(max(r, 0.004))


def m_crop_wheat(x):
    r_vis = 0.032 + 0.028 * gauss(x, 550.0, 55.0) - 0.010 * gauss(x, 670.0, 25.0) \
        - 0.008 * gauss(x, 450.0, 40.0)
    canopy = _veg_common(x, r_vis, nir=0.38, swir1=0.28, swir2=0.20, d1400=0.70, d1900=0.78)
    canopy = canopy + 0.015 * gauss(x, 2200.0, 120.0)  # slight NPV/heading admixture
    return shade(0.80 * canopy + 0.20 * m_soil_dry(x))  # open-row soil background


def m_crop_corn(x):
    r_vis = 0.030 + 0.034 * gauss(x, 550.0, 60.0) - 0.013 * gauss(x, 670.0, 25.0) \
        - 0.010 * gauss(x, 450.0, 40.0)
    canopy = _veg_common(x, r_vis, nir=0.50, swir1=0.24, swir2=0.13, d1400=0.80, d1900=0.86)
    return shade(0.88 * canopy + 0.12 * m_soil_dry(x))  # row soil background


def m_crop_rice(x):
    r_vis = 0.031 + 0.030 * gauss(x, 555.0, 55.0) - 0.011 * gauss(x, 670.0, 25.0) \
        - 0.009 * gauss(x, 450.0, 40.0)
    # flooded background: suppressed NIR, very deep leaf-water bands, dark SWIR
    return _veg_common(x, r_vis, nir=0.34, swir1=0.16, swir2=0.09, d1400=0.85, d1900=0.90)


def m_burn_recent(x):
    r = (0.030 + 0.020 * smooth(x, 400.0, 1000.0) + 0.012 * smooth(x, 1000.0, 1600.0)
         - 0.012 * smooth(x, 1900.0, 2400.0))
    r += 0.008 * gauss(x, 500.0, 80.0)  # ash bump in the blue-green
    return shade(vapor(r, x, 0.40, 0.50))


# ---------------------------------------------------------------------------
# Library definition
# ---------------------------------------------------------------------------

def refs(*citations):
    return "; ".join(citations)


ENTRIES = [
    dict(id="water-clear-deep", material="water", subclass="oligotrophic, deep",
         name="Clear deep water (oligotrophic)", model=m_water_clear,
         derivation="Exponential decline 0.05@400nm with scale 1.5/300nm and NIR collapse "
                    "beyond 720nm (tau=90nm), floored at 0.0003. Parameters reproduce the "
                    "canonical oligotrophic-water signature: blue ~0.05, green ~0.02, "
                    "red <0.015, NIR ~0.",
         citation=refs("Kou, Labrie & Chylek (1993) Appl. Opt. 32:3531 — pure water absorption",
                       "Mobley (1994) Light and Water — ocean optics",
                       "Pope & Fry (1997) Appl. Opt. 36:8710 — water absorption 400-700nm")),
    dict(id="water-clear-mesotrophic", material="water", subclass="mesotrophic, clear",
         name="Clear productive water (mesotrophic lake)", model=m_water_clear_productive,
         derivation="Exponential decline 0.042@400nm with scale 1.15/300nm and NIR collapse "
                    "beyond 740nm (tau=110nm), floored at 0.0006 — the slightly greener, "
                    "more productive companion of the oligotrophic signature.",
         citation=refs("Kou, Labrie & Chylek (1993) Appl. Opt. 32:3531 — pure water absorption",
                       "Mobley (1994) Light and Water — ocean optics")),
    dict(id="water-turbid-sediment", material="water", subclass="turbid, sediment-dominated",
         name="Turbid water (sediment-dominated)", model=m_water_turbid,
         derivation="Baseline 0.055 plus broad suspended-sediment scattering peak "
                    "(+0.055 @ 580nm, sigma=240nm) and red plateau (+0.025 @ 680nm), NIR "
                    "collapse beyond 730nm (tau=110nm), 1400/1930nm bands at 0.70/0.80 depth.",
         citation=refs("Mobley (1994) Light and Water — particulate backscattering",
                       "Ritchie, Zimba & Everitt (2003) Photogramm. Eng. Remote Sens. — turbidity spectra")),
    dict(id="water-eutrophic-algae", material="water", subclass="eutrophic, algal bloom",
         name="Eutrophic water (algal bloom)", model=m_water_eutrophic,
         derivation="0.025 baseline + green scattering peak (+0.075 @ 565nm) + algal NIR "
                    "shoulder (+0.035 @ 710nm) - chlorophyll absorption (0.055 @ 675nm, "
                    "0.035 @ 440nm), NIR collapse beyond 760nm.",
         citation=refs("Kutser (2004) Limnol. Oceanogr. — cyanobacteria bloom reflectance",
                       "Dekker (1993) inland water quality spectra")),
    dict(id="vegetation-healthy-canopy", material="vegetation", subclass="healthy, green canopy",
         name="Healthy green vegetation (canopy)", model=m_vegetation_healthy,
         derivation="Chlorophyll dips (blue 0.011@450nm, red 0.012@670nm) + green peak "
                    "(0.032@550nm) on 0.028 base; red edge logistic 690-745nm to NIR plateau "
                    "0.44 with 5% droop @1150nm; SWIR humps 0.30@1680nm / 0.16@2210nm; "
                    "leaf-water bands 0.72/0.80 depth. Canopy-level canonical magnitudes.",
         citation=refs("Guyot & Baret (1988) — red edge",
                       "Jacquemoud & Baret (1990) Remote Sens. Environ. — PROSAIL canopy reflectance",
                       "Knipling (1970) Remote Sens. Environ. — 0.4-2.5um vegetation signatures")),
    dict(id="vegetation-stressed-canopy", material="vegetation", subclass="stressed canopy",
         name="Stressed vegetation (canopy)", model=m_vegetation_stressed,
         derivation="Same family as healthy canopy with chlorophyll loss (red base 0.070, "
                    "shallow red dip), depressed NIR 0.36, raised SWIR (0.33@1680nm, "
                    "0.21@2210nm) and shallower leaf-water bands (0.62/0.72) — the classic "
                    "drought/stress direction in the red-edge/SWIR space.",
         citation=refs("Carter (1994) Int. J. Remote Sens. — leaf stress reflectance",
                       "Ceccato et al. (2001) Int. J. Remote Sens. — SWIR water stress")),
    dict(id="vegetation-dry-grass", material="vegetation", subclass="senesced, non-photosynthetic",
         name="Dry / senesced vegetation (NPV)", model=m_vegetation_dry,
         derivation="No red edge: broad visible maximum 0.22@620nm, NIR plateau 0.35, SWIR "
                    "hump 0.38@1680nm with cellulose/lignin absorption 0.05@2100nm, shallow "
                    "leaf-water bands (0.45/0.55).",
         citation=refs("Elvidge (1990) Remote Sens. Environ. — cellulose absorption 2100nm",
                       "Asner (1998) Prog. Phys. Geogr. — NPV signatures")),
    dict(id="soil-loam-dry", material="soil", subclass="dry loam, low organic matter",
         name="Dry loamy soil (low organic matter)", model=m_soil_dry,
         derivation="Rising baseline 0.13@400nm to ~0.30@1300nm, weak ferric iron dip "
                    "(0.02@880nm), SWIR plateau ~0.345@1750nm with mild 2200nm clay/OH "
                    "feature, 1400/1930nm bands at 0.35/0.45 depth.",
         citation=refs("Stoner & Baumgardner (1981) Soil Sci. Soc. Am. J. — 0.4-2.5um soil curves",
                       "Ben-Dor, Irons & Epema (1999) — soil reflectance")),
    dict(id="soil-loam-moist", material="soil", subclass="moist loam",
         name="Moist loamy soil", model=m_soil_moist,
         derivation="Dry-loam shape with ~25% lower albedo and deep moisture bands "
                    "(0.72/0.80) — the canonical dry-vs-moist soil contrast.",
         citation=refs("Stoner & Baumgardner (1981) Soil Sci. Soc. Am. J.",
                       "Ben-Dor, Irons & Epema (1999) — soil moisture effects")),
    dict(id="soil-organic-dark", material="soil", subclass="organic-rich, dark",
         name="Organic-rich dark soil", model=m_soil_organic,
         derivation="Low, flattish curve (0.07@400nm to ~0.15@1750nm) from organic-matter "
                    "masking, mild 2200nm feature, moderate moisture bands (0.55/0.65).",
         citation=refs("Stoner & Baumgardner (1981) Soil Sci. Soc. Am. J. — organic-dominated soil",
                       "Ben-Dor, Irons & Epema (1999)")),
    dict(id="imperv-asphalt-new", material="impervious_surface", subclass="asphalt, new",
         name="Asphalt (new)", model=m_asphalt,
         derivation="Dark, gently rising curve 0.06@400nm to ~0.12@1500nm, slight SWIR "
                    "decline; featureless apart from mild water bands (0.30/0.40).",
         citation=refs("Herold, Gardner & Roberts (2003) Remote Sens. Environ. — urban spectral library")),
    dict(id="imperv-concrete-aged", material="impervious_surface", subclass="concrete, aged, bright",
         name="Concrete (aged, bright)", model=m_concrete,
         derivation="Bright 0.35-0.44 visible/NIR with slight green bias, SWIR decline to "
                    "~0.28@2400nm, weak 2200nm feature, mild water bands (0.35/0.45).",
         citation=refs("Herold, Gardner & Roberts (2003) Remote Sens. Environ. — urban spectral library")),
    dict(id="imperv-roof-dark", material="impervious_surface", subclass="bitumen roof, dark",
         name="Dark roof (bitumen)", model=m_roof_dark,
         derivation="Dark 0.08 rising to ~0.17@1600nm then declining; the low-albedo "
                    "companion of the impervious class, mild water bands (0.40/0.50).",
         citation=refs("Herold, Gardner & Roberts (2003) Remote Sens. Environ. — urban spectral library")),
    dict(id="rock-granite-bare", material="bare_rock", subclass="felsic, granite",
         name="Bare granite", model=m_granite,
         derivation="Bright felsic curve 0.25 rising to ~0.41@1400nm, mild 2200nm clay/OH "
                    "feature, water bands 0.35/0.45.",
         citation=refs("Hunt & Salisbury (1970) Air Force Cambridge Res. — mineral spectra",
                       "Clark et al. (2007) USGS Dig. Spectral Lib. 6 — felsic rock curves")),
    dict(id="rock-basalt-bare", material="bare_rock", subclass="mafic, basalt",
         name="Bare basalt", model=m_basalt,
         derivation="Dark mafic curve 0.05 rising to ~0.095@850nm with broad Fe2+ absorption "
                    "(0.025@1050nm), near-flat SWIR ~0.10, water bands 0.35/0.45.",
         citation=refs("Hunt & Salisbury (1970) Air Force Cambridge Res. — mafic mineral spectra",
                       "Clark et al. (2007) USGS Dig. Spectral Lib. 6 — basalt curves")),
    dict(id="sand-quartz-dry", material="sand", subclass="dry dune, quartz",
         name="Dry quartz sand", model=m_sand_dry,
         derivation="Bright rising curve 0.25 to ~0.51@1700nm with quartz reststrahlen "
                    "doublet (0.05@2200nm, 0.03@2330nm), water bands 0.30/0.40.",
         citation=refs("Hunt & Salisbury (1970) — quartz reststrahlen bands",
                       "Clark et al. (2007) USGS Dig. Spectral Lib. 6 — quartz")),
    dict(id="sand-beach-wet", material="sand", subclass="wet intertidal",
         name="Wet beach sand", model=m_sand_wet,
         derivation="Dry-sand shape at ~60% albedo with deep moisture bands (0.80/0.88) and "
                    "suppressed SWIR — the canonical dry/wet sand contrast.",
         citation=refs("Hunt & Salisbury (1970) — quartz",
                       "Clark et al. (2007) USGS Dig. Spectral Lib. 6 — water in minerals")),
    dict(id="snow-fresh", material="snow", subclass="fresh, fine-grained",
         name="Fresh snow", model=m_snow_fresh,
         derivation="Visible albedo 0.96 declining through 0.86@900nm, steep ice-absorption "
                    "collapse to 0.02 by 1350nm, bands at 1030/1260nm, residual dips at "
                    "1500/1930nm, small 2250nm feature.",
         citation=refs("Warren (1982) Rev. Geophys. — snow optics",
                       "Warren & Brandt (2008) J. Geophys. Res. — ice optical constants")),
    dict(id="ice-lake", material="ice", subclass="lake ice",
         name="Lake ice", model=m_ice_lake,
         derivation="Visible 0.72-0.78 (below snow), steeper collapse across 700-1250nm with "
                    "deeper relative 1030nm band, SWIR near zero beyond 1400nm.",
         citation=refs("Warren (1982) Rev. Geophys. — ice optics",
                       "Warren & Brandt (2008) J. Geophys. Res. — ice optical constants")),
    dict(id="cloud-water-thick", material="cloud", subclass="optically thick water cloud",
         name="Thick water cloud (top)", model=m_cloud_thick,
         derivation="Bright, spectrally flat 0.90-0.93 across visible/NIR; liquid-water dips "
                    "0.35@1450nm and 0.55@1940nm, mild 1200nm feature, SWIR decline beyond "
                    "2200nm — the water-cloud phase signature (ice clouds dip deeper at 1700nm).",
         citation=refs("Nakajima & King (1990) J. Atmos. Sci. — cloud reflectance vs effective radius",
                       "Platnick et al. (2003) IEEE TGRS — MODIS cloud optical properties")),
    dict(id="shadow-terrain-cast", material="shadow", subclass="terrain/cast, skylight-dominated",
         name="Terrain shadow (skylight)", model=m_shadow,
         derivation="Near-spectrally-flat dark spectrum 0.040@400nm to 0.030@2500nm — shadow "
                    "is identified by magnitude (low albedo), not shape; the classic shadow/"
                    "dark-water ambiguity is expected and documented.",
         citation=refs("Li & Strahler (1992) IEEE TGRS — geometric-optical shadow terms",
                       "Schaepman-Strub et al. (2006) Remote Sens. Environ. — reflectance conventions")),
    dict(id="crop-wheat-green", material="cropland", subclass="wheat, green canopy",
         name="Wheat canopy (green)", model=m_crop_wheat,
         derivation="Vegetation family with erectophile NIR 0.38, slight NPV/heading "
                    "admixture (+0.015 around 2200nm) and 20% open-row dry-soil background "
                    "mixing — the standard canopy+soil mixture of cereal rows.",
         citation=refs("Jacquemoud & Baret (1990) — canopy architecture effects",
                       "Thenkabail et al. (2000) Remote Sens. Rev. — crop hyperspectral bands")),
    dict(id="crop-corn-green", material="cropland", subclass="corn, green canopy",
         name="Corn canopy (green)", model=m_crop_corn,
         derivation="Broadleaf high-biomass canopy: NIR 0.50, deep leaf-water bands "
                    "(0.80/0.86), modest SWIR (0.24/0.13), 12% row soil background.",
         citation=refs("Jacquemoud & Baret (1990) — canopy reflectance",
                       "Thenkabail et al. (2000) Remote Sens. Rev. — crop hyperspectral bands")),
    dict(id="crop-rice-paddy", material="cropland", subclass="rice, paddy",
         name="Rice paddy canopy", model=m_crop_rice,
         derivation="Flooded-background canopy: NIR 0.34, very deep leaf-water bands "
                    "(0.85/0.90) and dark SWIR (0.16/0.09) from the water layer below.",
         citation=refs("Xiao et al. (2005) Remote Sens. Environ. — paddy rice signatures",
                       "Thenkabail et al. (2000) Remote Sens. Rev. — crop hyperspectral bands")),
    dict(id="burn-recent-char", material="burned_area", subclass="recent fire, char/ash",
         name="Recent burn (char/ash)", model=m_burn_recent,
         derivation="Very dark, nearly flat 0.03-0.06 with a weak ash bump (+0.008@500nm); "
                    "NIR collapse opposite to vegetation makes burns separable from green cover.",
         citation=refs("Pereira et al. (1999) — post-fire spectral response",
                       "Roy, Lewis & Justice (2002) Remote Sens. Environ. — burn scar spectra")),
]

CLOUD_NOTE = ""  # placeholder to keep entry order explicit


def build_entry(spec, wavelengths):
    values = [round(spec["model"](x), 4) for x in wavelengths]
    entry = {
        "id": spec["id"],
        "name": spec["name"],
        "material": spec["material"],
        "subclass": spec["subclass"],
        "wavelengths": [int(x) if float(x).is_integer() else x for x in wavelengths],
        "fwhm": [WAVELENGTH_STEP] * len(wavelengths),
        "reflectance": values,
        "source": f"rs-studio D12 synthetic model ({GENERATOR}::{spec['model'].__name__})",
        "license": LICENSE,
        "citation": f"Synthetic teaching spectrum dedicated to the public domain (CC0-1.0) "
                    f"by the rs-studio project; parametric model grounded in: {spec['citation']}.",
        "synthetic": True,
        "derivation": f"{spec['derivation']} Generated deterministically by {GENERATOR} "
                      f"({spec['model'].__name__}); magnitudes chosen to reproduce the "
                      f"canonical values in the cited literature.",
    }
    return entry


def build_library():
    wavelengths = grid()
    root = {
        "$schema": "library.schema.json",
        "format": "sicnu-spectral-library",
        "version": 2,
        "id": "rs-studio.builtin-spectral-library.v1",
        "entries": [build_entry(spec, wavelengths) for spec in ENTRIES],
    }
    return root, wavelengths


# ---------------------------------------------------------------------------
# Self checks (authoring gate — keeps the C++ smoke tests green by construction)
# ---------------------------------------------------------------------------

def sam_degrees(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(y * y for y in b))
    if na == 0.0 or nb == 0.0:
        return 180.0
    return math.degrees(math.acos(max(-1.0, min(1.0, dot / (na * nb)))))


def check_library(root, wavelengths):
    problems = []
    entries = root["entries"]
    ids = [e["id"] for e in entries]
    if len(set(ids)) != len(ids):
        problems.append("duplicate ids")
    for e in entries:
        if e["material"] not in TAXONOMY:
            problems.append(f"{e['id']}: material '{e['material']}' outside taxonomy")
        for i in range(1, len(wavelengths)):
            if wavelengths[i] <= wavelengths[i - 1]:
                problems.append(f"{e['id']}: grid not strictly increasing at {i}")
                break
        for i, v in enumerate(e["reflectance"]):
            if not (0.0 <= v <= 1.0) or math.isnan(v):
                problems.append(f"{e['id']}: reflectance[{i}]={v} outside [0,1]")
                break

    counts = {}
    for e in entries:
        counts[e["material"]] = counts.get(e["material"], 0) + 1
    for material in TAXONOMY:
        if material not in counts:
            problems.append(f"taxonomy material '{material}' has no entries")
    for material in ("water", "vegetation", "soil", "impervious_surface"):
        if counts.get(material, 0) < 3:
            problems.append(f"key material '{material}' has < 3 entries ({counts.get(material)})")

    spectra = {e["id"]: (e["reflectance"], e["material"]) for e in entries}

    def top_matches(query_id, n=3):
        q, _ = spectra[query_id]
        ranked = sorted(
            ((sam_degrees(q, s), jid, mat) for jid, (s, mat) in spectra.items()),
        )
        return [(d, jid, mat) for d, jid, mat in ranked if jid != query_id][:n]

    for query, expected in (("water-clear-deep", "water"),
                            ("vegetation-healthy-canopy", "vegetation")):
        # Crops are physically vegetation-shaped (green canopy over some soil);
        # the vegetation-family gate therefore accepts vegetation + cropland.
        # Water must match water strictly — dark-material ambiguity (shadow,
        # burn) must not beat a same-class entry.
        family = ("water",) if expected == "water" else ("vegetation", "cropland")
        best = top_matches(query, 2)
        for d, jid, mat in best:
            if mat not in family:
                problems.append(f"smoke gate: {query} best match {jid} is {mat}, "
                                f"expected {family} (SAM {d:.3f} deg)")

    # Class-cohesion report (informational, not a gate): inter-class ambiguity
    # (shadow vs water, NPV vs soil, crops vs vegetation) is real physics; the
    # hard gates are the smoke tests above and the numeric constraints.
    spectra_all = {e["id"]: (e["reflectance"], e["material"]) for e in entries}
    for material in ("water", "vegetation", "soil", "impervious_surface"):
        members = [jid for jid, (_, m) in spectra_all.items() if m == material]
        worst = None
        for m in members:
            q, _ = spectra_all[m]
            dists = sorted((sam_degrees(q, s), jid, mat)
                           for jid, (s, mat) in spectra_all.items() if jid != m)
            intra = [d for d, _, mat in dists if mat == material]
            inter = [d for d, _, mat in dists if mat != material]
            if intra and inter and min(inter) <= max(intra):
                line = (f"cohesion note: {material} member {m}: nearest non-class "
                        f"{min(inter):.2f} deg vs worst intra-class {max(intra):.2f} deg")
                print(f"NOTE: {line}")
                worst = line
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-only", action="store_true",
                        help="validate models and print diagnostics without writing")
    parser.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "library.json"))
    args = parser.parse_args()

    root, wavelengths = build_library()
    problems = check_library(root, wavelengths)

    # diagnostics
    for e in root["entries"]:
        r = e["reflectance"]
        bands = {"blue(480)": r[16], "green(560)": r[32], "red(660)": r[52],
                 "nir(840)": r[88], "swir1(1650)": r[250], "swir2(2200)": r[360]}
        pretty = " ".join(f"{k}={v:.3f}" for k, v in bands.items())
        print(f"{e['id']:28s} {pretty}")
    for q in ("water-clear-deep", "vegetation-healthy-canopy"):
        qspec, _ = {e["id"]: e["reflectance"] for e in root["entries"]}[q], None
        mats = {e["id"]: e["material"] for e in root["entries"]}
        ranked = sorted((sam_degrees(qspec, e["reflectance"]), e["id"], e["material"])
                        for e in root["entries"])
        print(f"top-3 for {q}:")
        for d, jid, mat in ranked[:4]:
            print(f"   {d:8.3f} deg  {jid:30s} {mat}")

    if problems:
        for p in problems:
            print(f"PROBLEM: {p}", file=sys.stderr)
        return 1

    payload = dumps(root)
    if not args.check_only:
        Path(args.out).write_text(payload, encoding="utf-8")
        print(f"wrote {args.out} ({len(payload.encode('utf-8'))} bytes, "
              f"{len(root['entries'])} entries)")
    return 0


def dumps(obj, indent=0):
    """JSON with number arrays inlined (readable diff-friendly output)."""
    pad = "  " * indent
    if isinstance(obj, dict):
        items = [f"{'  ' * (indent + 1)}{json.dumps(k)}: {dumps(v, indent + 1)}"
                 for k, v in obj.items()]
        return "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(obj, list):
        if obj and all(isinstance(x, (int, float)) for x in obj) and len(obj) > 8:
            return "[" + ", ".join(json.dumps(x) for x in obj) + "]"
        items = [f"{'  ' * (indent + 1)}{dumps(x, indent + 1)}" for x in obj]
        return "[\n" + ",\n".join(items) + "\n" + pad + "]"
    return json.dumps(obj)


if __name__ == "__main__":
    sys.exit(main())
