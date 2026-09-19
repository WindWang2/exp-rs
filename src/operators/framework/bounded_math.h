// bounded_math.h — Checked integer math and documented ceilings for every
// manifest/user integer that feeds an allocation or index computation in the
// operator runtime (#1044).
//
// The runtime contract: ANY integer that enters allocation or index math is
// (a) bounded at the validation boundary against one of the ceilings below —
// each ceiling is derived from an existing runtime cap or the repository's
// own convention, never an arbitrary constant — and (b) computed in 64-bit
// checked arithmetic so a value that slips past validation cannot overflow
// into a truncated allocation or an out-of-bounds index.
//
// Qt-free and header-only: usable from the catalog validator, the tile
// engines and the OpenCV operators alike.
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace sicnu::operators {

/// Declared temporal_length / fed-frames ceiling. The multimodal engine
/// materializes every declared frame per tile window in memory (missing
/// frames are zero-filled), so the declared axis is bounded exactly like the
/// PROVIDED axis the engine already enforces (kMaxTemporalFrames semantics,
/// Platform 8.0 WP-D: "an unbounded time axis is an unbounded allocation").
inline constexpr std::int64_t kMaxTemporalFrames = 1024;

/// preprocess.pad ceiling (Platform 7.0). The pad grows every tile window on
/// all four sides BEFORE the forward pass (`tileSize + 2*halo + 2*pad`), so
/// an unbounded pad makes the inference window memory-unbounded — the same
/// rationale as the existing tiling.tile_size 32768 / tiling.halo tile_size/2
/// ceilings. 1024 px of context padding is far above any real EO use.
inline constexpr std::int64_t kMaxPreprocessPadPx = 1024;

/// Odd kernel-size ceiling for windowed raster filters. The repository
/// already clamps spatial windows to [3, 101] (raster_spatial operators) and
/// morphology kernels to [3, 65]; the unclamped OpenCV filter /
/// image-enhancement paths get the same documented ceiling. A kernel beyond
/// ~100 px has no scientific meaning (σ equivalent > 40 px) and its halo
/// buffer scales with the square of the radius.
inline constexpr std::int64_t kMaxKernelPx = 101;

/// Fixed model input extent ceiling (input.width/height). These extents size
/// the per-tile resize target (`preprocess.resize == "to_input"`), so they
/// inherit the tiling.tile_size "absurdly large" bound.
inline constexpr std::int64_t kMaxDeclaredInputPx = 32768;

/// Hard ceiling for one inference window's side (tile + halo + pad). Tile
/// engines compute the window side in int64 and refuse anything above this
/// BEFORE the buffer allocation; the catalog-legal maximum
/// (32768 + 2*16384 + 2*1024 = 67584) sits far below it.
inline constexpr std::int64_t kMaxWindowPx = 131072;

/// Hard ceiling for ONE window buffer in float elements (4 GiB of float
/// payload). windowPx² × channels above this is a typed refusal, never a
/// multi-GB allocation attempt — the window is materialized once per feed
/// outside the engine's OOM ladder, so std::bad_alloc there is not a typed
/// failure today. Note the budget is INDEPENDENT of the side bound: the
/// catalog-legal maximum side (67584 px) is far below kMaxWindowPx, but
/// 67584² floats alone exceed this budget, so a large tile_size with any
/// real channel count type-fails here FIRST (review P2: the two ceilings
/// are deliberately not convertible — this one bounds memory, the other
/// bounds geometry).
inline constexpr std::uint64_t kMaxWindowFloats = 1ull << 30;

/// 64-bit checked multiply on non-negative quantities. nullopt = the true
/// result does not fit (or a negative operand was supplied) — callers turn
/// that into a typed refusal instead of wrapping.
inline std::optional<std::int64_t> checkedMul( std::int64_t a, std::int64_t b )
{
  if ( a < 0 || b < 0 )
    return std::nullopt;
  if ( a != 0 && b > std::numeric_limits<std::int64_t>::max() / a )
    return std::nullopt;
  return a * b;
}

} // namespace sicnu::operators
