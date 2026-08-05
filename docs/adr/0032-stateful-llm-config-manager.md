# ADR 0032: Stateful LlmConfigManager Facade Architecture

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`LlmConfigManager` was a shallow class containing static helper functions (`activeProfile()`, `setActiveProfile()`, `loadProfiles()`, `saveProfiles()`) that queried and wrote to `QSettings` directly on every call. Because it held no in-memory state and emitted no signals, UI components like `AgentCopilotDockWidget` and `LlmSettingsDialog` had to write custom synchronization logic or stay out of sync when active LLM provider profiles changed.

## Decision

1. **Stateful `QObject` Singleton**: Convert `LlmConfigManager` to a `QObject` singleton (`LlmConfigManager::instance()`) that loads profiles from `QSettings` once on startup, caches them in memory, and manages updates atomically.
2. **Reactive Signals**: Expose `activeProfileChanged(const LlmProviderProfile &profile)` and `profilesChanged()` signals on `LlmConfigManager::instance()`. UI widgets connect to these signals to auto-update combo boxes and model parameters reactively.
3. **Zero-Breakage Backwards Compatibility**: Retain static methods (`activeProfile()`, `setActiveProfile(...)`, `loadProfiles()`, `saveProfiles(...)`) as inline/forwarding wrappers delegating to `LlmConfigManager::instance()`.

## Consequences

- **Locality**: Profile defaults, caching, `QSettings` serialization, and signal dispatch concentrate inside `LlmConfigManager`.
- **Leverage**: Inter-widget pointer callbacks between settings dialogs and dock widgets are eliminated.
- **Testability**: `LlmConfigManager` instance methods and signals can be tested headlessly.
