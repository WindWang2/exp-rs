// sar_complex.h — complex (SLC) raster contract and bounded tile access
// (Advanced SAR / PolSAR / InSAR 10.0, capability package A).
//
// One SLC channel is one CFloat32 GDAL band holding the complex scattering
// amplitude. Complex kernels must NOT pass through the float BIP streams
// (GdalMultibandBlockStream): GDAL's complex→float conversion keeps only the
// real part, which silently destroys the phase this whole family exists for.
// Everything here reads NATIVE CFloat32 windows (GdalDatasetWrapper::
// readBandWindowNative) and writes CFloat32 tiles (GdalStreamingOutput::
// writeTileRaw with GDT_CFloat32).
//
// Channel identity contract (additive dataset metadata, docs/processing/
// sar-domain.md §6):
//   SICNU_SAR_COMPLEX_CHANNELS  "HH;HV;VV" (reciprocity: SHV = SVH) or
//                               "HH;HV;VH;VV" — token order IS band order.
// Operators additionally accept explicit band-mapping parameters
// (hh_band / hv_band / …); an explicit mapping wins over the declaration.
// A channel declared but absent from the raster is a typed refusal at the
// operator seam (POLARIZATION_MISMATCH), never a guessed band.
//
// Numeric-domain policy (mirrors sar_metadata.h):
//   * A complex sample is INVALID iff either component is non-finite, or the
//     band declares a NoData sentinel and BOTH components equal it (the
//     float-family convention generalized componentwise). Invalid samples are
//     normalized to (NaN, NaN) before any kernel sees them — never clamped,
//     never replaced by a plausible zero.
//   * Power = |s|² (linear, ≥ 0); amplitude = |s|; phase = atan2 in radians.
//     The linear/dB bookkeeping of the detected families applies to power
//     products unchanged.
//
// Memory: tile-based only. The complex tile stream materializes O(tile +
// halo) per step; full-raster complex buffers are never created here.
#pragma once

#include <complex>
#include <functional>
#include <QString>
#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;
class GdalBlockStream;

namespace sicnu::sar
{

/// Dataset metadata key declaring the complex channel order (band order).
inline const char *kComplexChannelsKey = "SICNU_SAR_COMPLEX_CHANNELS";

/// Physical identity of one complex channel.
enum class PolChannel
{
    Hh,
    Hv,
    Vh,
    Vv,
};

/// Channel token → identity; accepts "HH"/"hh"/"Hh" case-insensitively.
/// @return false (out untouched) on an unknown token.
bool parsePolChannel( const QString &token, PolChannel *out );

/// Canonical upper-case token ("HH", "HV", "VH", "VV").
QString polChannelToString( PolChannel channel );

/// Parses the declared channel order "HH;HV;VV" (semicolon-separated,
/// order = band order). Returns false with @a error when a token is unknown
/// or the list is empty; duplicate channels are rejected (a raster cannot
/// carry the same identity twice without an explicit band mapping).
bool parseChannelDeclaration( const QString &value,
                              std::vector<PolChannel> *out,
                              QString *error = nullptr );

/// Contract validation for a complex-band mapping: every band is within
/// [1, bandCount], has native dtype GDT_CFloat32, and the band list is
/// duplicate-free. Returns false with a human-readable @a error otherwise
/// (callers refuse; they never downcast complex bands to float).
bool validateComplexBands( const GdalDatasetWrapper &ds,
                           const std::vector<int> &bands1based,
                           QString *error = nullptr );

/// True when the band's native dtype is GDT_CFloat32.
bool isComplexBand( const GdalDatasetWrapper &ds, int band1based );

/// True when the sample is inside the complex domain (both components finite
/// and not the componentwise NoData sentinel pair). Applies the declared
/// band sentinel when present.
bool complexSampleValid( const GdalDatasetWrapper &ds, int band1based,
                         std::complex<float> sample );

/// |s|² as double. NaN for invalid (non-finite) input — callers normalize
/// first via the stream below.
double complexPower( std::complex<float> sample );
/// |s| as double; NaN for non-finite input.
double complexAmplitude( std::complex<float> sample );
/// atan2(im, re) in radians ∈ (−π, π]; NaN for the zero sample (phase of a
/// zero vector is undefined — masked like any other invalid geometry).
double complexPhaseRad( std::complex<float> sample );

struct ComplexTile
{
    int xOffset = 0;         ///< core-tile left edge (0-based)
    int yOffset = 0;         ///< core-tile top edge (0-based)
    int width = 0;           ///< core width (edge-clamped)
    int height = 0;          ///< core height (edge-clamped)
    int halo = 0;            ///< halo radius in pixels
    int bufferWidth = 0;     ///< width + 2·halo
    int bufferHeight = 0;    ///< height + 2·halo
    int index = 0;
    int totalTiles = 0;
};

/**
 * Streaming complex tile iterator: reads every band of @a bands1based
 * (native CFloat32) into one pixel-interleaved std::complex<float> buffer,
 * one tile at a time — the complex counterpart of GdalMultibandBlockStream.
 *
 * BIP layout: pixels[((y * bufferWidth) + x) * bandCount + b], y/x relative
 * to the halo origin (x,y = core position + halo). Halo regions are filled
 * by edge replication from the valid read window (same semantics as
 * GdalBlockStream's halo path). Positions outside the raster in halo-less
 * mode cannot occur (tiles are edge-clamped).
 *
 * Invalid samples — non-finite components or the componentwise declared
 * NoData sentinel pair — are normalized to (NaN, NaN) in the stream, so
 * kernels never see sentinel values (sar_metadata.h per-kernel policy).
 *
 * Threading: sequential, single-threaded (GDAL reads on one dataset are not
 * concurrent-safe; same contract as the float streams). The buffer is
 * BORROWED for one callback invocation.
 */
class ComplexBandTileStream
{
  public:
    /// @a bands1based must pass validateComplexBands(). halo >= 0.
    ComplexBandTileStream( const GdalDatasetWrapper &ds,
                           const std::vector<int> &bands1based,
                           int tileWidth = 256, int tileHeight = 256,
                           int halo = 0 );

    int tileCount() const { return static_cast<int>( m_tiles.size() ); }
    const ComplexTile &tile( int i ) const { return m_tiles[i]; }
    int bandCount() const { return static_cast<int>( m_bands.size() ); }

    using TileCallback =
        std::function<bool( const ComplexTile &tile, const std::complex<float> *pixelsBip )>;
    /// @return false when a read failed or the callback aborted (early exit).
    bool forEach( const TileCallback &callback ) const;

    /// Reads tile @a index into @a pixelsBip (size bufferWidth·bufferHeight·
    /// bandCount complex samples, same layout as the forEach callback).
    /// Exposed so callers can walk TWO identically-gridded rasters in
    /// lockstep (twin-stream interferometry).
    bool readTile( int index, std::complex<float> *pixelsBip ) const;

  private:
    const GdalDatasetWrapper &m_ds;
    std::vector<int> m_bands;
    int m_tileWidth;
    int m_tileHeight;
    int m_halo;
    int m_rasterWidth;
    int m_rasterHeight;
    std::vector<ComplexTile> m_tiles;
};

/// Writes one tile of one band from a complex buffer (row-major,
/// tile.width * tile.height samples) as GDT_CFloat32 via the streaming
/// output's typed tile seam. Returns false on write failure.
bool writeComplexTile( GdalStreamingOutput &output, int band1based,
                       int xOffset, int yOffset, int width, int height,
                       const std::complex<float> *pixels );

} // namespace sicnu::sar
