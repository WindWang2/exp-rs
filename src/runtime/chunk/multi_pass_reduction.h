// multi_pass_reduction.h — Pass-1 accumulation primitive for
// GlobalReductionStreaming operators (LSEE 10.0, ADR 0148 §1/§5-adjacent).
//
// A global-reduction streaming operator runs two passes: pass 1 folds every
// tile into one GLOBAL state (histogram, sum, min/max, count matrix…), pass
// 2 produces output from that state. This header owns the pass-1 shape so
// operators stop hand-rolling fold loops:
//
//   auto state = chunk::reduceTiles<Tile>(
//       tiles, init, /*fold*/[](Acc acc, const Tile&){ return ...; });
//
//  - Accumulation is SEQUENTIAL in tile order: the pass-1 state is the
//    deterministic baseline (ADR 0124 serial anchor). Parallel reduction is
//    a Tolerance-grade concern left to the operator, never this helper.
//  - Cancellation: an optional predicate is polled BETWEEN tiles; on cancel
//    the function returns the partial state with `cancelled = true` so the
//    caller can unwind without losing the accumulated prefix.
//  - Overflow-safe counting is the fold's responsibility (the accumulator is
//    a template parameter; this header adds no arithmetic of its own).
//
// Header-only and Qt-free (runtime layer charter).
#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace sicnu::runtime::chunk
{

/// Result of a pass-1 reduction over a tile range.
template<typename State>
struct ReductionResult
{
    State state{};
    bool cancelled = false;
    std::size_t tilesFolded = 0;
};

/// Folds @p tiles into @p initialState in order via @p fold(acc, tile) → acc.
/// @p isCancelled is polled between tiles (may be empty). Tiles are passed by
/// const reference: the fold must not retain them.
template<typename State, typename Tile, typename FoldFn>
ReductionResult<State> reduceTiles( const std::vector<Tile> &tiles, State initialState,
                                    FoldFn fold,
                                    std::function<bool()> isCancelled = {} )
{
    ReductionResult<State> result;
    result.state = std::move( initialState );
    for ( const Tile &tile : tiles )
    {
        if ( isCancelled && isCancelled() )
        {
            result.cancelled = true;
            return result;
        }
        result.state = fold( std::move( result.state ), tile );
        ++result.tilesFolded;
    }
    return result;
}

/// Streaming variant: folds a producer sequence (false = end of stream) —
/// the pass-1 shape for producers that never materialize the tile vector.
template<typename State, typename NextFn, typename FoldFn>
ReductionResult<State> reduceStream( NextFn next, State initialState, FoldFn fold,
                                     std::function<bool()> isCancelled = {} )
{
    ReductionResult<State> result;
    result.state = std::move( initialState );
    while ( true )
    {
        if ( isCancelled && isCancelled() )
        {
            result.cancelled = true;
            return result;
        }
        const auto *tile = next();
        if ( !tile )
            return result;
        result.state = fold( std::move( result.state ), *tile );
        ++result.tilesFolded;
    }
}

} // namespace sicnu::runtime::chunk
