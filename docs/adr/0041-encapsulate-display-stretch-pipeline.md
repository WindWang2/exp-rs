# ADR 0041: Encapsulate Raster Stretch Resolution in DisplayStretchPipeline

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

Display stretch resolution and application logic was exposed via free functions (`validate`, `resolve`, `apply`) in `display_stretch.h`.

## Decision

1. **Pipeline Class**: Encapsulate stretch validation, resolution, and target application into a `DisplayStretchPipeline` deep module class.
2. **Backward-Compatible Free Functions**: Maintain inline free function wrappers forwarding to `DisplayStretchPipeline` for existing callers.

## Consequences

- **Module Deepening**: Provides a clear class interface for display stretch operations.
- **Maintainability**: Centralizes stretch processing rules and statistical resolution inside `DisplayStretchPipeline`.
