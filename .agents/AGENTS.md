# Project Agent Guidelines & Conventions (`exp-rs`)

All AI agents assisting with coding, reviewing, or refactoring in this repository must strictly adhere to the following core guidelines derived from the [Karpathy Guidelines](file:///.agents/skills/karpathy-guidelines/SKILL.md).

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
- **Style Alignment**: Match existing project C++17 / Qt 6 coding conventions exactly.
- **Orphan Cleanup**: Only delete unused variables/headers/functions that *your* changes rendered obsolete.

## 4. Goal-Driven Execution & Verification
- **Verifiable Success Criteria**: Every change must be backed by reproducible verification steps (e.g., unit tests, build targets).
- **Verification Loop**: Never declare a task complete without running CMake build and Catch2 unit test verification suites to ensure 100% green builds.
