# Project Agent Guidelines & Conventions (`exp-rs`)

All AI agents assisting with coding, reviewing, or refactoring in this repository must strictly adhere to the following four core guidelines — Andrej Karpathy's guidelines for AI coding agents (provenance: `CHANGELOG.md`, "Agent & Tooling Integration" entry, 2026-08-03: "Configured project-scoped behavioral rules integrating Andrej Karpathy's 4 core guidelines").

Sibling runtime doc: `CLAUDE.md` (Claude Code reads it; zcode reads this file). Keep the two consistent; when they disagree, `CMakeLists.txt` / `CMakePresets.json` decide, then this file.

> **Unattended mode** (`/goal` with `autonomy=full`): where a guideline below says to surface options or clarify, write the options and the taken default into `.planning/<slug>/DECISIONS.md` instead of asking the user.

---

## 1. Think Before Coding
- **Surface Assumptions & Tradeoffs**: State explicit assumptions before writing code. If multiple architectural options or interpretations exist, present them explicitly rather than choosing silently.
- **Push Back on Complexity**: If a simpler design exists, point it out. Name any ambiguities and clarify before modifying code.

## 2. Simplicity First (YAGNI)
- **Minimum Code**: Write the minimum amount of code necessary to solve the exact problem.
- **No Speculative Flexibility**: Do not add unrequested abstractions, extra parameters, extension points, or hypothetical error handling.
- **Conciseness Target**: If an implementation can be done cleanly in 50 lines, do not write 200 lines.

## 3. Surgical Changes
- **Local Isolation**: Touch only the lines directly required for the task.
- **No Unrelated Churn**: Do not "clean up", reformat, or refactor adjacent, unrelated code, comments, or headers.
- **Style Alignment**: Match existing project C++20 / Qt 6 coding conventions exactly (`CMAKE_CXX_STANDARD 20`, `CMakeLists.txt:3`).
- **Orphan Cleanup**: Only delete unused variables/headers/functions that *your* changes rendered obsolete.

## 4. Goal-Driven Execution & Verification
- **Verifiable Success Criteria**: Every change must be backed by reproducible verification steps (e.g., unit tests, build targets).
- **Verification Loop**: Never declare a task complete without running CMake build and Catch2 unit test verification suites to ensure 100% green builds.
