# 01 — Default Construction & Direct TaskCenter Submission

**What to build:** Make `SubmissionSink` and `CompletionWatcher` optional in `ToolCallDispatcher`. When not provided, default directly to `TaskCenter::instance().enqueueTask` and `TaskCenter::instance().taskUpdated` signal completion watching, eliminating mandatory lambda injection.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `ToolCallDispatcher` can be constructed with default zero arguments `ToolCallDispatcher()`
- [ ] Submitting an algorithm without custom sinks automatically enqueues to `TaskCenter::instance()`
- [ ] Completion watching automatically delivers task result payload upon task completion
- [ ] Unit tests in `test_tool_call_dispatcher.cpp` pass cleanly
