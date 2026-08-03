# Changelog

All notable changes to the `exp-rs` project will be documented in this file.

## [Unreleased] - 2026-08-03

### 🚀 Features & Deepening Architecture
- **Pipeline Status Enrichment**: Integrated `PipelineStatusResolver` callback injection in `WorkflowSession`, enabling authoritative DAG pipeline step completion status sync from `TaskCenter` without circular library dependencies.
- **Dynamic Worker Pool Control**: Implemented `PythonWorkerProcessPool::setPoolSize(int)` with dynamic grow/shrink capabilities and busy-worker protection.
- **Viewport Encapsulation**: Refactored `ActiveViewHost::viewportSnapshot()` to consolidate map canvas state into value-semantic `ViewportSnapshot` structs with single-point null safety.

### 🛡️ Security & Quality Fixes
- **IPC Socket Permissions (SEC-001)**: Restricted `QLocalServer` Unix domain socket permissions to `QLocalServer::UserAccessOption` (User-only `0700` access) to prevent multi-user local privilege escalation.
- **Transactional Upfront Shrink**: Added idle-count pre-validation to `PythonWorkerProcessPool::setPoolSize()` to guarantee atomicity and prevent pool size state desynchronization.
- **Qt6 Deprecation & Macro Safety**: Replaced deprecated `qMax` with `(std::max)` for header safety on Windows (`NOMINMAX`).
- **Container Growth Cap**: Enforced definition step bounds on `WorkflowSession::markStepComplete` and `snapshot()` step IDs.

### 🛠️ Agent & Tooling Integration
- **Agent Guidelines (`AGENTS.md`)**: Configured project-scoped behavioral rules integrating Andrej Karpathy's 4 core guidelines (Think Before Coding, Simplicity First, Surgical Changes, Goal-Driven Verification).
- **Skill Suites**: Installed `karpathy-guidelines` and the full `gstack` 59-skill suite for automated PR reviews, security auditing, and performance benchmarking.

---
*Verified against Catch2 test suite (245/245 assertions passing).*
